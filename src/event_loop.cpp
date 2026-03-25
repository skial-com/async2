#include "event_loop.h"
#include "data/data_node.h"
#include "handle_manager.h"
#include "http_request.h"
#include "tcp_socket.h"
#include "udp_socket.h"
#include "ws_connection.h"
#include "socket_event.h"
#include "msgpack/msgpack_parse.h"
#include "msgpack/msgpack_serialize.h"
#include <cmath>
#include <cstring>
#include <random>
#include <unordered_set>
#include "extension.h"
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

extern HandleManager g_handle_manager;

static void ShareLockCb(CURL*, curl_lock_data, curl_lock_access, void*) {}
static void ShareUnlockCb(CURL*, curl_lock_data, void*) {}

EventLoop::EventLoop() {
    curl_multi_ = nullptr;
    curl_share_ = nullptr;
    loop_ = nullptr;
}

EventLoop::~EventLoop() {
}

bool EventLoop::Start() {
    loop_ = new uv_loop_t;
    if (uv_loop_init(loop_) != 0) {
        delete loop_;
        loop_ = nullptr;
        return false;
    }

    curl_multi_ = curl_multi_init();
    if (!curl_multi_) {
        uv_loop_close(loop_);
        delete loop_;
        loop_ = nullptr;
        return false;
    }

    curl_multi_setopt(curl_multi_, CURLMOPT_SOCKETFUNCTION, OnCurlSocket);
    curl_multi_setopt(curl_multi_, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(curl_multi_, CURLMOPT_TIMERFUNCTION, OnCurlTimeout);
    curl_multi_setopt(curl_multi_, CURLMOPT_TIMERDATA, this);

    curl_share_ = curl_share_init();
    curl_share_setopt(curl_share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    curl_share_setopt(curl_share_, CURLSHOPT_LOCKFUNC, ShareLockCb);
    curl_share_setopt(curl_share_, CURLSHOPT_UNLOCKFUNC, ShareUnlockCb);

    loop_->data = this;

    uv_timer_init(loop_, &timeout_);
    timeout_.data = this;

    uv_async_init(loop_, &async_stop_, OnAsyncStop);
    async_stop_.data = this;
    uv_async_init(loop_, &async_add_, OnAsyncAdd);
    async_add_.data = this;
    uv_async_init(loop_, &async_settings_, OnAsyncSettings);
    async_settings_.data = this;
    uv_async_init(loop_, &async_cancel_, OnAsyncCancel);
    async_cancel_.data = this;
    uv_async_init(loop_, &async_tcp_op_, OnAsyncTcpOp);
    async_tcp_op_.data = this;
    uv_async_init(loop_, &async_udp_op_, OnAsyncUdpOp);
    async_udp_op_.data = this;
    uv_async_init(loop_, &async_dns_op_, OnAsyncDnsOp);
    async_dns_op_.data = this;
    uv_async_init(loop_, &async_ws_op_, OnAsyncWsOp);
    async_ws_op_.data = this;

    if (!dns_resolver_.Init(loop_)) {
        // Clean up and fail
        uv_close(reinterpret_cast<uv_handle_t*>(&async_stop_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_add_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_settings_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_cancel_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_tcp_op_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_udp_op_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_dns_op_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_ws_op_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&timeout_), nullptr);
        uv_run(loop_, UV_RUN_DEFAULT);
        uv_loop_close(loop_);
        delete loop_;
        loop_ = nullptr;
        curl_share_cleanup(curl_share_);
        curl_share_ = nullptr;
        curl_multi_cleanup(curl_multi_);
        curl_multi_ = nullptr;
        return false;
    }

    if (uv_thread_create(&thread_, EventLoopThread, this) != 0) {
        uv_close(reinterpret_cast<uv_handle_t*>(&async_stop_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_add_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_settings_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_cancel_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_tcp_op_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_udp_op_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_dns_op_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_ws_op_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&timeout_), nullptr);
        dns_resolver_.Shutdown();
        uv_run(loop_, UV_RUN_DEFAULT);
        uv_loop_close(loop_);
        delete loop_;
        loop_ = nullptr;
        curl_share_cleanup(curl_share_);
        curl_share_ = nullptr;
        curl_multi_cleanup(curl_multi_);
        curl_multi_ = nullptr;
        return false;
    }

    return true;
}

static void OnHandleClosed(uv_handle_t* handle) {
    // no-op, just need the callback for uv_close
}

void EventLoop::Stop() {
    uv_async_send(&async_stop_);
    uv_thread_join(&thread_);

    // Close pending retry timers before shutting down curl
    for (auto* timer : retry_timers_) {
        uv_timer_stop(timer);
        uv_close(reinterpret_cast<uv_handle_t*>(timer), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_timer_t*>(h);
        });
    }
    retry_timers_.clear();

    // Clear reconnect timer tracking — individual timers are stopped
    // by WsStopTimers in the loop below
    ws_reconnect_timers_.clear();

    // Clean up WebSocket connections — stop timers and poll handles
    for (auto& [id, conn] : ws_connections_) {
        WsStopTimers(conn);
        if (conn->uv_poll) {
            uv_poll_stop(conn->uv_poll);
            uv_close(reinterpret_cast<uv_handle_t*>(conn->uv_poll), [](uv_handle_t* h) {
                delete reinterpret_cast<uv_poll_t*>(h);
            });
            conn->uv_poll = nullptr;
        }
    }

    // Close TCP/UDP socket handles so uv_run can drain them
    for (auto& [id, sock] : tcp_sockets_) {
        if (sock->uv_handle) {
            uv_close(reinterpret_cast<uv_handle_t*>(sock->uv_handle), nullptr);
            sock->uv_handle = nullptr;
        }
    }
    for (auto& [id, sock] : udp_sockets_) {
        if (sock->uv_handle) {
            uv_close(reinterpret_cast<uv_handle_t*>(sock->uv_handle), nullptr);
            sock->uv_handle = nullptr;
        }
    }

    // Remove in-flight HTTP easy handles from curl_multi before cleanup.
    // curl_multi_cleanup frees the connection pool; any easy handles still
    // in the multi would have dangling pointers to freed pool data, causing
    // use-after-free when curl_easy_cleanup runs later in ~HttpRequest.
    for (CURL* easy : http_curl_handles_) {
        curl_multi_remove_handle(curl_multi_, easy);
    }
    http_curl_handles_.clear();

    // Remove WS easy handles from curl_multi and clean them up.
    // curl_multi_cleanup does NOT free easy handles — they must be
    // removed and cleaned up explicitly before the multi is destroyed.
    for (CURL* easy : ws_curl_handles_) {
        curl_multi_remove_handle(curl_multi_, easy);
        curl_easy_cleanup(easy);
    }
    ws_curl_handles_.clear();

    curl_share_cleanup(curl_share_);
    curl_share_ = nullptr;

    // Clean up curl_multi BEFORE closing the uv loop — curl_multi_cleanup
    // tears down the connection pool which triggers socket callbacks (CURL_POLL_REMOVE
    // and possibly CURL_POLL_IN/OUT for TLS shutdown). The uv loop is still valid
    // at this point so the callbacks work normally and close the poll handles.
    dns_resolver_.Shutdown();
    curl_multi_cleanup(curl_multi_);
    curl_multi_ = nullptr;

    // Close all uv handles so uv_loop_close succeeds
    uv_close(reinterpret_cast<uv_handle_t*>(&async_stop_), OnHandleClosed);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_add_), OnHandleClosed);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_settings_), OnHandleClosed);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_cancel_), OnHandleClosed);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_tcp_op_), OnHandleClosed);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_udp_op_), OnHandleClosed);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_dns_op_), OnHandleClosed);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_ws_op_), OnHandleClosed);
    uv_close(reinterpret_cast<uv_handle_t*>(&timeout_), OnHandleClosed);

    // Run the loop once more to process the close callbacks
    uv_run(loop_, UV_RUN_DEFAULT);

    uv_loop_close(loop_);
    delete loop_;
    loop_ = nullptr;

    // Drain queues — detach from handle map first to prevent CleanHandles
    // from double-freeing. Use a set to avoid double-free if a request
    // appears in multiple queues (e.g., cancel_queue + done_queue).
    std::unordered_set<HttpRequest*> deleted;

    auto drain_http_queue = [&](LockedQueue<HttpRequest*>& q) {
        q.Lock();
        while (!q.Empty()) {
            HttpRequest* req = q.Pop();
            if (deleted.insert(req).second)
                g_handle_manager.FreeHandle(req->handle_id);
        }
        q.Unlock();
    };
    drain_http_queue(pending_queue);
    drain_http_queue(cancel_queue);
    drain_http_queue(done_queue);

    option_queue.Lock();
    while (!option_queue.Empty()) delete option_queue.Pop();
    option_queue.Unlock();

    // Drain TCP/UDP queues
    tcp_op_queue.Lock();
    while (!tcp_op_queue.Empty()) delete tcp_op_queue.Pop();
    tcp_op_queue.Unlock();

    udp_op_queue.Lock();
    while (!udp_op_queue.Empty()) delete udp_op_queue.Pop();
    udp_op_queue.Unlock();

    socket_done_queue.Lock();
    while (!socket_done_queue.Empty()) delete socket_done_queue.Pop();
    socket_done_queue.Unlock();

    dns_op_queue.Lock();
    while (!dns_op_queue.Empty()) delete dns_op_queue.Pop();
    dns_op_queue.Unlock();

    // Drain WebSocket queue
    ws_op_queue.Lock();
    while (!ws_op_queue.Empty()) delete ws_op_queue.Pop();
    ws_op_queue.Unlock();

    // Free TCP/UDP/WS objects via handle manager (deletes + removes from map,
    // so CleanHandles() won't double-free).
    for (auto& [id, sock] : tcp_sockets_)
        g_handle_manager.FreeHandle(id);
    tcp_sockets_.clear();
    for (auto& [id, sock] : udp_sockets_)
        g_handle_manager.FreeHandle(id);
    udp_sockets_.clear();
    for (auto& [id, conn] : ws_connections_) {
        conn->curl_handle = nullptr;
        g_handle_manager.FreeHandle(id);
    }
    ws_connections_.clear();
}

