#ifndef ASYNC2_UDP_SOCKET_H
#define ASYNC2_UDP_SOCKET_H

#include <uv.h>
#include <atomic>
#include <string>
#include <vector>
#include "smsdk_ext.h"

enum class UdpState { CREATED, BOUND, CLOSING, CLOSED };
enum class UdpOpType { BIND, SEND, CLOSE, SET_OPTION };

class UdpSocket;  // forward declaration

struct UdpOp {
    UdpOpType type;
    int handle_id;
    std::string addr;
    int port;
    std::vector<uint8_t> data;
    int option_id;      // for SET_OPTION
    int option_value;   // for SET_OPTION
    UdpSocket* socket_ptr;  // used by BIND to register in event loop
};

class UdpSocket {
public:
    int handle_id;
    IPluginContext* plugin_context;
    funcid_t on_data;
    funcid_t on_error;
    funcid_t on_close;
    int userdata;
    bool broadcast{false};  // SO_BROADCAST
    int ttl{0};             // IP_TTL (0 = OS default)
    int send_bufsize{0};    // SO_SNDBUF (0 = OS default)
    int recv_bufsize{0};    // SO_RCVBUF (0 = OS default)
    std::atomic<bool> handle_closed{false};
    uv_udp_t* uv_handle;   // event thread only
    UdpState state;

    UdpSocket(IPluginContext* ctx, int ud)
        : handle_id(0), plugin_context(ctx),
          on_data(0), on_error(0), on_close(0),
          userdata(ud), uv_handle(nullptr), state(UdpState::CREATED) {}
    ~UdpSocket() {}
};

#endif
