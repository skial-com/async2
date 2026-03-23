#ifndef ASYNC2_EVENT_LOOP_H
#define ASYNC2_EVENT_LOOP_H

#include <uv.h>
#include <curl/curl.h>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "locked_queue.h"
#include "dns_resolver.h"

class HttpRequest;
class TcpSocket;
struct TcpOp;
struct SocketEvent;
class UdpSocket;
struct UdpOp;
class WsConnection;
struct WsOp;

struct CurlGlobalOption {
    CURLMoption key;
    long value;
};

enum class DnsOpType { SET_TIMEOUT };

struct DnsOp {
    DnsOpType type;
    int timeout_ms;
    int tries;
};

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    bool Start();
    void Stop();
    void EnqueueRequest(HttpRequest* request);
    void CancelRequest(HttpRequest* request);
    void DrainCompleted(std::vector<HttpRequest*>& out);
    void DrainSocketEvents(std::vector<SocketEvent*>& out);
    void SignalPending();
    void ChangeCurlSetting(CURLMoption key, long value);

    // TCP operations
    void EnqueueTcpOp(TcpOp* op);

    // UDP operations
    void EnqueueUdpOp(UdpOp* op);

    // WebSocket operations
    void EnqueueWsOp(WsOp* op);

    // DNS operations
    void EnqueueDnsOp(DnsOp* op);

    // DNS cache (thread-safe, delegates to DnsResolver)
    void DnsCacheFlush();
    void DnsCacheStats(int& count, int& memory);
    void DnsCacheSetTtl(int seconds);

    // Pool stats (atomic, safe to read from game thread)
    std::atomic<int> stats_active{0};     // currently in curl_multi
    std::atomic<int> stats_pending{0};    // in pending_queue
    std::atomic<int> stats_completed{0};  // lifetime completed
    std::atomic<int> stats_retries{0};    // lifetime retries

    LockedQueue<HttpRequest*> pending_queue;
    LockedQueue<HttpRequest*> cancel_queue;
    LockedQueue<HttpRequest*> done_queue;
    LockedQueue<CurlGlobalOption*> option_queue;

    LockedQueue<TcpOp*> tcp_op_queue;
    LockedQueue<UdpOp*> udp_op_queue;
    LockedQueue<WsOp*> ws_op_queue;
    LockedQueue<SocketEvent*> socket_done_queue;
    LockedQueue<DnsOp*> dns_op_queue;

private:
    CURLM* curl_multi_;
    CURLSH* curl_share_;
    uv_loop_t* loop_;
    uv_thread_t thread_;
    uv_timer_t timeout_;
    uv_async_t async_stop_;
    uv_async_t async_add_;
    uv_async_t async_settings_;
    uv_async_t async_cancel_;
    uv_async_t async_tcp_op_;
    uv_async_t async_udp_op_;
    uv_async_t async_dns_op_;
    uv_async_t async_ws_op_;

    DnsResolver dns_resolver_;
    std::unordered_map<int, TcpSocket*> tcp_sockets_;
    std::unordered_map<int, UdpSocket*> udp_sockets_;
    std::unordered_map<int, WsConnection*> ws_connections_;
    std::unordered_set<CURL*> ws_curl_handles_;
    std::unordered_set<CURL*> http_curl_handles_;  // in-flight HTTP easy handles in curl_multi
    std::unordered_set<HttpRequest*> active_http_requests_;  // all HTTP reqs known to event thread
    std::unordered_set<uv_timer_t*> retry_timers_;
    std::unordered_set<uv_timer_t*> ws_reconnect_timers_;

    static void EventLoopThread(void* data);
    static void OnRetryTimer(uv_timer_t* handle);
    static void OnTimeout(uv_timer_t* req);
    static void OnSocketActivity(uv_poll_t* req, int status, int events);
    static int OnCurlTimeout(CURLM* multi, long timeout_ms, void* userdata);
    static int OnCurlSocket(CURL* easy, curl_socket_t s, int action, void* data, void* socketdata);
    static size_t OnReceiveData(char* contents, size_t size, size_t nmemb, void* userdata);
    static size_t OnReceiveHeaders(char* buffer, size_t size, size_t nitems, void* userdata);
    static void OnAsyncAdd(uv_async_t* handle);
    static void OnAsyncStop(uv_async_t* handle);
    static void OnAsyncSettings(uv_async_t* handle);
    static void OnAsyncCancel(uv_async_t* handle);

    // TCP callbacks
    static void OnAsyncTcpOp(uv_async_t* handle);
    static void OnTcpConnected(uv_connect_t* req, int status);
    static void OnTcpAllocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void OnTcpRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnTcpWriteDone(uv_write_t* req, int status);
    static void OnTcpClosed(uv_handle_t* handle);
    static void OnTcpNewConnection(uv_stream_t* server, int status);

    // WebSocket callbacks
    static void OnAsyncWsOp(uv_async_t* handle);
    static void OnWsPollActivity(uv_poll_t* handle, int status, int events);
    static void OnWsCloseTimeout(uv_timer_t* handle);
    static void OnWsPingTimer(uv_timer_t* handle);
    static void OnWsReconnectTimer(uv_timer_t* handle);

    // DNS callbacks
    static void OnAsyncDnsOp(uv_async_t* handle);

    // UDP callbacks
    static void OnAsyncUdpOp(uv_async_t* handle);
    static void OnUdpAllocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void OnUdpRead(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                          const struct sockaddr* addr, unsigned flags);
    static void OnUdpSendDone(uv_udp_send_t* req, int status);
    static void OnUdpClosed(uv_handle_t* handle);

    void ProcessCurlJob(HttpRequest* request);
    void CheckCompletedJobs();

    void ProcessTcpConnect(TcpOp* op);
    void ProcessTcpSend(TcpOp* op);
    void ProcessTcpClose(TcpOp* op);
    void ProcessTcpListen(TcpOp* op);
    void ProcessTcpBind(TcpOp* op);
    void ProcessTcpSetOption(TcpOp* op);
    void ProcessTcpAcceptConfig(TcpOp* op);

    void ProcessUdpBind(UdpOp* op);
    void ProcessUdpSend(UdpOp* op);
    void ProcessUdpClose(UdpOp* op);
    void ProcessUdpSetOption(UdpOp* op);

    void ProcessWsConnect(WsOp* op);
    void ProcessWsSend(WsOp* op);
    void ProcessWsClose(WsOp* op);
    void ProcessWsSetOption(WsOp* op);
    void WsCleanup(WsConnection* conn, bool keep_connection = false);
    bool WsShouldReconnect(WsConnection* conn);
    void WsStartReconnect(WsConnection* conn);
    void WsDisconnectOrReconnect(WsConnection* conn);
    void WsDisconnectOrReconnect(WsConnection* conn, int error_code, const std::string& error_msg);
    void WsReconnect(WsConnection* conn);
    bool WsInitCurl(WsConnection* conn);
    void WsStartPingTimer(WsConnection* conn);
    void WsStopTimers(WsConnection* conn);

    void PushSocketEvent(SocketEvent* evt);
};

class CurlSocketContext {
public:
    curl_socket_t curl_socket;
    uv_poll_t uv_poll_handle;

    CurlSocketContext(curl_socket_t s, uv_loop_t* loop);
    void BeginRemoval();
    static void OnPollClosed(uv_handle_t* handle);
};

#endif