void EventLoop::EnqueueRequest(HttpRequest* request) {
    request->PrepareForSend();
    stats_pending.fetch_add(1, std::memory_order_relaxed);
    pending_queue.Lock();
    pending_queue.Push(request);
    pending_queue.Unlock();
    uv_async_send(&async_add_);
}

void EventLoop::CancelRequest(HttpRequest* request) {
    cancel_queue.Lock();
    cancel_queue.Push(request);
    cancel_queue.Unlock();
    uv_async_send(&async_cancel_);
}

void EventLoop::DrainCompleted(std::vector<HttpRequest*>& out) {
    done_queue.Lock();
    while (!done_queue.Empty()) {
        out.push_back(done_queue.Pop());
    }
    done_queue.Unlock();
}

void EventLoop::SignalPending() {
    if (pending_queue.HasItems()) {
        uv_async_send(&async_add_);
    }
}

void EventLoop::ChangeCurlSetting(CURLMoption key, long value) {
    CurlGlobalOption* opt = new CurlGlobalOption();
    opt->key = key;
    opt->value = value;
    option_queue.Lock();
    option_queue.Push(opt);
    option_queue.Unlock();
    uv_async_send(&async_settings_);
}

// Thread entry point
void EventLoop::EventLoopThread(void* data) {
    EventLoop* self = static_cast<EventLoop*>(data);
    uv_run(self->loop_, UV_RUN_DEFAULT);
    // Clean up this thread's parser before thread exits (avoids leak on dlclose)
    DataParserCleanup();
}

// UV timer fired
void EventLoop::OnTimeout(uv_timer_t* req) {
    EventLoop* self = static_cast<EventLoop*>(req->data);
    int running;
    curl_multi_socket_action(self->curl_multi_, CURL_SOCKET_TIMEOUT, 0, &running);
    self->CheckCompletedJobs();
}

// UV poll activity on a socket
void EventLoop::OnSocketActivity(uv_poll_t* req, int status, int events) {
    CurlSocketContext* socket_ctx = static_cast<CurlSocketContext*>(req->data);
    EventLoop* self = static_cast<EventLoop*>(socket_ctx->uv_poll_handle.loop->data);

    uv_timer_stop(&self->timeout_);

    int running;
    int flags = 0;
    if (status < 0) {
        flags = CURL_CSELECT_ERR;
    } else {
        if (events & UV_READABLE)
            flags |= CURL_CSELECT_IN;
        if (events & UV_WRITABLE)
            flags |= CURL_CSELECT_OUT;
    }

    curl_multi_socket_action(self->curl_multi_, socket_ctx->curl_socket, flags, &running);
    self->CheckCompletedJobs();
}

// Curl wants us to adjust timeout
int EventLoop::OnCurlTimeout(CURLM* multi, long timeout_ms, void* userdata) {
    EventLoop* self = static_cast<EventLoop*>(userdata);
    if (timeout_ms == -1) {
        uv_timer_stop(&self->timeout_);
        return 0;
    }
    uv_timer_start(&self->timeout_, OnTimeout, timeout_ms, 0);
    return 0;
}

// Curl wants us to watch/unwatch a socket
int EventLoop::OnCurlSocket(CURL* easy, curl_socket_t s, int action, void* data, void* socketdata) {
    EventLoop* self = static_cast<EventLoop*>(data);

    if (action == CURL_POLL_REMOVE) {
        if (socketdata) {
            auto* context = static_cast<CurlSocketContext*>(socketdata);
            context->BeginRemoval();
            curl_multi_assign(self->curl_multi_, s, nullptr);
        }
        return 0;
    }

    if (action == CURL_POLL_IN || action == CURL_POLL_OUT || action == CURL_POLL_INOUT) {
        CurlSocketContext* context;
        if (!socketdata) {
            context = new CurlSocketContext(s, self->loop_);
            curl_multi_assign(self->curl_multi_, s, context);
        } else {
            context = static_cast<CurlSocketContext*>(socketdata);
        }
        // CURL_POLL_IN=1, OUT=2, INOUT=3 (IN|OUT) — bitmask by design
        int flags = 0;
        if (action & CURL_POLL_IN)  flags |= UV_READABLE;
        if (action & CURL_POLL_OUT) flags |= UV_WRITABLE;
        uv_poll_start(&context->uv_poll_handle, flags, OnSocketActivity);
    }

    return 0;
}

// Curl response body data callback
size_t EventLoop::OnReceiveData(char* contents, size_t size, size_t nmemb, void* userdata) {
    size_t realsize = size * nmemb;
    HttpRequest* req = static_cast<HttpRequest*>(userdata);
    req->response_body.insert(req->response_body.end(), contents, contents + realsize);
    return realsize;
}

// Curl response header callback
size_t EventLoop::OnReceiveHeaders(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t realsize = size * nitems;
    HttpRequest* req = static_cast<HttpRequest*>(userdata);

    // Find the colon delimiter directly in the buffer to avoid temporary strings
    const char* colon = static_cast<const char*>(memchr(buffer, ':', realsize));
    if (!colon)
        return realsize;

    // Build lowercase header name
    std::string name(buffer, colon - buffer);
    for (char& c : name) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

    // Trim whitespace from value
    const char* val_start = colon + 1;
    const char* val_end = buffer + realsize;
    while (val_start < val_end && (*val_start == ' ' || *val_start == '\t')) val_start++;
    while (val_end > val_start && (val_end[-1] == '\r' || val_end[-1] == '\n' ||
                                    val_end[-1] == ' '  || val_end[-1] == '\t')) val_end--;

    req->response_headers[name].assign(val_start, val_end);
    return realsize;
}

// Event thread: add pending jobs
void EventLoop::OnAsyncAdd(uv_async_t* handle) {
    EventLoop* self = static_cast<EventLoop*>(handle->data);
    self->pending_queue.Lock();
    while (!self->pending_queue.Empty()) {
        HttpRequest* req = self->pending_queue.Pop();
        self->stats_pending.fetch_sub(1, std::memory_order_relaxed);
        self->active_http_requests_.insert(req);
        if (req->handle_closed) {
            // Closed before we picked it up — never added to curl_multi
            req->completed = true;
            if (req->body_node) {
                DataNode::Decref(req->body_node);
                req->body_node = nullptr;
            }
            req->OnCompleted();
            self->stats_completed.fetch_add(1, std::memory_order_relaxed);
            self->active_http_requests_.erase(req);
            self->done_queue.Lock();
            self->done_queue.Push(req);
            self->done_queue.Unlock();
            continue;
        }
        req->SetupCurl();
        self->ProcessCurlJob(req);
    }
    self->pending_queue.Unlock();
}

// Event thread: stop loop
void EventLoop::OnAsyncStop(uv_async_t* handle) {
    EventLoop* self = static_cast<EventLoop*>(handle->data);
    uv_stop(self->loop_);
}

// Event thread: update curl global settings
void EventLoop::OnAsyncSettings(uv_async_t* handle) {
    EventLoop* self = static_cast<EventLoop*>(handle->data);
    self->option_queue.Lock();
    while (!self->option_queue.Empty()) {
        CurlGlobalOption* opt = self->option_queue.Pop();
        curl_multi_setopt(self->curl_multi_, opt->key, opt->value);
        delete opt;
    }
    self->option_queue.Unlock();
}

// Event thread: cancel in-flight jobs
void EventLoop::OnAsyncCancel(uv_async_t* handle) {
    EventLoop* self = static_cast<EventLoop*>(handle->data);
    self->cancel_queue.Lock();
    while (!self->cancel_queue.Empty()) {
        HttpRequest* req = self->cancel_queue.Pop();
        // Already completed and handed to game thread (which may have freed it) — skip.
        // Validate via active set instead of dereferencing the potentially-freed pointer.
        if (self->active_http_requests_.find(req) == self->active_http_requests_.end())
            continue;
        if (req->in_retry_wait) {
            // Stop and close the retry timer to prevent use-after-free
            for (auto it = self->retry_timers_.begin(); it != self->retry_timers_.end(); ++it) {
                if ((*it)->data == req) {
                    uv_timer_stop(*it);
                    uv_close(reinterpret_cast<uv_handle_t*>(*it), [](uv_handle_t* h) {
                        delete reinterpret_cast<uv_timer_t*>(h);
                    });
                    self->retry_timers_.erase(it);
                    break;
                }
            }
        } else {
            // Currently in curl_multi
            self->http_curl_handles_.erase(req->curl);
            curl_multi_remove_handle(self->curl_multi_, req->curl);
            self->stats_active.fetch_sub(1, std::memory_order_relaxed);
        }
        req->completed = true;
        req->in_retry_wait = false;
        req->OnCompleted();
        self->stats_completed.fetch_add(1, std::memory_order_relaxed);
        self->active_http_requests_.erase(req);
        self->done_queue.Lock();
        self->done_queue.Push(req);
        self->done_queue.Unlock();
    }
    self->cancel_queue.Unlock();
}

