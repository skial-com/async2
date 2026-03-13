#ifndef ASYNC2_DNS_RESOLVER_H
#define ASYNC2_DNS_RESOLVER_H

#include <uv.h>
#include <ares.h>
#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>

struct sockaddr;

struct DnsCacheEntry {
    sockaddr_storage addr;
    socklen_t addrlen;
    std::chrono::steady_clock::time_point expires_at;
};

class DnsResolver {
public:
    using ResolveCallback = std::function<void(int status, struct sockaddr* addr, socklen_t addrlen)>;

    DnsResolver();
    ~DnsResolver();

    bool Init(uv_loop_t* loop);
    void Shutdown();

    // Resolve hostname. If host is an IP literal, callback fires immediately.
    // Callback fires on event loop thread. status=0 on success, non-zero on error.
    void Resolve(const std::string& host, int port, ResolveCallback cb);

    // Reinitialize c-ares channel with new timeout settings. Event thread only.
    // Returns false if channel recreation fails.
    bool Reinit(int timeout_ms, int tries);

    // Thread-safe cache operations (mutex-protected)
    void FlushCache();
    void GetCacheStats(int& count, int& memory);
    void SetCacheTtl(int seconds);

private:
    uv_loop_t* loop_;
    ares_channel channel_;
    uv_timer_t timer_;
    bool initialized_;
    int timeout_ms_ = 5000;
    int tries_ = 2;

    // DNS cache (mutex-protected for cross-thread access)
    std::unordered_map<std::string, DnsCacheEntry> cache_;
    std::mutex cache_mutex_;
    std::atomic<int> cache_ttl_{60};  // seconds, 0 = disabled
    static constexpr size_t kMaxCacheEntries = 1024;

    struct PollHandle {
        uv_poll_t poll;
        ares_socket_t sock;
        DnsResolver* resolver;
    };
    std::unordered_map<ares_socket_t, PollHandle*> poll_handles_;

    struct ResolveRequest {
        DnsResolver* resolver;
        ResolveCallback callback;
        int port;
        std::string hostname;
    };

    static void OnSockStateChange(void* data, ares_socket_t sock, int readable, int writable);
    static void OnPollActivity(uv_poll_t* handle, int status, int events);
    static void OnTimer(uv_timer_t* handle);
    static void OnResolveComplete(void* arg, int status, int timeouts, struct ares_addrinfo* result);

    bool InitChannel();
    void UpdateTimer();

    static void SetAddrPort(sockaddr_storage* addr, int port);
};

#endif
