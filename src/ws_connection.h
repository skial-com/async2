#ifndef ASYNC2_WS_CONNECTION_H
#define ASYNC2_WS_CONNECTION_H

#include <uv.h>
#include <curl/curl.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include "smsdk_ext.h"
#include "header_map.h"

enum class WsState { CREATED, CONNECTING, CONNECTED, CLOSING, CLOSED, RECONNECTING };

// onError codes async2 itself raises (ws.inc mirrors these). -1 is
// WS_ERROR_PARSE_FAILED, raised from the game thread in extension.cpp.
constexpr int WS_ERROR_PONG_TIMEOUT = -2;     // no frame within WS_PONG_TIMEOUT of a ping
constexpr int WS_ERROR_SEND_QUEUE_FULL = -3;  // queued payload exceeded WS_MAX_SEND_QUEUE

enum class WsOpType { CONNECT, SEND_TEXT, SEND_BINARY, SEND_PING, SEND_JSON, SEND_MSGPACK, CLOSE, SET_OPTION };

class WsConnection;
struct DataNode;

// One outgoing frame waiting in WsConnection::send_queue_ (event thread only).
struct WsPendingFrame {
    std::vector<uint8_t> data;
    unsigned int flags;
    size_t offset = 0;   // payload bytes curl has already accepted
};

struct WsOp {
    WsOpType type;
    int handle_id;
    std::string url;                 // CONNECT
    std::vector<uint8_t> data;       // SEND_*
    int close_code = 1000;           // CLOSE
    std::string close_reason;        // CLOSE
    int option_id;                   // SET_OPTION
    int option_value;                // SET_OPTION
    WsConnection* conn_ptr;         // CONNECT: register in event loop
    DataNode* body_node = nullptr;   // SEND_JSON/SEND_MSGPACK: stolen or copied tree
    ~WsOp();
};

class WsConnection {
public:
    CURL* curl_handle = nullptr;
    uv_poll_t* uv_poll = nullptr;
    curl_socket_t sockfd = CURL_SOCKET_BAD;

    int handle_id = 0;
    IPluginContext* plugin_context;
    int userdata;
    std::atomic<bool> handle_closed{false};
    WsState state = WsState::CREATED;

    // Callbacks (set on game thread before Connect)
    funcid_t on_connect = 0;
    funcid_t on_message = 0;
    funcid_t on_error = 0;
    funcid_t on_close = 0;
    funcid_t on_pong = 0;

    // Custom headers for upgrade request (game thread writes, event thread reads)
    HeaderMap headers_;
    std::mutex headers_mutex_;
    struct curl_slist* built_headers_ = nullptr;  // event thread only

    void SetHeader(const char* key, const char* value);
    void RemoveHeader(const char* key);
    void ClearHeaders();

    // Options (set before Connect)
    int max_message_size = 16 * 1024 * 1024;  // 16 MB
    bool auto_pong = true;
    int ping_interval = 0;                     // seconds, 0 = disabled
    int close_timeout = 5;                     // seconds
    bool ssl_verifypeer = true;
    bool ssl_verifyhost = true;
    int parse_messages = 0;  // 0=raw, 1=JSON, 2=MsgPack
    int connect_timeout_ms = 10000;  // 10 seconds default
    int pong_timeout = 0;            // seconds, 0 = disabled; needs ping_interval > 0
    int max_send_queue = 16 * 1024 * 1024;  // queued payload bytes before the connection fails

    // Message reassembly (event thread only)
    std::vector<uint8_t> message_buf_;
    bool is_binary_message_ = false;
    bool in_fragmented_message_ = false;

    // Close handshake (event thread only)
    bool close_sent_ = false;
    uv_timer_t* close_timer_ = nullptr;

    // Set once a WS_CLOSED event has been enqueued for this connection.
    // All WS_CLOSED push sites must check this and skip on true, to prevent
    // the game thread from double-FreeHandle-ing a recycled handle id.
    bool close_event_pushed_ = false;

    // Auto-ping (event thread only)
    uv_timer_t* ping_timer_ = nullptr;

    // Pong watchdog (event thread only). An auto-ping arms pong_timer_ and snapshots
    // rx_frames_; if no frame of any kind has arrived when it fires, the peer is
    // gone without a FIN and the connection is treated as dropped.
    uv_timer_t* pong_timer_ = nullptr;
    uint64_t rx_frames_ = 0;
    uint64_t rx_frames_at_ping_ = 0;

    // Outgoing frames (event thread only). curl_ws_send is non-blocking: it can take
    // part of a frame (CURLE_OK with sent < len) or none of it (CURLE_AGAIN), and a
    // frame it has started must be finished before another may begin. So every send
    // -- data, ping, close, close echo -- goes through this FIFO; only the head is
    // ever in progress, and the rest wait for UV_WRITABLE. Cleared on disconnect:
    // a half-sent frame cannot be resumed on a new connection.
    std::deque<WsPendingFrame> send_queue_;
    size_t send_queue_bytes_ = 0;
    int poll_events_ = 0;   // events uv_poll is currently started with

    // Reconnect settings (set on game thread before Connect, read on event thread)
    int max_reconnects = 0;              // 0 = disabled, -1 = unlimited
    int reconnect_delay_ms = 1000;
    float reconnect_backoff = 2.0f;
    int reconnect_max_delay_ms = 30000;
    std::atomic<int> reconnect_count{0};

    // Reconnect state (event thread only)
    uv_timer_t* reconnect_timer_ = nullptr;
    std::string url_;

    WsConnection(IPluginContext* ctx, int userdata);
    ~WsConnection();
};

#endif