void EventLoop::ProcessCurlJob(HttpRequest* request) {
    curl_easy_setopt(request->curl, CURLOPT_WRITEFUNCTION, OnReceiveData);
    curl_easy_setopt(request->curl, CURLOPT_WRITEDATA, static_cast<void*>(request));
    curl_easy_setopt(request->curl, CURLOPT_HEADERFUNCTION, OnReceiveHeaders);
    curl_easy_setopt(request->curl, CURLOPT_HEADERDATA, static_cast<void*>(request));
    curl_easy_setopt(request->curl, CURLOPT_PRIVATE, static_cast<void*>(request));
    curl_easy_setopt(request->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(request->curl, CURLOPT_ERRORBUFFER, request->curl_error_message);
    if (request->built_headers_)
        curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->built_headers_);
    stats_active.fetch_add(1, std::memory_order_relaxed);
    http_curl_handles_.insert(request->curl);
    curl_multi_add_handle(curl_multi_, request->curl);
}

void EventLoop::CheckCompletedJobs() {
    CURLMsg* message;
    int pending;

    while ((message = curl_multi_info_read(curl_multi_, &pending))) {
        if (message->msg == CURLMSG_DONE) {
            CURL* easy = message->easy_handle;

            // WebSocket CONNECT_ONLY completion
            if (ws_curl_handles_.count(easy)) {
                WsConnection* conn;
                curl_easy_getinfo(easy, CURLINFO_PRIVATE, reinterpret_cast<char**>(&conn));
                ws_curl_handles_.erase(easy);
                // DO NOT curl_multi_remove_handle — handle must stay in multi per curl docs

                if (conn->handle_closed) {
                    // Build event BEFORE cleanup — game thread may free conn after push
                    auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
                        conn->plugin_context, conn->on_close, conn->userdata, true);
                    curl_multi_remove_handle(curl_multi_, easy);
                    curl_easy_cleanup(easy);
                    conn->curl_handle = nullptr;
                    conn->state = WsState::CLOSED;
                    ws_connections_.erase(conn->handle_id);
                    PushSocketEvent(evt);
                    continue;
                }

                if (message->data.result == CURLE_OK) {
                    curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, &conn->sockfd);
                    conn->uv_poll = new uv_poll_t;
                    uv_poll_init_socket(loop_, conn->uv_poll, conn->sockfd);
                    conn->uv_poll->data = conn;
                    uv_poll_start(conn->uv_poll, UV_READABLE, OnWsPollActivity);
                    conn->state = WsState::CONNECTED;
                    conn->reconnect_count = 0;

                    // Start auto-ping timer if configured
                    if (conn->ping_interval > 0)
                        WsStartPingTimer(conn);

                    PushSocketEvent(MakeSocketEvent(SocketEventType::WS_CONNECTED, conn->handle_id,
                        conn->plugin_context, conn->on_connect, conn->userdata, false));
                } else {
                    // Connection failed
                    PushSocketEvent(MakeErrorEvent(SocketEventType::WS_ERROR, conn->handle_id,
                        conn->plugin_context, conn->on_error, conn->userdata, false,
                        static_cast<int>(message->data.result),
                        curl_easy_strerror(message->data.result)));
                    curl_multi_remove_handle(curl_multi_, easy);
                    curl_easy_cleanup(easy);
                    conn->curl_handle = nullptr;

                    WsDisconnectOrReconnect(conn);
                }
                continue;
            }

            // HTTP completion
            HttpRequest* request;
            http_curl_handles_.erase(easy);
            curl_multi_remove_handle(curl_multi_, easy);
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, reinterpret_cast<char**>(&request));
            request->curlcode = message->data.result;
            stats_active.fetch_sub(1, std::memory_order_relaxed);

            // Check if we should retry
            if (request->ShouldRetry()) {
                // Capture state before PrepareForRetry clears it
                CURLcode log_code = CURLE_OK;
                long log_httpcode = 0;
                const char* log_url = nullptr;
                char log_error[CURL_ERROR_SIZE] = {};
                if (request->log_retries) {
                    log_code = request->curlcode;
                    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &log_httpcode);
                    curl_easy_getinfo(request->curl, CURLINFO_EFFECTIVE_URL, &log_url);
                    memcpy(log_error, request->curl_error_message, CURL_ERROR_SIZE);
                }

                request->PrepareForRetry();
                stats_retries.fetch_add(1, std::memory_order_relaxed);

                // Calculate delay with exponential backoff + full jitter
                int delay = static_cast<int>(request->retry_delay_ms *
                    std::pow(request->retry_backoff, request->retry_count - 1));
                if (delay > request->retry_max_delay_ms) delay = request->retry_max_delay_ms;
                if (delay < 0) delay = request->retry_max_delay_ms;  // overflow guard
                thread_local std::minstd_rand rng(std::random_device{}());
                delay = std::uniform_int_distribution<int>(0, delay)(rng);

                if (request->log_retries) {
                    if (log_code != CURLE_OK) {
                        smutils->LogMessage(myself,
                            "HTTP %d retry %d/%d: curl %d (%s) delay %dms (%s:%d %s)",
                            request->handle_id, request->retry_count, request->max_retries,
                            log_code, log_error, delay,
                            request->log_caller_file.c_str(), request->log_caller_line,
                            log_url ? log_url : "");
                    } else {
                        smutils->LogMessage(myself,
                            "HTTP %d retry %d/%d: HTTP %ld delay %dms (%s:%d %s)",
                            request->handle_id, request->retry_count, request->max_retries,
                            log_httpcode, delay,
                            request->log_caller_file.c_str(), request->log_caller_line,
                            log_url ? log_url : "");
                    }
                }

                auto* timer = new uv_timer_t;
                uv_timer_init(loop_, timer);
                timer->data = request;
                retry_timers_.insert(timer);
                uv_timer_start(timer, OnRetryTimer, delay, 0);
                continue;
            }

            // Parse response on event thread if requested
            if (request->parse_mode > 0 && request->curlcode == CURLE_OK
                && !request->response_body.empty()) {
                if (request->parse_mode == 1) {
                    request->response_node = DataParseJson(
                        request->response_body.data(), request->response_body.size());
                } else if (request->parse_mode == 2) {
                    request->response_node = MsgPackParse(
                        reinterpret_cast<const uint8_t*>(request->response_body.data()),
                        request->response_body.size());
                }
                if (request->response_node) {
                    request->response_body.clear();
                    request->response_body.shrink_to_fit();
                }
            }

            request->completed = true;
            request->OnCompleted();
            stats_completed.fetch_add(1, std::memory_order_relaxed);
            active_http_requests_.erase(request);
            done_queue.Lock();
            done_queue.Push(request);
            done_queue.Unlock();
        }
    }
}

void EventLoop::OnRetryTimer(uv_timer_t* handle) {
    HttpRequest* request = static_cast<HttpRequest*>(handle->data);
    EventLoop* self = static_cast<EventLoop*>(handle->loop->data);

    // Clean up timer
    self->retry_timers_.erase(handle);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), [](uv_handle_t* h) {
        delete reinterpret_cast<uv_timer_t*>(h);
    });

    // If already completed (cancel path handled it), skip
    if (request->completed)
        return;

    request->in_retry_wait = false;

    // If cancelled during retry wait, send to done_queue.
    // Must erase from active_http_requests_ so OnAsyncCancel (which fires
    // after timers in the same event loop iteration) won't double-push.
    if (request->handle_closed) {
        request->completed = true;
        request->OnCompleted();
        self->stats_completed.fetch_add(1, std::memory_order_relaxed);
        self->active_http_requests_.erase(request);
        self->done_queue.Lock();
        self->done_queue.Push(request);
        self->done_queue.Unlock();
        return;
    }

    // Rebuild headers from map (game thread may have modified them during retry wait)
    BuildHeaderSlist(request->headers_, request->headers_mutex_, request->built_headers_, &request->auto_headers_);
    if (request->built_headers_)
        curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->built_headers_);

    // Re-add to curl_multi
    self->stats_active.fetch_add(1, std::memory_order_relaxed);
    self->http_curl_handles_.insert(request->curl);
    curl_multi_add_handle(self->curl_multi_, request->curl);
}

// CurlSocketContext
CurlSocketContext::CurlSocketContext(curl_socket_t s, uv_loop_t* loop) {
    curl_socket = s;
    uv_poll_init_socket(loop, &uv_poll_handle, s);
    uv_poll_handle.data = this;
}

void CurlSocketContext::BeginRemoval() {
    uv_poll_stop(&uv_poll_handle);
    uv_close(reinterpret_cast<uv_handle_t*>(&uv_poll_handle), OnPollClosed);
}

void CurlSocketContext::OnPollClosed(uv_handle_t* handle) {
    delete static_cast<CurlSocketContext*>(handle->data);
}

// ---- DNS operations ----

void EventLoop::EnqueueDnsOp(DnsOp* op) {
    dns_op_queue.Lock();
    dns_op_queue.Push(op);
    dns_op_queue.Unlock();
    uv_async_send(&async_dns_op_);
}

void EventLoop::OnAsyncDnsOp(uv_async_t* handle) {
    EventLoop* self = static_cast<EventLoop*>(handle->data);
    self->dns_op_queue.Lock();
    while (!self->dns_op_queue.Empty()) {
        DnsOp* op = self->dns_op_queue.Pop();
        switch (op->type) {
            case DnsOpType::SET_TIMEOUT:
                self->dns_resolver_.Reinit(op->timeout_ms, op->tries);
                break;
        }
        delete op;
    }
    self->dns_op_queue.Unlock();
}

void EventLoop::DnsCacheFlush() {
    dns_resolver_.FlushCache();
}

