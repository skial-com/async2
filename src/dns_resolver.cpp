#include "dns_resolver.h"
#include <cstring>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

DnsResolver::DnsResolver() : loop_(nullptr), channel_(nullptr), initialized_(false) {}

DnsResolver::~DnsResolver() {
    if (initialized_)
        Shutdown();
}

bool DnsResolver::InitChannel() {
    struct ares_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.timeout = timeout_ms_;
    opts.tries = tries_;
    opts.sock_state_cb = OnSockStateChange;
    opts.sock_state_cb_data = this;

    int flags = ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES | ARES_OPT_SOCK_STATE_CB;

    // Disable c-ares built-in query cache — we manage our own app-level cache
    // with flush/stats support. Having both creates confusing semantics
    // (DnsCacheFlush wouldn't clear c-ares's internal cache).
    opts.qcache_max_ttl = 0;
    flags |= ARES_OPT_QUERY_CACHE;

    int rc = ares_init_options(&channel_, &opts, flags);
    return rc == ARES_SUCCESS;
}

bool DnsResolver::Init(uv_loop_t* loop) {
    loop_ = loop;

    if (!InitChannel())
        return false;

    uv_timer_init(loop_, &timer_);
    timer_.data = this;

    initialized_ = true;
    return true;
}

void DnsResolver::Shutdown() {
    if (!initialized_) return;

    for (auto& [sock, ph] : poll_handles_) {
        uv_poll_stop(&ph->poll);
        uv_close(reinterpret_cast<uv_handle_t*>(&ph->poll), [](uv_handle_t* h) {
            delete reinterpret_cast<PollHandle*>(h->data);
        });
    }
    poll_handles_.clear();

    uv_timer_stop(&timer_);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer_), nullptr);

    if (channel_) {
        ares_destroy(channel_);
        channel_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.clear();
    }

    initialized_ = false;
}

bool DnsResolver::Reinit(int timeout_ms, int tries) {
    if (!initialized_) return false;

    timeout_ms_ = timeout_ms > 0 ? timeout_ms : 5000;
    tries_ = tries > 0 ? tries : 2;

    // Cancel pending queries (callbacks fire with ARES_ECANCELLED)
    ares_cancel(channel_);
    ares_destroy(channel_);
    channel_ = nullptr;

    return InitChannel();
}

void DnsResolver::SetAddrPort(sockaddr_storage* addr, int port) {
    if (addr->ss_family == AF_INET) {
        reinterpret_cast<struct sockaddr_in*>(addr)->sin_port = htons(static_cast<uint16_t>(port));
    } else if (addr->ss_family == AF_INET6) {
        reinterpret_cast<struct sockaddr_in6*>(addr)->sin6_port = htons(static_cast<uint16_t>(port));
    }
}

void DnsResolver::Resolve(const std::string& host, int port, ResolveCallback cb) {
    // Check if host is an IP literal — no caching needed
    struct sockaddr_in sin4;
    struct sockaddr_in6 sin6;

    if (inet_pton(AF_INET, host.c_str(), &sin4.sin_addr) == 1) {
        sin4.sin_family = AF_INET;
        sin4.sin_port = htons(static_cast<uint16_t>(port));
        memset(sin4.sin_zero, 0, sizeof(sin4.sin_zero));
        cb(0, reinterpret_cast<struct sockaddr*>(&sin4), sizeof(sin4));
        return;
    }

    if (inet_pton(AF_INET6, host.c_str(), &sin6.sin6_addr) == 1) {
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port = htons(static_cast<uint16_t>(port));
        sin6.sin6_flowinfo = 0;
        sin6.sin6_scope_id = 0;
        cb(0, reinterpret_cast<struct sockaddr*>(&sin6), sizeof(sin6));
        return;
    }

    // Check application-level cache
    if (cache_ttl_ > 0) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(host);
        if (it != cache_.end()) {
            auto now = std::chrono::steady_clock::now();
            if (now < it->second.expires_at) {
                // Cache hit
                sockaddr_storage addr_copy = it->second.addr;
                socklen_t addrlen = it->second.addrlen;
                SetAddrPort(&addr_copy, port);
                cb(0, reinterpret_cast<struct sockaddr*>(&addr_copy), addrlen);
                return;
            }
            // Expired
            cache_.erase(it);
        }
    }

    // Cache miss — do c-ares resolution
    auto* req = new ResolveRequest{this, std::move(cb), port, host};

    struct ares_addrinfo_hints hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    ares_getaddrinfo(channel_, host.c_str(), nullptr, &hints, OnResolveComplete, req);
    UpdateTimer();
}

void DnsResolver::FlushCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
}

void DnsResolver::GetCacheStats(int& count, int& memory) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    count = static_cast<int>(cache_.size());
    size_t mem = 0;
    for (auto& [key, entry] : cache_) {
        mem += sizeof(DnsCacheEntry);
        mem += key.capacity() + 1;
    }
    // Bucket overhead
    mem += cache_.bucket_count() * sizeof(void*);
    memory = (mem > static_cast<size_t>(INT_MAX)) ? INT_MAX : static_cast<int>(mem);
}

