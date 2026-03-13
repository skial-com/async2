#ifndef ASYNC2_WS_CONNECTION_H
#define ASYNC2_WS_CONNECTION_H

#include <uv.h>
#include <curl/curl.h>
#include <atomic>
#include <string>
#include <vector>
#include "smsdk_ext.h"
#include "header_map.h"

enum class WsState { CREATED, CONNECTING, CONNECTED, CLOSING, CLOSED, RECONNECTING };

enum class WsOpType { CONNECT, SEND_TEXT, SEND_BINARY, SEND_PING, SEND_JSON, SEND_MSGPACK, CLOSE, SET_OPTION };

class WsConnection;
struct DataNode;

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
    DataNode* body_node = nullptr;   // SEND_JSON/SEND_MSGPACK: deep-copied tree
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

    // Message reassembly (event thread only)
    std::vector<uint8_t> message_buf_;
    bool is_binary_message_ = false;
    bool in_fragmented_message_ = false;

    // Close handshake (event thread only)
    bool close_sent_ = false;
    uv_timer_t* close_timer_ = nullptr;

    // Auto-ping (event thread only)
    uv_timer_t* ping_timer_ = nullptr;

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