void EventLoop::DnsCacheStats(int& count, int& memory) {
    dns_resolver_.GetCacheStats(count, memory);
}

void EventLoop::DnsCacheSetTtl(int seconds) {
    dns_resolver_.SetCacheTtl(seconds);
}

// ---- TCP/UDP support ----

void EventLoop::EnqueueTcpOp(TcpOp* op) {
    tcp_op_queue.Lock();
    tcp_op_queue.Push(op);
    tcp_op_queue.Unlock();
    uv_async_send(&async_tcp_op_);
}

void EventLoop::EnqueueUdpOp(UdpOp* op) {
    udp_op_queue.Lock();
    udp_op_queue.Push(op);
    udp_op_queue.Unlock();
    uv_async_send(&async_udp_op_);
}

void EventLoop::DrainSocketEvents(std::vector<SocketEvent*>& out) {
    socket_done_queue.Lock();
    while (!socket_done_queue.Empty()) {
        out.push_back(socket_done_queue.Pop());
    }
    socket_done_queue.Unlock();
}

void EventLoop::PushSocketEvent(SocketEvent* evt) {
    socket_done_queue.Lock();
    socket_done_queue.Push(evt);
    socket_done_queue.Unlock();
}

// Helper: get peer address string from uv_tcp_t
static std::string GetPeerAddr(uv_tcp_t* handle, int* port) {
    struct sockaddr_storage addr;
    int namelen = sizeof(addr);
    if (uv_tcp_getpeername(handle, reinterpret_cast<struct sockaddr*>(&addr), &namelen) != 0) {
        *port = 0;
        return "";
    }
    char ip[INET6_ADDRSTRLEN];
    if (addr.ss_family == AF_INET) {
        auto* sin = reinterpret_cast<struct sockaddr_in*>(&addr);
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        *port = ntohs(sin->sin_port);
    } else {
        auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(&addr);
        inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
        *port = ntohs(sin6->sin6_port);
    }
    return ip;
}

static void CloseTcpHandle(TcpSocket* sock) {
    if (!sock->uv_handle) return;
    uv_tcp_t* h = sock->uv_handle;
    sock->uv_handle = nullptr;  // Prevent other ops from using a closing handle
    uv_close(reinterpret_cast<uv_handle_t*>(h), [](uv_handle_t* handle) {
        delete reinterpret_cast<uv_tcp_t*>(handle);
    });
}

static void ApplyTcpOptions(TcpSocket* sock) {
    if (!sock->uv_handle) return;
    if (sock->nodelay)
        uv_tcp_nodelay(sock->uv_handle, 1);
    if (sock->keepalive)
        uv_tcp_keepalive(sock->uv_handle, 1, sock->keepalive_delay);
    if (sock->send_bufsize > 0) {
        int val = sock->send_bufsize;
        uv_send_buffer_size(reinterpret_cast<uv_handle_t*>(sock->uv_handle), &val);
    }
    if (sock->recv_bufsize > 0) {
        int val = sock->recv_bufsize;
        uv_recv_buffer_size(reinterpret_cast<uv_handle_t*>(sock->uv_handle), &val);
    }
}

// ---- TCP callbacks ----

void EventLoop::OnAsyncTcpOp(uv_async_t* handle) {
    EventLoop* self = static_cast<EventLoop*>(handle->data);
    self->tcp_op_queue.Lock();
    while (!self->tcp_op_queue.Empty()) {
        TcpOp* op = self->tcp_op_queue.Pop();
        switch (op->type) {
            case TcpOpType::CONNECT:    self->ProcessTcpConnect(op); break;
            case TcpOpType::SEND:       self->ProcessTcpSend(op); break;
            case TcpOpType::CLOSE:      self->ProcessTcpClose(op); break;
            case TcpOpType::LISTEN:     self->ProcessTcpListen(op); break;
            case TcpOpType::BIND:       self->ProcessTcpBind(op); break;
            case TcpOpType::SET_OPTION: self->ProcessTcpSetOption(op); break;
            case TcpOpType::ACCEPT_CONFIG: self->ProcessTcpAcceptConfig(op); break;
        }
        delete op;
    }
    self->tcp_op_queue.Unlock();
}

void EventLoop::ProcessTcpConnect(TcpOp* op) {
    if (op->socket_ptr)
        tcp_sockets_.emplace(op->handle_id, op->socket_ptr);

    auto it = tcp_sockets_.find(op->handle_id);
    if (it == tcp_sockets_.end()) return;
    TcpSocket* sock = it->second;
    if (sock->handle_closed) return;

    sock->state = TcpState::RESOLVING;

    int handle_id = op->handle_id;
    dns_resolver_.Resolve(op->host, op->port, [this, handle_id](int status, struct sockaddr* addr, socklen_t addrlen) {
        auto it2 = tcp_sockets_.find(handle_id);
        if (it2 == tcp_sockets_.end()) return;
        TcpSocket* sock2 = it2->second;
        if (sock2->handle_closed) return;

        if (status != 0) {
            PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata,
                sock2->handle_closed.load(), status, ares_strerror(status)));
            return;
        }

        // Init handle if not already created (e.g., by a prior Bind call)
        if (!sock2->uv_handle) {
            sock2->uv_handle = new uv_tcp_t;
            uv_tcp_init(loop_, sock2->uv_handle);
            sock2->uv_handle->data = sock2;
            ApplyTcpOptions(sock2);
        }

        auto* conn_req = new uv_connect_t;
        conn_req->data = sock2;
        sock2->state = TcpState::CONNECTING;

        int rc = uv_tcp_connect(conn_req, sock2->uv_handle, addr, OnTcpConnected);
        if (rc != 0) {
            delete conn_req;
            PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata,
                sock2->handle_closed.load(), rc, uv_strerror(rc)));
        }
    });
}

void EventLoop::OnTcpConnected(uv_connect_t* req, int status) {
    TcpSocket* sock = static_cast<TcpSocket*>(req->data);
    EventLoop* self = static_cast<EventLoop*>(sock->uv_handle->loop->data);
    delete req;

    if (sock->handle_closed) return;

    if (status != 0) {
        self->PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, sock->handle_id,
            sock->plugin_context, sock->on_error, sock->userdata, false, status, uv_strerror(status)));
        return;
    }

    sock->state = TcpState::CONNECTED;
    uv_read_start(reinterpret_cast<uv_stream_t*>(sock->uv_handle), OnTcpAllocBuffer, OnTcpRead);

    self->PushSocketEvent(MakeSocketEvent(SocketEventType::TCP_CONNECTED, sock->handle_id,
        sock->plugin_context, sock->on_connect, sock->userdata, false));
}

void EventLoop::OnTcpAllocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    TcpSocket* sock = static_cast<TcpSocket*>(handle->data);
    size_t alloc_size = sock->max_chunk_size > 0 ? static_cast<size_t>(sock->max_chunk_size) : 4096;
    buf->base = new char[alloc_size];
    buf->len = alloc_size;
}

void EventLoop::OnTcpRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TcpSocket* sock = static_cast<TcpSocket*>(stream->data);
    EventLoop* self = static_cast<EventLoop*>(stream->loop->data);

    if (nread > 0) {
        // Split into chunks of max_chunk_size
        const uint8_t* data = reinterpret_cast<const uint8_t*>(buf->base);
        size_t remaining = static_cast<size_t>(nread);
        size_t chunk_size = sock->max_chunk_size > 0 ? static_cast<size_t>(sock->max_chunk_size) : 4096;

        while (remaining > 0 && !sock->handle_closed) {
            size_t this_chunk = remaining < chunk_size ? remaining : chunk_size;
            auto* evt = MakeSocketEvent(SocketEventType::TCP_DATA, sock->handle_id,
                sock->plugin_context, sock->on_data, sock->userdata, false);
            evt->data.assign(data, data + this_chunk);
            self->PushSocketEvent(evt);
            data += this_chunk;
            remaining -= this_chunk;
        }
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            // Connection closed by peer — trigger close
            uv_read_stop(stream);
            sock->state = TcpState::CLOSING;
            uv_close(reinterpret_cast<uv_handle_t*>(sock->uv_handle), OnTcpClosed);
        } else {
            self->PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, sock->handle_id,
                sock->plugin_context, sock->on_error, sock->userdata, sock->handle_closed.load(),
                static_cast<int>(nread), uv_strerror(static_cast<int>(nread))));
        }
    }

    delete[] buf->base;
}

void EventLoop::ProcessTcpSend(TcpOp* op) {
    auto it = tcp_sockets_.find(op->handle_id);
    if (it == tcp_sockets_.end()) return;
    TcpSocket* sock = it->second;
    if (sock->handle_closed || sock->state != TcpState::CONNECTED || !sock->uv_handle) return;

    auto* wreq = new TcpWriteReq;
    wreq->data = new uint8_t[op->data.size()];
    memcpy(wreq->data, op->data.data(), op->data.size());
    wreq->buf = uv_buf_init(reinterpret_cast<char*>(wreq->data), static_cast<unsigned int>(op->data.size()));
    wreq->req.data = sock;

    int rc = uv_write(&wreq->req, reinterpret_cast<uv_stream_t*>(sock->uv_handle), &wreq->buf, 1, OnTcpWriteDone);
    if (rc != 0) {
        delete[] wreq->data;
        delete wreq;
        PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, sock->handle_id,
            sock->plugin_context, sock->on_error, sock->userdata, sock->handle_closed.load(),
            rc, uv_strerror(rc)));
    }
}