void DnsResolver::SetCacheTtl(int seconds) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_ttl_ = seconds >= 0 ? seconds : 0;
}

// c-ares callbacks

void DnsResolver::OnSockStateChange(void* data, ares_socket_t sock, int readable, int writable) {
    auto* self = static_cast<DnsResolver*>(data);

    if (!readable && !writable) {
        auto it = self->poll_handles_.find(sock);
        if (it != self->poll_handles_.end()) {
            uv_poll_stop(&it->second->poll);
            auto* ph = it->second;
            self->poll_handles_.erase(it);
            uv_close(reinterpret_cast<uv_handle_t*>(&ph->poll), [](uv_handle_t* h) {
                delete static_cast<PollHandle*>(h->data);
            });
        }
        return;
    }

    int events = 0;
    if (readable) events |= UV_READABLE;
    if (writable) events |= UV_WRITABLE;

    auto it = self->poll_handles_.find(sock);
    if (it != self->poll_handles_.end()) {
        uv_poll_start(&it->second->poll, events, OnPollActivity);
    } else {
        auto* ph = new PollHandle();
        ph->sock = sock;
        ph->resolver = self;
        uv_poll_init_socket(self->loop_, &ph->poll, sock);
        ph->poll.data = ph;
        uv_poll_start(&ph->poll, events, OnPollActivity);
        self->poll_handles_[sock] = ph;
    }
}

void DnsResolver::OnPollActivity(uv_poll_t* handle, int status, int events) {
    auto* ph = static_cast<PollHandle*>(handle->data);
    auto* self = ph->resolver;

    ares_socket_t rfd = (events & UV_READABLE) ? ph->sock : ARES_SOCKET_BAD;
    ares_socket_t wfd = (events & UV_WRITABLE) ? ph->sock : ARES_SOCKET_BAD;

    ares_process_fd(self->channel_, rfd, wfd);
    self->UpdateTimer();
}

void DnsResolver::OnTimer(uv_timer_t* handle) {
    auto* self = static_cast<DnsResolver*>(handle->data);
    ares_process_fd(self->channel_, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
    self->UpdateTimer();
}

void DnsResolver::OnResolveComplete(void* arg, int status, int timeouts, struct ares_addrinfo* result) {
    auto* req = static_cast<ResolveRequest*>(arg);

    if (status != ARES_SUCCESS || !result || !result->nodes) {
        req->callback(status, nullptr, 0);
        if (result) ares_freeaddrinfo(result);
        delete req;
        return;
    }

    // Use first result
    struct ares_addrinfo_node* node = result->nodes;
    if (node->ai_family == AF_INET) {
        auto* sin = reinterpret_cast<struct sockaddr_in*>(node->ai_addr);
        sin->sin_port = htons(static_cast<uint16_t>(req->port));
    } else if (node->ai_family == AF_INET6) {
        auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(node->ai_addr);
        sin6->sin6_port = htons(static_cast<uint16_t>(req->port));
    } else {
        req->callback(ARES_ENOTFOUND, nullptr, 0);
        ares_freeaddrinfo(result);
        delete req;
        return;
    }

    // Cache the result (without port — port is set on lookup)
    DnsResolver* self = req->resolver;
    if (self->cache_ttl_ > 0 && !req->hostname.empty()) {
        std::lock_guard<std::mutex> lock(self->cache_mutex_);

        // Evict oldest if at capacity
        if (self->cache_.size() >= kMaxCacheEntries) {
            auto oldest = self->cache_.begin();
            for (auto it = self->cache_.begin(); it != self->cache_.end(); ++it) {
                if (it->second.expires_at < oldest->second.expires_at)
                    oldest = it;
            }
            self->cache_.erase(oldest);
        }

        DnsCacheEntry entry;
        memset(&entry.addr, 0, sizeof(entry.addr));
        memcpy(&entry.addr, node->ai_addr, node->ai_addrlen);
        entry.addrlen = node->ai_addrlen;

        // Clear port in cached entry — it's set per-lookup
        SetAddrPort(&entry.addr, 0);

        // Use DNS TTL from response, clamped to our configured max
        int max_ttl = self->cache_ttl_.load();
        int ttl = node->ai_ttl > 0 ? node->ai_ttl : max_ttl;
        if (ttl > max_ttl) ttl = max_ttl;

        entry.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);
        self->cache_[req->hostname] = entry;
    }

    req->callback(0, node->ai_addr, node->ai_addrlen);

    ares_freeaddrinfo(result);
    delete req;
}

void DnsResolver::UpdateTimer() {
    struct timeval tv;
    struct timeval* tvp = ares_timeout(channel_, nullptr, &tv);
    if (tvp) {
        uint64_t ms = tvp->tv_sec * 1000 + tvp->tv_usec / 1000;
        if (ms == 0) ms = 1;
        uv_timer_start(&timer_, OnTimer, ms, 0);
    } else {
        uv_timer_stop(&timer_);
    }
}
