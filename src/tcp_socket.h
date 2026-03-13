#ifndef ASYNC2_TCP_SOCKET_H
#define ASYNC2_TCP_SOCKET_H

#include <uv.h>
#include <atomic>
#include <string>
#include <vector>
#include "smsdk_ext.h"

enum class TcpState { CREATED, RESOLVING, CONNECTING, CONNECTED, LISTENING, CLOSING, CLOSED };

enum class TcpOpType { CONNECT, SEND, CLOSE, LISTEN, BIND, SET_OPTION, ACCEPT_CONFIG };

class TcpSocket;  // forward declaration for TcpOp::socket_ptr

struct TcpOp {
    TcpOpType type;
    int handle_id;
    std::string host;
    int port;
    std::vector<uint8_t> data;
    // For ACCEPT_CONFIG: set callbacks on accepted socket
    funcid_t on_connect, on_data, on_error, on_close;
    int backlog;
    funcid_t on_accept;
    int option_id;      // for SET_OPTION
    int option_value;   // for SET_OPTION
    TcpSocket* socket_ptr;  // used by CONNECT/LISTEN/BIND to register in event loop
};

class TcpSocket {
public:
    int handle_id;
    IPluginContext* plugin_context;
    funcid_t on_connect;
    funcid_t on_data;
    funcid_t on_error;
    funcid_t on_close;
    funcid_t on_accept;   // server mode
    int userdata;
    int max_chunk_size;
    bool nodelay{false};       // TCP_NODELAY
    bool keepalive{false};     // SO_KEEPALIVE
    int keepalive_delay{60};   // seconds between keepalive probes
    int send_bufsize{0};       // SO_SNDBUF (0 = OS default)
    int recv_bufsize{0};       // SO_RCVBUF (0 = OS default)

    std::atomic<bool> handle_closed{false};

    // Event thread only
    uv_tcp_t* uv_handle;
    TcpState state;

    TcpSocket(IPluginContext* ctx, int userdata);
    ~TcpSocket();
};

struct TcpWriteReq {
    uv_write_t req;
    uv_buf_t buf;
    uint8_t* data;
};

#endif