void EventLoop::OnTcpWriteDone(uv_write_t* req, int status) {
    TcpWriteReq* wreq = reinterpret_cast<TcpWriteReq*>(req);
    TcpSocket* sock = static_cast<TcpSocket*>(req->data);
    EventLoop* self = static_cast<EventLoop*>(sock->uv_handle->loop->data);

    if (status != 0 && !sock->handle_closed) {
        self->PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, sock->handle_id,
            sock->plugin_context, sock->on_error, sock->userdata, false,
            status, uv_strerror(status)));
    }

    delete[] wreq->data;
    delete wreq;
}

void EventLoop::ProcessTcpClose(TcpOp* op) {
    auto it = tcp_sockets_.find(op->handle_id);
    if (it == tcp_sockets_.end()) return;
    TcpSocket* sock = it->second;
    if (sock->state == TcpState::CLOSING || sock->state == TcpState::CLOSED) return;

    sock->state = TcpState::CLOSING;
    if (sock->uv_handle) {
        uv_read_stop(reinterpret_cast<uv_stream_t*>(sock->uv_handle));
        uv_close(reinterpret_cast<uv_handle_t*>(sock->uv_handle), OnTcpClosed);
    } else {
        // Never connected, just push closed event
        sock->state = TcpState::CLOSED;
        PushSocketEvent(MakeSocketEvent(SocketEventType::TCP_CLOSED, sock->handle_id,
            sock->plugin_context, sock->on_close, sock->userdata, sock->handle_closed.load()));
        tcp_sockets_.erase(op->handle_id);
    }
}

void EventLoop::OnTcpClosed(uv_handle_t* handle) {
    TcpSocket* sock = static_cast<TcpSocket*>(handle->data);
    EventLoop* self = static_cast<EventLoop*>(handle->loop->data);

    sock->state = TcpState::CLOSED;
    delete sock->uv_handle;
    sock->uv_handle = nullptr;

    self->PushSocketEvent(MakeSocketEvent(SocketEventType::TCP_CLOSED, sock->handle_id,
        sock->plugin_context, sock->on_close, sock->userdata, sock->handle_closed.load()));
    self->tcp_sockets_.erase(sock->handle_id);
}

void EventLoop::ProcessTcpListen(TcpOp* op) {
    if (op->socket_ptr)
        tcp_sockets_.emplace(op->handle_id, op->socket_ptr);

    auto it = tcp_sockets_.find(op->handle_id);
    if (it == tcp_sockets_.end()) return;
    TcpSocket* sock = it->second;
    if (sock->handle_closed) return;

    sock->on_accept = op->on_accept;

    int handle_id = op->handle_id;
    int backlog = op->backlog;
    dns_resolver_.Resolve(op->host, op->port, [this, handle_id, backlog](int status, struct sockaddr* addr, socklen_t addrlen) {
        auto it2 = tcp_sockets_.find(handle_id);
        if (it2 == tcp_sockets_.end()) return;
        TcpSocket* sock2 = it2->second;
        if (sock2->handle_closed) return;

        if (status != 0) {
            PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false,
                status, ares_strerror(status)));
            return;
        }

        sock2->uv_handle = new uv_tcp_t;
        uv_tcp_init(loop_, sock2->uv_handle);
        sock2->uv_handle->data = sock2;
        ApplyTcpOptions(sock2);

        int rc = uv_tcp_bind(sock2->uv_handle, addr, 0);
        if (rc != 0) {
            CloseTcpHandle(sock2);
            PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false, rc, uv_strerror(rc)));
            return;
        }

        rc = uv_listen(reinterpret_cast<uv_stream_t*>(sock2->uv_handle), backlog, OnTcpNewConnection);
        if (rc != 0) {
            CloseTcpHandle(sock2);
            PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false, rc, uv_strerror(rc)));
            return;
        }

        sock2->state = TcpState::LISTENING;
    });
}

void EventLoop::OnTcpNewConnection(uv_stream_t* server, int status) {
    TcpSocket* listener = static_cast<TcpSocket*>(server->data);
    EventLoop* self = static_cast<EventLoop*>(server->loop->data);

    if (status != 0 || listener->handle_closed) return;

    // Create accepted socket
    auto* client = new TcpSocket(listener->plugin_context, listener->userdata);
    client->on_data = listener->on_data;
    client->on_error = listener->on_error;
    client->on_close = listener->on_close;
    client->max_chunk_size = listener->max_chunk_size;
    client->state = TcpState::CONNECTED;

    client->uv_handle = new uv_tcp_t;
    uv_tcp_init(self->loop_, client->uv_handle);
    client->uv_handle->data = client;

    int rc = uv_accept(server, reinterpret_cast<uv_stream_t*>(client->uv_handle));
    if (rc != 0) {
        uv_close(reinterpret_cast<uv_handle_t*>(client->uv_handle), [](uv_handle_t* h) {
            auto* sock = static_cast<TcpSocket*>(h->data);
            delete sock->uv_handle;
            sock->uv_handle = nullptr;
            delete sock;
        });
        return;
    }

    int peer_port = 0;
    std::string peer_addr = GetPeerAddr(client->uv_handle, &peer_port);

    // Don't start reading yet — the game thread must assign a handle_id first
    // (via ACCEPT_CONFIG), otherwise OnTcpRead would push events with handle_id=0.

    auto* evt = MakeSocketEvent(SocketEventType::TCP_ACCEPTED, listener->handle_id,
        listener->plugin_context, listener->on_accept, listener->userdata, false);
    evt->remote_addr = peer_addr;
    evt->remote_port = peer_port;
    evt->accepted_handle_id = reinterpret_cast<intptr_t>(client);
    self->PushSocketEvent(evt);
}

void EventLoop::ProcessTcpBind(TcpOp* op) {
    if (op->socket_ptr)
        tcp_sockets_.emplace(op->handle_id, op->socket_ptr);

    auto it = tcp_sockets_.find(op->handle_id);
    if (it == tcp_sockets_.end()) return;
    TcpSocket* sock = it->second;
    if (sock->handle_closed) return;

    int handle_id = op->handle_id;
    dns_resolver_.Resolve(op->host, op->port, [this, handle_id](int status, struct sockaddr* addr, socklen_t addrlen) {
        auto it2 = tcp_sockets_.find(handle_id);
        if (it2 == tcp_sockets_.end()) return;
        TcpSocket* sock2 = it2->second;
        if (sock2->handle_closed) return;

        if (status != 0) {
            PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false,
                status, ares_strerror(status)));
            return;
        }

        // Init handle if not already created (bind before connect)
        if (!sock2->uv_handle) {
            sock2->uv_handle = new uv_tcp_t;
            uv_tcp_init(loop_, sock2->uv_handle);
            sock2->uv_handle->data = sock2;
            ApplyTcpOptions(sock2);
        }

        int rc = uv_tcp_bind(sock2->uv_handle, addr, 0);
        if (rc != 0) {
            CloseTcpHandle(sock2);
            PushSocketEvent(MakeErrorEvent(SocketEventType::TCP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false, rc, uv_strerror(rc)));
        }
    });
}

void EventLoop::ProcessTcpSetOption(TcpOp* op) {
    auto it = tcp_sockets_.find(op->handle_id);
    if (it == tcp_sockets_.end()) return;
    TcpSocket* sock = it->second;
    if (!sock->uv_handle) return;

    auto* h = reinterpret_cast<uv_handle_t*>(sock->uv_handle);
    switch (op->option_id) {
        case 4: { // TCP_SEND_BUFSIZE
            int val = op->option_value;
            uv_send_buffer_size(h, &val);
            break;
        }
        case 5: { // TCP_RECV_BUFSIZE
            int val = op->option_value;
            uv_recv_buffer_size(h, &val);
            break;
        }
        default:
            break;
    }
}

void EventLoop::ProcessTcpAcceptConfig(TcpOp* op) {
    if (op->socket_ptr)
        tcp_sockets_.emplace(op->handle_id, op->socket_ptr);

    auto it = tcp_sockets_.find(op->handle_id);
    if (it == tcp_sockets_.end()) return;
    TcpSocket* sock = it->second;
    sock->on_connect = op->on_connect;
    sock->on_data = op->on_data;
    sock->on_error = op->on_error;
    sock->on_close = op->on_close;

    // Start reading now that handle_id is assigned
    if (sock->uv_handle && !sock->handle_closed) {
        uv_read_start(reinterpret_cast<uv_stream_t*>(sock->uv_handle), OnTcpAllocBuffer, OnTcpRead);
    }
}

// ---- UDP support ----

void EventLoop::OnAsyncUdpOp(uv_async_t* handle) {
    EventLoop* self = static_cast<EventLoop*>(handle->data);
    self->udp_op_queue.Lock();
    while (!self->udp_op_queue.Empty()) {
        UdpOp* op = self->udp_op_queue.Pop();
        switch (op->type) {
            case UdpOpType::BIND:       self->ProcessUdpBind(op); break;
            case UdpOpType::SEND:       self->ProcessUdpSend(op); break;
            case UdpOpType::CLOSE:      self->ProcessUdpClose(op); break;
            case UdpOpType::SET_OPTION: self->ProcessUdpSetOption(op); break;
        }
        delete op;
    }
    self->udp_op_queue.Unlock();
}

static void ApplyUdpOptions(UdpSocket* sock) {
    if (!sock->uv_handle) return;
    if (sock->broadcast)
        uv_udp_set_broadcast(sock->uv_handle, 1);
    if (sock->ttl > 0)
        uv_udp_set_ttl(sock->uv_handle, sock->ttl);
    if (sock->send_bufsize > 0) {
        int val = sock->send_bufsize;
        uv_send_buffer_size(reinterpret_cast<uv_handle_t*>(sock->uv_handle), &val);
    }
    if (sock->recv_bufsize > 0) {
        int val = sock->recv_bufsize;
        uv_recv_buffer_size(reinterpret_cast<uv_handle_t*>(sock->uv_handle), &val);
    }
}

