#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cstdio>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <curl/curl.h>

#include "extension.h"
#include "event_loop.h"
#include "handle_manager.h"
#include "http_request.h"
#include "tcp_socket.h"
#include "udp_socket.h"
#include "ws_connection.h"
#include "socket_event.h"
#include "natives.h"
#include "data/data_node.h"
#include "data/data_handle.h"
#include "msgpack/msgpack_parse.h"

extern const char* g_compile_time;   // from compile_time.cpp
extern const char* g_build_info;     // from compile_time.cpp
extern const char* g_deps_version;   // from compile_time.cpp

HandleManager g_handle_manager;
EventLoop g_event_loop;
std::atomic<int> g_log_level{0};
std::unordered_set<IPluginContext*> g_plugin_contexts;

// Reverse index: client_index → HTTP handle IDs for O(1) disconnect lookup
static std::unordered_map<int, std::vector<int>> g_client_handles;

void TrackClientHandle(int client, int handle_id) {
    if (client <= 0) return;
    g_client_handles[client].push_back(handle_id);
}

void UntrackClientHandle(int client, int handle_id) {
    if (client <= 0) return;
    auto it = g_client_handles.find(client);
    if (it == g_client_handles.end()) return;
    auto& vec = it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), handle_id), vec.end());
    if (vec.empty()) g_client_handles.erase(it);
}

Async2Extension g_Async2Extension;
SMEXT_LINK(&g_Async2Extension);

IPluginFunction* GetSourcepawnFunction(IPluginContext* context, cell_t id) {
    if (g_plugin_contexts.find(context) == g_plugin_contexts.end())
        return nullptr;
    return context->GetFunctionById(id);
}