void EventLoop::ProcessUdpBind(UdpOp* op) {
    if (op->socket_ptr)
        udp_sockets_.emplace(op->handle_id, op->socket_ptr);

    auto it = udp_sockets_.find(op->handle_id);
    if (it == udp_sockets_.end()) return;
    UdpSocket* sock = it->second;
    if (sock->handle_closed) return;

    int handle_id = op->handle_id;
    dns_resolver_.Resolve(op->addr, op->port, [this, handle_id](int status, struct sockaddr* addr, socklen_t addrlen) {
        auto it2 = udp_sockets_.find(handle_id);
        if (it2 == udp_sockets_.end()) return;
        UdpSocket* sock2 = it2->second;
        if (sock2->handle_closed) return;

        if (status != 0) {
            PushSocketEvent(MakeErrorEvent(SocketEventType::UDP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false,
                status, ares_strerror(status)));
            return;
        }

        sock2->uv_handle = new uv_udp_t;
        uv_udp_init(loop_, sock2->uv_handle);
        sock2->uv_handle->data = sock2;
        ApplyUdpOptions(sock2);

        int rc = uv_udp_bind(sock2->uv_handle, addr, 0);
        if (rc != 0) {
            PushSocketEvent(MakeErrorEvent(SocketEventType::UDP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false, rc, uv_strerror(rc)));
            return;
        }

        rc = uv_udp_recv_start(sock2->uv_handle, OnUdpAllocBuffer, OnUdpRead);
        if (rc != 0) {
            PushSocketEvent(MakeErrorEvent(SocketEventType::UDP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false, rc, uv_strerror(rc)));
            return;
        }

        sock2->state = UdpState::BOUND;
    });
}

void EventLoop::ProcessUdpSetOption(UdpOp* op) {
    auto it = udp_sockets_.find(op->handle_id);
    if (it == udp_sockets_.end()) return;
    UdpSocket* sock = it->second;
    if (!sock->uv_handle) return;

    auto* h = reinterpret_cast<uv_handle_t*>(sock->uv_handle);
    switch (op->option_id) {
        case 2: { // UDP_SEND_BUFSIZE
            int val = op->option_value;
            uv_send_buffer_size(h, &val);
            break;
        }
        case 3: { // UDP_RECV_BUFSIZE
            int val = op->option_value;
            uv_recv_buffer_size(h, &val);
            break;
        }
        default:
            break;
    }
}

struct UdpSendReq {
    uv_udp_send_t req;
    uint8_t* data;
    uv_buf_t buf;
};

void EventLoop::ProcessUdpSend(UdpOp* op) {
    auto it = udp_sockets_.find(op->handle_id);
    if (it == udp_sockets_.end()) return;
    UdpSocket* sock = it->second;
    if (sock->handle_closed || !sock->uv_handle) return;

    int handle_id = op->handle_id;
    std::vector<uint8_t> send_data = std::move(op->data);

    dns_resolver_.Resolve(op->addr, op->port,
        [this, handle_id, send_data = std::move(send_data)](int status, struct sockaddr* addr, socklen_t addrlen) {
        auto it2 = udp_sockets_.find(handle_id);
        if (it2 == udp_sockets_.end()) return;
        UdpSocket* sock2 = it2->second;
        if (sock2->handle_closed || !sock2->uv_handle) return;

        if (status != 0) {
            PushSocketEvent(MakeErrorEvent(SocketEventType::UDP_ERROR, handle_id,
                sock2->plugin_context, sock2->on_error, sock2->userdata, false,
                status, ares_strerror(status)));
            return;
        }

        auto* sreq = new UdpSendReq;
        sreq->data = new uint8_t[send_data.size()];
        memcpy(sreq->data, send_data.data(), send_data.size());
        sreq->buf = uv_buf_init(reinterpret_cast<char*>(sreq->data), static_cast<unsigned int>(send_data.size()));
        sreq->req.data = sock2;

        int rc = uv_udp_send(&sreq->req, sock2->uv_handle, &sreq->buf, 1, addr, OnUdpSendDone);
        if (rc != 0) {
            delete[] sreq->data;
            delete sreq;
        }
    });
}

void EventLoop::ProcessUdpClose(UdpOp* op) {
    auto it = udp_sockets_.find(op->handle_id);
    if (it == udp_sockets_.end()) return;
    UdpSocket* sock = it->second;
    if (sock->state == UdpState::CLOSING || sock->state == UdpState::CLOSED) return;

    sock->state = UdpState::CLOSING;
    if (sock->uv_handle) {
        uv_udp_recv_stop(sock->uv_handle);
        uv_close(reinterpret_cast<uv_handle_t*>(sock->uv_handle), OnUdpClosed);
    } else {
        sock->state = UdpState::CLOSED;
        PushSocketEvent(MakeSocketEvent(SocketEventType::UDP_CLOSED, sock->handle_id,
            sock->plugin_context, sock->on_close, sock->userdata, sock->handle_closed.load()));
        udp_sockets_.erase(op->handle_id);
    }
}

void EventLoop::OnUdpAllocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = new char[65536]; // max UDP datagram
    buf->len = 65536;
}

void EventLoop::OnUdpRead(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                          const struct sockaddr* addr, unsigned flags) {
    UdpSocket* sock = static_cast<UdpSocket*>(handle->data);
    EventLoop* self = static_cast<EventLoop*>(handle->loop->data);

    if (nread > 0 && addr && !sock->handle_closed) {
        char ip[INET6_ADDRSTRLEN];
        int port = 0;
        if (addr->sa_family == AF_INET) {
            auto* sin = reinterpret_cast<const struct sockaddr_in*>(addr);
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            port = ntohs(sin->sin_port);
        } else {
            auto* sin6 = reinterpret_cast<const struct sockaddr_in6*>(addr);
            inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
            port = ntohs(sin6->sin6_port);
        }

        auto* evt = MakeSocketEvent(SocketEventType::UDP_DATA, sock->handle_id,
            sock->plugin_context, sock->on_data, sock->userdata, false);
        evt->data.assign(reinterpret_cast<const uint8_t*>(buf->base),
                         reinterpret_cast<const uint8_t*>(buf->base) + nread);
        evt->remote_addr = ip;
        evt->remote_port = port;
        self->PushSocketEvent(evt);
    } else if (nread < 0) {
        self->PushSocketEvent(MakeErrorEvent(SocketEventType::UDP_ERROR, sock->handle_id,
            sock->plugin_context, sock->on_error, sock->userdata, sock->handle_closed.load(),
            static_cast<int>(nread), uv_strerror(static_cast<int>(nread))));
    }

    delete[] buf->base;
}

void EventLoop::OnUdpSendDone(uv_udp_send_t* req, int status) {
    UdpSendReq* sreq = reinterpret_cast<UdpSendReq*>(req);
    delete[] sreq->data;
    delete sreq;
}

void EventLoop::OnUdpClosed(uv_handle_t* handle) {
    UdpSocket* sock = static_cast<UdpSocket*>(handle->data);
    EventLoop* self = static_cast<EventLoop*>(handle->loop->data);

    sock->state = UdpState::CLOSED;
    delete sock->uv_handle;
    sock->uv_handle = nullptr;

    self->PushSocketEvent(MakeSocketEvent(SocketEventType::UDP_CLOSED, sock->handle_id,
        sock->plugin_context, sock->on_close, sock->userdata, sock->handle_closed.load()));
    self->udp_sockets_.erase(sock->handle_id);
}

// ---- WebSocket support ----

void EventLoop::EnqueueWsOp(WsOp* op) {
    ws_op_queue.Lock();
    ws_op_queue.Push(op);
    ws_op_queue.Unlock();
    uv_async_send(&async_ws_op_);
}

void EventLoop::OnAsyncWsOp(uv_async_t* handle) {
    EventLoop* self = static_cast<EventLoop*>(handle->data);
    self->ws_op_queue.Lock();
    while (!self->ws_op_queue.Empty()) {
        WsOp* op = self->ws_op_queue.Pop();
        switch (op->type) {
            case WsOpType::CONNECT:     self->ProcessWsConnect(op); break;
            case WsOpType::SEND_TEXT:
            case WsOpType::SEND_BINARY:
            case WsOpType::SEND_PING:
            case WsOpType::SEND_JSON:
            case WsOpType::SEND_MSGPACK: self->ProcessWsSend(op); break;
            case WsOpType::CLOSE:       self->ProcessWsClose(op); break;
            case WsOpType::SET_OPTION:  self->ProcessWsSetOption(op); break;
        }
        delete op;
    }
    self->ws_op_queue.Unlock();
}

void EventLoop::ProcessWsConnect(WsOp* op) {
    WsConnection* conn = op->conn_ptr;
    ws_connections_.emplace(op->handle_id, conn);
    conn->url_ = std::move(op->url);

    if (conn->handle_closed) return;

    if (!WsInitCurl(conn)) {
        PushSocketEvent(MakeErrorEvent(SocketEventType::WS_ERROR, conn->handle_id,
            conn->plugin_context, conn->on_error, conn->userdata, false,
            -1, "curl_easy_init failed"));
        ws_connections_.erase(op->handle_id);
    }
}

void EventLoop::ProcessWsSend(WsOp* op) {
    auto it = ws_connections_.find(op->handle_id);
    if (it == ws_connections_.end()) return;
    WsConnection* conn = it->second;
    if (conn->handle_closed || !conn->curl_handle || conn->state != WsState::CONNECTED) return;

    // Serialize body_node on event thread (SEND_JSON / SEND_MSGPACK)
    if (op->body_node) {
        if (op->type == WsOpType::SEND_JSON) {
            std::string json = DataSerializeJson(*op->body_node, false);
            op->data.assign(json.begin(), json.end());
        } else if (op->type == WsOpType::SEND_MSGPACK) {
            auto buf = MsgPackSerialize(*op->body_node);
            op->data = std::move(buf);
        }
        DataNode::Decref(op->body_node);
        op->body_node = nullptr;
    }

    unsigned int flags;
    switch (op->type) {
        case WsOpType::SEND_TEXT:    flags = CURLWS_TEXT; break;
        case WsOpType::SEND_BINARY:  flags = CURLWS_BINARY; break;
        case WsOpType::SEND_PING:    flags = CURLWS_PING; break;
        case WsOpType::SEND_JSON:    flags = CURLWS_TEXT; break;
        case WsOpType::SEND_MSGPACK: flags = CURLWS_BINARY; break;
        default: return;
    }

    size_t sent;
    CURLcode rc = curl_ws_send(conn->curl_handle, op->data.data(),
                                op->data.size(), &sent, 0, flags);
    if (rc != CURLE_OK && rc != CURLE_AGAIN) {
        PushSocketEvent(MakeErrorEvent(SocketEventType::WS_ERROR, conn->handle_id,
            conn->plugin_context, conn->on_error, conn->userdata, false,
            static_cast<int>(rc), curl_easy_strerror(rc)));
    }
}

void EventLoop::ProcessWsClose(WsOp* op) {
    auto it = ws_connections_.find(op->handle_id);
    if (it == ws_connections_.end()) return;
    WsConnection* conn = it->second;

    if (conn->state == WsState::CLOSING || conn->state == WsState::CLOSED) return;

    // Cancel pending reconnect
    if (conn->state == WsState::RECONNECTING) {
        auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
            conn->plugin_context, conn->on_close, conn->userdata, conn->handle_closed.load());
        WsStopTimers(conn);
        conn->state = WsState::CLOSED;
        ws_connections_.erase(conn->handle_id);
        PushSocketEvent(evt);
        return;
    }

    if (conn->curl_handle && conn->uv_poll && conn->state == WsState::CONNECTED && !conn->close_sent_) {
        conn->state = WsState::CLOSING;

        // Build close payload: 2 bytes code + reason
        uint8_t close_data[2 + 123];
        close_data[0] = (op->close_code >> 8) & 0xFF;
        close_data[1] = op->close_code & 0xFF;
        size_t reason_len = op->close_reason.size();
        if (reason_len > 123) reason_len = 123;
        if (reason_len > 0)
            memcpy(close_data + 2, op->close_reason.data(), reason_len);
        size_t len = 2 + reason_len;

        size_t sent;
        curl_ws_send(conn->curl_handle, close_data, len, &sent, 0, CURLWS_CLOSE);
        conn->close_sent_ = true;

        // Start close timeout timer
        conn->close_timer_ = new uv_timer_t;
        uv_timer_init(loop_, conn->close_timer_);
        conn->close_timer_->data = conn;
        uv_timer_start(conn->close_timer_, OnWsCloseTimeout,
                        conn->close_timeout * 1000, 0);
    } else {
        // Not connected yet or no poll handle — just clean up
        auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
            conn->plugin_context, conn->on_close, conn->userdata, conn->handle_closed.load());
        WsCleanup(conn);
        PushSocketEvent(evt);
    }
}

void EventLoop::ProcessWsSetOption(WsOp* op) {
    auto it = ws_connections_.find(op->handle_id);
    if (it == ws_connections_.end()) return;
    WsConnection* conn = it->second;

    switch (op->option_id) {
        case 0: // WS_MAX_MESSAGE_SIZE
            conn->max_message_size = op->option_value > 0 ? op->option_value : 16 * 1024 * 1024;
            break;
        case 1: // WS_AUTO_PONG
            conn->auto_pong = (op->option_value != 0);
            break;
        case 2: // WS_PING_INTERVAL
            conn->ping_interval = op->option_value > 0 ? op->option_value : 0;
            // If already connected, restart or stop ping timer
            if (conn->state == WsState::CONNECTED) {
                if (conn->ping_timer_) {
                    uv_timer_stop(conn->ping_timer_);
                    uv_close(reinterpret_cast<uv_handle_t*>(conn->ping_timer_), [](uv_handle_t* h) {
                        delete reinterpret_cast<uv_timer_t*>(h);
                    });
                    conn->ping_timer_ = nullptr;
                }
                if (conn->ping_interval > 0)
                    WsStartPingTimer(conn);
            }
            break;
        case 3: // WS_CLOSE_TIMEOUT
            conn->close_timeout = op->option_value > 0 ? op->option_value : 5;
            break;
        default:
            break;
    }
}

void EventLoop::OnWsPollActivity(uv_poll_t* handle, int status, int events) {
    WsConnection* conn = static_cast<WsConnection*>(handle->data);
    EventLoop* self = static_cast<EventLoop*>(handle->loop->data);
    if (conn->handle_closed) {
        // Build event BEFORE cleanup — WsCleanup erases conn from ws_connections_,
        // and after PushSocketEvent the game thread may free conn immediately.
        auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
            conn->plugin_context, conn->on_close, conn->userdata, true);
        self->WsCleanup(conn);
        self->PushSocketEvent(evt);
        return;
    }

    if (status < 0) {
        self->PushSocketEvent(MakeErrorEvent(SocketEventType::WS_ERROR, conn->handle_id,
            conn->plugin_context, conn->on_error, conn->userdata, false,
            status, uv_strerror(status)));
        self->WsDisconnectOrReconnect(conn);
        return;
    }

    uint8_t buf[65536];
    while (true) {
        size_t nread;
        const struct curl_ws_frame* meta;
        CURLcode rc = curl_ws_recv(conn->curl_handle, buf, sizeof(buf), &nread, &meta);

        if (rc == CURLE_AGAIN) break;  // no more data

        if (rc == CURLE_GOT_NOTHING) {
            self->WsDisconnectOrReconnect(conn);
            return;
        }
        if (rc != CURLE_OK) {
            self->PushSocketEvent(MakeErrorEvent(SocketEventType::WS_ERROR, conn->handle_id,
                conn->plugin_context, conn->on_error, conn->userdata, false,
                static_cast<int>(rc), curl_easy_strerror(rc)));
            self->WsDisconnectOrReconnect(conn);
            return;
        }
        if (!meta) continue;  // safety: skip if no frame metadata

        // CLOSE frame
        if (meta->flags & CURLWS_CLOSE) {
            int close_code = 1005;  // no code present
            std::string close_reason;
            if (nread >= 2) {
                close_code = (buf[0] << 8) | buf[1];
                if (nread > 2)
                    close_reason.assign(reinterpret_cast<char*>(buf + 2), nread - 2);
            }

            // If we haven't sent close, send close reply
            if (!conn->close_sent_) {
                size_t sent;
                // Echo back the close code
                uint8_t reply[2];
                reply[0] = buf[0];
                reply[1] = (nread >= 2) ? buf[1] : 0;
                curl_ws_send(conn->curl_handle, reply, nread >= 2 ? 2 : 0, &sent, 0, CURLWS_CLOSE);
                conn->close_sent_ = true;
            }

            self->WsDisconnectOrReconnect(conn, close_code, close_reason);
            return;
        }

        // PING frame
        if (meta->flags & CURLWS_PING) {
            // curl handles auto-pong unless CURLWS_RAW_MODE was set
            continue;
        }

        // PONG frame
        if (meta->flags & CURLWS_PONG) {
            if (conn->on_pong) {
                auto* evt = MakeSocketEvent(SocketEventType::WS_PONG, conn->handle_id,
                    conn->plugin_context, conn->on_pong, conn->userdata, false);
                evt->data.assign(buf, buf + nread);
                self->PushSocketEvent(evt);
            }
            continue;
        }

        if (conn->handle_closed) break;  // stop processing if closed mid-loop

        // Data frame (TEXT or BINARY)
        // Track binary/text from first frame
        if (!conn->in_fragmented_message_) {
            conn->is_binary_message_ = (meta->flags & CURLWS_BINARY) != 0;
            conn->message_buf_.clear();
        }

        // Check max message size before accumulating
        if (conn->message_buf_.size() + nread > static_cast<size_t>(conn->max_message_size)) {
            // Close with 1009 (Message Too Big)
            self->PushSocketEvent(MakeErrorEvent(SocketEventType::WS_ERROR, conn->handle_id,
                conn->plugin_context, conn->on_error, conn->userdata, false,
                1009, "Message exceeds max_message_size"));
            // Send close frame 1009
            uint8_t close_data[2] = {0x03, 0xF1};  // 1009
            size_t sent;
            curl_ws_send(conn->curl_handle, close_data, 2, &sent, 0, CURLWS_CLOSE);
            conn->close_sent_ = true;
            auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
                conn->plugin_context, conn->on_close, conn->userdata, conn->handle_closed.load());
            self->WsCleanup(conn);
            self->PushSocketEvent(evt);
            return;
        }

        // Accumulate data
        conn->message_buf_.insert(conn->message_buf_.end(), buf, buf + nread);

        // Check if message is complete
        bool is_continuation = (meta->flags & CURLWS_CONT) != 0;
        if (is_continuation || meta->bytesleft > 0) {
            conn->in_fragmented_message_ = true;
            continue;  // more data coming
        }

        // Message complete
        conn->in_fragmented_message_ = false;
        auto* evt = MakeSocketEvent(SocketEventType::WS_MESSAGE, conn->handle_id,
            conn->plugin_context, conn->on_message, conn->userdata, false);
        evt->is_binary = conn->is_binary_message_;
        evt->parse_mode = conn->parse_messages;
        evt->error_callback = conn->on_error;

        if (conn->parse_messages > 0) {
            if (conn->is_binary_message_) {
                // Binary frame: wrap as Binary DataNode (move, no copy)
                evt->parsed_node = DataNode::MakeBinary(std::move(conn->message_buf_));
            } else if (conn->parse_messages == 1) {
                // JSON text parse
                evt->parsed_node = DataParseJson(
                    reinterpret_cast<const char*>(conn->message_buf_.data()),
                    conn->message_buf_.size());
                if (!evt->parsed_node) {
                    // Parse failed — keep raw for game-thread fallback
                    evt->data = std::move(conn->message_buf_);
                }
            } else if (conn->parse_messages == 2) {
                // MsgPack parse
                evt->parsed_node = MsgPackParse(
                    conn->message_buf_.data(), conn->message_buf_.size());
                if (!evt->parsed_node) {
                    evt->data = std::move(conn->message_buf_);
                }
            }
        } else {
            evt->data = std::move(conn->message_buf_);
        }
        conn->message_buf_.clear();
        self->PushSocketEvent(evt);
    }
}