static void DispatchSocketEvent(SocketEvent* evt) {
    if (evt->handle_closed) {
        // Socket was closed by user — skip non-close events (stale data/errors
        // that arrived after Close was called), but still fire close callbacks
        // so users always get a completion callback.
        if (!IsCloseEvent(evt->type)) {
            delete evt;
            return;
        }
    }

    // Debug logging for socket events
    if (g_log_level.load(std::memory_order_relaxed) >= 3) {
        switch (evt->type) {
            case SocketEventType::TCP_CONNECTED:
                smutils->LogMessage(myself, "[async2] TCP %d connected", evt->handle_id);
                break;
            case SocketEventType::TCP_ERROR:
                smutils->LogMessage(myself, "[async2] TCP %d error: %d (%s)",
                    evt->handle_id, evt->error_code, evt->error_msg.c_str());
                break;
            case SocketEventType::TCP_CLOSED:
                smutils->LogMessage(myself, "[async2] TCP %d closed", evt->handle_id);
                break;
            case SocketEventType::UDP_ERROR:
                smutils->LogMessage(myself, "[async2] UDP %d error: %d (%s)",
                    evt->handle_id, evt->error_code, evt->error_msg.c_str());
                break;
            case SocketEventType::UDP_CLOSED:
                smutils->LogMessage(myself, "[async2] UDP %d closed", evt->handle_id);
                break;
            case SocketEventType::WS_CONNECTED:
                smutils->LogMessage(myself, "[async2] WS %d connected", evt->handle_id);
                break;
            case SocketEventType::WS_ERROR:
                smutils->LogMessage(myself, "[async2] WS %d error: %d (%s)",
                    evt->handle_id, evt->error_code, evt->error_msg.c_str());
                break;
            case SocketEventType::WS_CLOSED:
                smutils->LogMessage(myself, "[async2] WS %d closed (code %d)",
                    evt->handle_id, evt->error_code);
                break;
            default:
                break;
        }
    }

    IPluginFunction* fn = GetSourcepawnFunction(evt->plugin_ctx, evt->callback);

    switch (evt->type) {
        case SocketEventType::TCP_CONNECTED: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::TCP_ACCEPTED: {
            // Register the accepted socket in the handle manager
            TcpSocket* client = reinterpret_cast<TcpSocket*>(static_cast<intptr_t>(evt->accepted_handle_id));
            int client_handle = g_handle_manager.CreateHandle(static_cast<void*>(client), HANDLE_TCP_SOCKET);
            if (client_handle == 0) {
                delete client;
                break;
            }
            client->handle_id = client_handle;

            // Register in event loop's tcp_sockets_ map and start reading.
            // Must happen via ACCEPT_CONFIG op so handle_id is set before
            // any data events are pushed (OnTcpRead uses sock->handle_id).
            auto* cfg = new TcpOp();
            cfg->type = TcpOpType::ACCEPT_CONFIG;
            cfg->handle_id = client_handle;
            cfg->socket_ptr = client;
            cfg->on_data = client->on_data;
            cfg->on_error = client->on_error;
            cfg->on_close = client->on_close;
            cfg->on_connect = client->on_connect;
            g_event_loop.EnqueueTcpOp(cfg);

            if (fn) {
                fn->PushCell(evt->handle_id);  // listener handle
                fn->PushCell(client_handle);    // accepted socket handle
                fn->PushStringEx(const_cast<char*>(evt->remote_addr.c_str()),
                                 evt->remote_addr.size() + 1, SM_PARAM_STRING_COPY, 0);
                fn->PushCell(evt->remote_port);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::TCP_DATA: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushStringEx(reinterpret_cast<char*>(evt->data.data()),
                                 evt->data.size(), SM_PARAM_STRING_COPY | SM_PARAM_STRING_BINARY, 0);
                fn->PushCell(static_cast<cell_t>(evt->data.size()));
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::TCP_ERROR: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushCell(evt->error_code);
                fn->PushStringEx(const_cast<char*>(evt->error_msg.c_str()),
                                 evt->error_msg.size() + 1, SM_PARAM_STRING_COPY, 0);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::TCP_CLOSED: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushCell(evt->handle_closed ? 1 : 0);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::UDP_DATA: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushStringEx(reinterpret_cast<char*>(evt->data.data()),
                                 evt->data.size(), SM_PARAM_STRING_COPY | SM_PARAM_STRING_BINARY, 0);
                fn->PushCell(static_cast<cell_t>(evt->data.size()));
                fn->PushStringEx(const_cast<char*>(evt->remote_addr.c_str()),
                                 evt->remote_addr.size() + 1, SM_PARAM_STRING_COPY, 0);
                fn->PushCell(evt->remote_port);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::UDP_ERROR: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushCell(evt->error_code);
                fn->PushStringEx(const_cast<char*>(evt->error_msg.c_str()),
                                 evt->error_msg.size() + 1, SM_PARAM_STRING_COPY, 0);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::UDP_CLOSED: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushCell(evt->handle_closed ? 1 : 0);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::WS_CONNECTED: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::WS_MESSAGE: {
            if (fn) {
                if (evt->parse_mode > 0) {
                    // Take ownership of parsed node
                    DataNode* node = evt->parsed_node;
                    evt->parsed_node = nullptr;

                    // Fallback: event-thread parse failed, try game thread
                    std::string parse_error;
                    if (!node && !evt->is_binary && !evt->data.empty()) {
                        if (evt->parse_mode == 1)
                            node = DataParseJson(
                                reinterpret_cast<const char*>(evt->data.data()),
                                evt->data.size(), &parse_error);
                        else if (evt->parse_mode == 2)
                            node = MsgPackParse(evt->data.data(), evt->data.size());
                    }

                    if (node) {
                        int json_handle = 0;
                        DataHandle* dh = new DataHandle(node);
                        json_handle = g_handle_manager.CreateHandle(
                            static_cast<void*>(dh), HANDLE_JSON_VALUE);
                        if (json_handle == 0) delete dh;

                        fn->PushCell(evt->handle_id);
                        fn->PushCell(json_handle);
                        fn->PushCell(evt->userdata);
                        fn->Execute(nullptr);
                    } else {
                        // Parse failed — fire onError with WS_ERROR_PARSE_FAILED (-1)
                        IPluginFunction* err_fn = GetSourcepawnFunction(
                            evt->plugin_ctx, evt->error_callback);
                        if (err_fn) {
                            if (parse_error.empty())
                                parse_error = (evt->parse_mode == 2)
                                    ? "MsgPack parse failed" : "JSON parse failed";
                            err_fn->PushCell(evt->handle_id);
                            err_fn->PushCell(-1);  // WS_ERROR_PARSE_FAILED
                            err_fn->PushStringEx(
                                const_cast<char*>(parse_error.c_str()),
                                parse_error.size() + 1, SM_PARAM_STRING_COPY, 0);
                            err_fn->PushCell(evt->userdata);
                            err_fn->Execute(nullptr);
                        }
                    }
                } else {
                    // Raw mode: existing behavior
                    // Null-terminate so text data can be used as a C string
                    // (e.g., JsonParseString). Length excludes the null.
                    evt->data.push_back(0);
                    fn->PushCell(evt->handle_id);
                    fn->PushStringEx(reinterpret_cast<char*>(evt->data.data()),
                                     evt->data.size(),
                                     SM_PARAM_STRING_COPY | SM_PARAM_STRING_BINARY, 0);
                    fn->PushCell(static_cast<cell_t>(evt->data.size() - 1));
                    fn->PushCell(evt->is_binary ? 1 : 0);
                    fn->PushCell(evt->userdata);
                    fn->Execute(nullptr);
                }
            }
            break;
        }
        case SocketEventType::WS_ERROR: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushCell(evt->error_code);
                fn->PushStringEx(const_cast<char*>(evt->error_msg.c_str()),
                                 evt->error_msg.size() + 1, SM_PARAM_STRING_COPY, 0);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::WS_CLOSED: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushCell(evt->error_code);  // close code
                fn->PushStringEx(const_cast<char*>(evt->error_msg.c_str()),
                                 evt->error_msg.size() + 1, SM_PARAM_STRING_COPY, 0);
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
        case SocketEventType::WS_PONG: {
            if (fn) {
                fn->PushCell(evt->handle_id);
                fn->PushStringEx(reinterpret_cast<char*>(evt->data.data()),
                                 evt->data.size(), SM_PARAM_STRING_COPY | SM_PARAM_STRING_BINARY, 0);
                fn->PushCell(static_cast<cell_t>(evt->data.size()));
                fn->PushCell(evt->userdata);
                fn->Execute(nullptr);
            }
            break;
        }
    }

    // Free the socket handle after the close callback fires.
    // This deletes the socket object and recycles the handle number.
    // Must happen after the callback so the plugin can't observe a dangling handle.
    if (IsCloseEvent(evt->type)) {
        g_handle_manager.FreeHandle(evt->handle_id);
    }

    delete evt;
}

void OnGameFrame(bool simulating) {
    g_event_loop.SignalPending();

    if (g_event_loop.done_queue.HasItems()) {
        std::vector<HttpRequest*> completed;
        g_event_loop.DrainCompleted(completed);
        for (HttpRequest* req : completed) {
            if (g_log_level.load(std::memory_order_relaxed) >= 2 && !req->handle_closed) {
                long httpcode = 0;
                if (req->curlcode == CURLE_OK)
                    curl_easy_getinfo(req->curl, CURLINFO_RESPONSE_CODE, &httpcode);
                if (req->curlcode != CURLE_OK) {
                    smutils->LogMessage(myself, "[async2] HTTP %d failed: curl %d (%s)%s",
                        req->handle_id, req->curlcode, req->curl_error_message,
                        req->retry_count > 0 ? " (after retries)" : "");
                } else if (g_log_level.load(std::memory_order_relaxed) >= 3) {
                    smutils->LogMessage(myself, "[async2] HTTP %d: %ld (%zu bytes, %d retries)",
                        req->handle_id, httpcode, req->response_body.size(), req->retry_count);
                }
            }
            req->OnCompletedGameThread();
        }
    }

    if (g_event_loop.socket_done_queue.HasItems()) {
        std::vector<SocketEvent*> events;
        g_event_loop.DrainSocketEvents(events);
        for (SocketEvent* evt : events) {
            DispatchSocketEvent(evt);
        }
    }
}

class PluginsListener : public IPluginsListener {
public:
    virtual void OnPluginLoaded(IPlugin* plugin) {
        g_plugin_contexts.insert(plugin->GetBaseContext());
    }

    virtual void OnPluginUnloaded(IPlugin* plugin) {
        IPluginContext* ctx = plugin->GetBaseContext();
        g_plugin_contexts.erase(ctx);

        // Close TCP/UDP/WS sockets owned by this plugin
        std::vector<std::pair<int, Handle>> sockets_to_close;
        for (auto& [id, h] : g_handle_manager.GetHandles()) {
            if (h.type == HANDLE_TCP_SOCKET) {
                auto* sock = static_cast<TcpSocket*>(h.pointer);
                if (sock->plugin_context == ctx)
                    sockets_to_close.push_back({id, h});
            } else if (h.type == HANDLE_UDP_SOCKET) {
                auto* sock = static_cast<UdpSocket*>(h.pointer);
                if (sock->plugin_context == ctx)
                    sockets_to_close.push_back({id, h});
            } else if (h.type == HANDLE_WS_SOCKET) {
                auto* conn = static_cast<WsConnection*>(h.pointer);
                if (conn->plugin_context == ctx)
                    sockets_to_close.push_back({id, h});
            }
        }
        for (auto& [id, h] : sockets_to_close) {
            if (h.type == HANDLE_TCP_SOCKET) {
                auto* sock = static_cast<TcpSocket*>(h.pointer);
                sock->handle_closed = true;
                auto* op = new TcpOp();
                op->type = TcpOpType::CLOSE;
                op->handle_id = sock->handle_id;
                op->socket_ptr = nullptr;
                g_event_loop.EnqueueTcpOp(op);
            } else if (h.type == HANDLE_UDP_SOCKET) {
                auto* sock = static_cast<UdpSocket*>(h.pointer);
                sock->handle_closed = true;
                auto* uop = new UdpOp();
                uop->type = UdpOpType::CLOSE;
                uop->handle_id = sock->handle_id;
                g_event_loop.EnqueueUdpOp(uop);
            } else if (h.type == HANDLE_WS_SOCKET) {
                auto* conn = static_cast<WsConnection*>(h.pointer);
                conn->handle_closed = true;
                auto* wop = new WsOp();
                wop->type = WsOpType::CLOSE;
                wop->handle_id = conn->handle_id;
                wop->close_code = 1001;  // Going Away
                g_event_loop.EnqueueWsOp(wop);
            }
        }
    }
};
static PluginsListener g_plugins_listener;

bool Async2Extension::SDK_OnLoad(char* error, size_t maxlength, bool late) {
    sharesys->AddNatives(myself, g_HttpNatives);
    sharesys->AddNatives(myself, g_JsonNatives);
    sharesys->AddNatives(myself, g_CurlNatives);
    sharesys->AddNatives(myself, g_MsgPackNatives);
    sharesys->AddNatives(myself, g_TcpNatives);
    sharesys->AddNatives(myself, g_UdpNatives);
    sharesys->AddNatives(myself, g_DnsNatives);
    sharesys->AddNatives(myself, g_CryptoNatives);
    sharesys->AddNatives(myself, g_WsNatives);
    sharesys->AddNatives(myself, g_LinkedListNatives);
    sharesys->AddNatives(myself, g_UrlNatives);
    sharesys->AddNatives(myself, g_UtilsNatives);
    sharesys->AddNatives(myself, g_HjsonNatives);
    sharesys->RegisterLibrary(myself, "async2");

    curl_global_init(CURL_GLOBAL_ALL);

    if (!g_event_loop.Start()) {
        snprintf(error, maxlength, "Failed to start event loop");
        return false;
    }

    InitAnchoredClock();

    // Detect currently loaded plugins
    IPluginIterator* plugin_iterator = plsys->GetPluginIterator();
    while (plugin_iterator->MorePlugins()) {
        g_plugin_contexts.insert(plugin_iterator->GetPlugin()->GetBaseContext());
        plugin_iterator->NextPlugin();
    }
    plugin_iterator->Release();

    plsys->AddPluginsListener(static_cast<IPluginsListener*>(&g_plugins_listener));
    playerhelpers->AddClientListener(&g_Async2Extension);
    smutils->AddGameFrameHook(OnGameFrame);
    rootconsole->AddRootConsoleCommand3("async2", "Async2 extension commands", &g_Async2Extension);

    return true;
}

// Resolve jemalloc mallctl via dlsym — works regardless of how jemalloc is loaded
// (LD_PRELOAD, engine-linked, etc.)
#ifndef _WIN32
#include <dlfcn.h>
using mallctl_fn = int (*)(const char*, void*, size_t*, void*, size_t);

static mallctl_fn GetMallctl() {
    static mallctl_fn fn = reinterpret_cast<mallctl_fn>(dlsym(RTLD_DEFAULT, "mallctl"));
    return fn;
}
#endif

static bool JemallocDump(const char* path) {
#ifndef _WIN32
    auto fn = GetMallctl();
    if (!fn) return false;
    const char* p = path;
    return fn("prof.dump", nullptr, nullptr, &p, sizeof(p)) == 0;
#else
    return false;
#endif
}

void Async2Extension::OnRootConsoleCommand(const char *cmdname, const ICommandArgs *args) {
    const char *subcmd = args->ArgC() >= 3 ? args->Arg(2) : "";

    if (strcmp(subcmd, "version") == 0) {
        rootconsole->ConsolePrint("  %s (compiled %s)", g_build_info, g_compile_time);
        rootconsole->ConsolePrint("  %s", curl_version());
        rootconsole->ConsolePrint("  %s", g_deps_version);
    } else if (strcmp(subcmd, "mem") == 0) {
        // HTTP pool stats
        int active = g_event_loop.stats_active.load(std::memory_order_relaxed);
        int pending = g_event_loop.stats_pending.load(std::memory_order_relaxed);
        int completed = g_event_loop.stats_completed.load(std::memory_order_relaxed);
        int retries = g_event_loop.stats_retries.load(std::memory_order_relaxed);
        rootconsole->ConsolePrint("  HTTP: active=%d pending=%d completed=%d retries=%d",
            active, pending, completed, retries);

        // DataNode pool stats
        size_t total, free_blocks, block_size;
        DataPoolStats(total, free_blocks, block_size);
        size_t in_use = total - free_blocks;
        rootconsole->ConsolePrint("  DataNode pool: %zu allocated, %zu in-use, %zu free (block=%zu, mem=%.1f MB)",
            total, in_use, free_blocks, block_size,
            static_cast<double>(total * block_size) / (1024.0 * 1024.0));

        // Handle manager stats
        size_t handle_count = g_handle_manager.GetHandles().size();
        rootconsole->ConsolePrint("  Handles: %zu active", handle_count);

        // DNS cache stats
        int dns_count, dns_mem;
        g_event_loop.DnsCacheStats(dns_count, dns_mem);
        rootconsole->ConsolePrint("  DNS cache: %d entries, %d bytes", dns_count, dns_mem);

        // Process RSS (Linux)
#ifndef _WIN32
        long rss_pages = 0;
        FILE* f = fopen("/proc/self/statm", "r");
        if (f) {
            long vm_pages;
            if (fscanf(f, "%ld %ld", &vm_pages, &rss_pages) != 2)
                rss_pages = 0;
            fclose(f);
        }
        if (rss_pages > 0) {
            double rss_mb = static_cast<double>(rss_pages * sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
            rootconsole->ConsolePrint("  Process RSS: %.1f MB", rss_mb);
        }
#endif
    } else if (strcmp(subcmd, "heapdump") == 0) {
        const char* path = args->ArgC() >= 4 ? args->Arg(3) : nullptr;
        if (JemallocDump(path)) {
            rootconsole->ConsolePrint("  Heap profile dumped%s%s",
                path ? " to " : " (auto-named)", path ? path : "");
        } else {
            rootconsole->ConsolePrint("  Failed — jemalloc with prof:true not detected.");
            rootconsole->ConsolePrint("  Start server with: LD_PRELOAD=libjemalloc.so MALLOC_CONF=prof:true,prof_prefix:jeprof");
        }
    } else {
        rootconsole->ConsolePrint("  async2 version   - Show version and build info");
        rootconsole->ConsolePrint("  async2 mem       - Show memory and pool stats");
        rootconsole->ConsolePrint("  async2 heapdump [path] - Dump jemalloc heap profile");
    }
}

void Async2Extension::OnClientDisconnecting(int client) {
    auto it = g_client_handles.find(client);
    if (it == g_client_handles.end()) return;

    // Take ownership of the handle list, then erase the map entry.
    // This prevents stale entries if close triggers further untracking.
    std::vector<int> handles = std::move(it->second);
    g_client_handles.erase(it);

    for (int id : handles) {
        HttpRequest* request = g_handle_manager.GetHttpRequest(id);
        if (!request) continue;

        if (request->in_event_thread) {
            request->handle_closed = true;
            g_handle_manager.MarkHandleClosed(id);
            g_event_loop.CancelRequest(request);
        } else {
            g_handle_manager.FreeHandle(id);
        }
    }
}

void Async2Extension::SDK_OnUnload() {
    // Remove hooks first so OnGameFrame can't touch the event loop during shutdown
    smutils->RemoveGameFrameHook(OnGameFrame);
    playerhelpers->RemoveClientListener(&g_Async2Extension);
    plsys->RemovePluginsListener(static_cast<IPluginsListener*>(&g_plugins_listener));
    rootconsole->RemoveRootConsoleCommand("async2", &g_Async2Extension);

    rootconsole->ConsolePrint("[%s] Waiting for event thread to stop.", SMEXT_CONF_LOGTAG);
    g_event_loop.Stop();
    rootconsole->ConsolePrint("[%s] Event thread stopped.", SMEXT_CONF_LOGTAG);

    // Clean up game thread's parser before dlclose
    DataParserCleanup();

    g_handle_manager.CleanHandles();
    HttpRequest::DrainCurlPool();
    curl_global_cleanup();
}