void EventLoop::OnWsCloseTimeout(uv_timer_t* handle) {
    WsConnection* conn = static_cast<WsConnection*>(handle->data);
    EventLoop* self = static_cast<EventLoop*>(handle->loop->data);

    // Close timeout — force cleanup
    conn->close_timer_ = nullptr;
    uv_close(reinterpret_cast<uv_handle_t*>(handle), [](uv_handle_t* h) {
        delete reinterpret_cast<uv_timer_t*>(h);
    });

    auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
        conn->plugin_context, conn->on_close, conn->userdata, conn->handle_closed.load());
    self->WsCleanup(conn);
    self->PushSocketEvent(evt);
}

void EventLoop::OnWsPingTimer(uv_timer_t* handle) {
    WsConnection* conn = static_cast<WsConnection*>(handle->data);

    if (conn->handle_closed || conn->state != WsState::CONNECTED || !conn->curl_handle) return;

    size_t sent;
    curl_ws_send(conn->curl_handle, "", 0, &sent, 0, CURLWS_PING);
}

void EventLoop::WsStartPingTimer(WsConnection* conn) {
    conn->ping_timer_ = new uv_timer_t;
    uv_timer_init(loop_, conn->ping_timer_);
    conn->ping_timer_->data = conn;
    uv_timer_start(conn->ping_timer_, OnWsPingTimer,
                    conn->ping_interval * 1000, conn->ping_interval * 1000);
}

void EventLoop::WsStopTimers(WsConnection* conn) {
    if (conn->close_timer_) {
        uv_timer_stop(conn->close_timer_);
        uv_close(reinterpret_cast<uv_handle_t*>(conn->close_timer_), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_timer_t*>(h);
        });
        conn->close_timer_ = nullptr;
    }
    if (conn->ping_timer_) {
        uv_timer_stop(conn->ping_timer_);
        uv_close(reinterpret_cast<uv_handle_t*>(conn->ping_timer_), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_timer_t*>(h);
        });
        conn->ping_timer_ = nullptr;
    }
    if (conn->reconnect_timer_) {
        uv_timer_stop(conn->reconnect_timer_);
        ws_reconnect_timers_.erase(conn->reconnect_timer_);
        uv_close(reinterpret_cast<uv_handle_t*>(conn->reconnect_timer_), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_timer_t*>(h);
        });
        conn->reconnect_timer_ = nullptr;
    }
}

bool EventLoop::WsShouldReconnect(WsConnection* conn) {
    if (conn->handle_closed) return false;
    if (conn->state == WsState::CLOSING) return false;  // user-initiated close
    if (conn->max_reconnects == 0) return false;
    if (conn->max_reconnects > 0 && conn->reconnect_count >= conn->max_reconnects) return false;
    return true;
}

void EventLoop::WsDisconnectOrReconnect(WsConnection* conn) {
    if (WsShouldReconnect(conn)) {
        WsStartReconnect(conn);
    } else {
        auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
            conn->plugin_context, conn->on_close, conn->userdata, conn->handle_closed.load());
        WsCleanup(conn);
        PushSocketEvent(evt);
    }
}

void EventLoop::WsDisconnectOrReconnect(WsConnection* conn, int error_code, const std::string& error_msg) {
    if (WsShouldReconnect(conn)) {
        WsStartReconnect(conn);
    } else {
        auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
            conn->plugin_context, conn->on_close, conn->userdata, conn->handle_closed.load());
        evt->error_code = error_code;
        evt->error_msg = error_msg;
        WsCleanup(conn);
        PushSocketEvent(evt);
    }
}

void EventLoop::WsStartReconnect(WsConnection* conn) {
    WsCleanup(conn, true);

    conn->reconnect_count++;
    
    // Exponential backoff with full jitter
    int delay = static_cast<int>(conn->reconnect_delay_ms *
        std::pow(conn->reconnect_backoff, conn->reconnect_count - 1));
    if (delay > conn->reconnect_max_delay_ms) delay = conn->reconnect_max_delay_ms;
    if (delay < 0) delay = conn->reconnect_max_delay_ms;  // overflow guard
    thread_local std::minstd_rand rng(std::random_device{}());
    delay = std::uniform_int_distribution<int>(0, delay)(rng);

    conn->reconnect_timer_ = new uv_timer_t;
    uv_timer_init(loop_, conn->reconnect_timer_);
    conn->reconnect_timer_->data = conn;
    ws_reconnect_timers_.insert(conn->reconnect_timer_);
    uv_timer_start(conn->reconnect_timer_, OnWsReconnectTimer, delay, 0);
}

bool EventLoop::WsInitCurl(WsConnection* conn) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    conn->curl_handle = curl;
    conn->state = WsState::CONNECTING;
    curl_easy_setopt(curl, CURLOPT_URL, conn->url_.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl, CURLOPT_PRIVATE, static_cast<void*>(conn));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SHARE, curl_share_);
    BuildHeaderSlist(conn->headers_, conn->headers_mutex_, conn->built_headers_);
    if (conn->built_headers_)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, conn->built_headers_);
    if (!conn->auto_pong)
        curl_easy_setopt(curl, CURLOPT_WS_OPTIONS, static_cast<long>(CURLWS_NOAUTOPONG));
    if (!conn->ssl_verifypeer)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    if (!conn->ssl_verifyhost)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(conn->connect_timeout_ms));

    ws_curl_handles_.insert(curl);
    curl_multi_add_handle(curl_multi_, curl);

    int running;
    curl_multi_socket_action(curl_multi_, CURL_SOCKET_TIMEOUT, 0, &running);
    CheckCompletedJobs();
    return true;
}

void EventLoop::WsReconnect(WsConnection* conn) {
    if (!WsInitCurl(conn)) {
        PushSocketEvent(MakeErrorEvent(SocketEventType::WS_ERROR, conn->handle_id,
            conn->plugin_context, conn->on_error, conn->userdata, false,
            -1, "curl_easy_init failed during reconnect"));
        auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
            conn->plugin_context, conn->on_close, conn->userdata, false);
        conn->state = WsState::CLOSED;
        ws_connections_.erase(conn->handle_id);
        PushSocketEvent(evt);
    }
}

void EventLoop::OnWsReconnectTimer(uv_timer_t* handle) {
    WsConnection* conn = static_cast<WsConnection*>(handle->data);
    EventLoop* self = static_cast<EventLoop*>(handle->loop->data);

    self->ws_reconnect_timers_.erase(handle);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), [](uv_handle_t* h) {
        delete reinterpret_cast<uv_timer_t*>(h);
    });
    conn->reconnect_timer_ = nullptr;
    
    if (conn->handle_closed) {
        auto* evt = MakeSocketEvent(SocketEventType::WS_CLOSED, conn->handle_id,
            conn->plugin_context, conn->on_close, conn->userdata, true);
        conn->state = WsState::CLOSED;
        self->ws_connections_.erase(conn->handle_id);
        self->PushSocketEvent(evt);
        return;
    }

    self->WsReconnect(conn);
}

void EventLoop::WsCleanup(WsConnection* conn, bool keep_connection) {
    WsStopTimers(conn);

    // Stop polling
    if (conn->uv_poll) {
        uv_poll_stop(conn->uv_poll);
        uv_close(reinterpret_cast<uv_handle_t*>(conn->uv_poll), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_poll_t*>(h);
        });
        conn->uv_poll = nullptr;
    }

    // Remove from curl_multi, cleanup curl handle
    if (conn->curl_handle) {
        ws_curl_handles_.erase(conn->curl_handle);
        curl_multi_remove_handle(curl_multi_, conn->curl_handle);
        curl_easy_cleanup(conn->curl_handle);
        conn->curl_handle = nullptr;
    }

    // Free built header slist (rebuilt in WsInitCurl from headers_ map)
    if (conn->built_headers_) {
        curl_slist_free_all(conn->built_headers_);
        conn->built_headers_ = nullptr;
    }

    if (keep_connection) {
        conn->sockfd = CURL_SOCKET_BAD;
        conn->message_buf_.clear();
        conn->in_fragmented_message_ = false;
        conn->close_sent_ = false;
        conn->state = WsState::RECONNECTING;
    } else {
        conn->state = WsState::CLOSED;
        ws_connections_.erase(conn->handle_id);
    }
}
