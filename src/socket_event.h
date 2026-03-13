#ifndef ASYNC2_SOCKET_EVENT_H
#define ASYNC2_SOCKET_EVENT_H

#include <cstdint>
#include <string>
#include <vector>
#include "smsdk_ext.h"
#include "data/data_node.h"

enum class SocketEventType {
    TCP_CONNECTED,
    TCP_ACCEPTED,
    TCP_DATA,
    TCP_ERROR,
    TCP_CLOSED,
    UDP_DATA,
    UDP_ERROR,
    UDP_CLOSED,
    WS_CONNECTED,
    WS_MESSAGE,
    WS_ERROR,
    WS_CLOSED,
    WS_PONG,
};

struct SocketEvent {
    SocketEventType type;
    int handle_id;
    IPluginContext* plugin_ctx;
    funcid_t callback;
    std::vector<uint8_t> data;
    int error_code = 0;
    std::string error_msg;
    std::string remote_addr;
    int remote_port = 0;
    intptr_t accepted_handle_id = 0;
    int userdata = 0;
    bool handle_closed = false;
    bool is_binary = false;
    DataNode* parsed_node = nullptr;
    int parse_mode = 0;  // copied from WsConnection::parse_messages at event creation
    funcid_t error_callback = 0;  // for parse failure: fire onError instead of onMessage

    ~SocketEvent() {
        if (parsed_node) DataNode::Destroy(parsed_node);
    }
};

inline bool IsCloseEvent(SocketEventType type) {
    return type == SocketEventType::TCP_CLOSED ||
           type == SocketEventType::UDP_CLOSED ||
           type == SocketEventType::WS_CLOSED;
}

// Factory: simple event (CONNECTED, CLOSED)
inline SocketEvent* MakeSocketEvent(SocketEventType type, int handle_id,
                                     IPluginContext* ctx, funcid_t callback,
                                     int userdata, bool closed) {
    auto* evt = new SocketEvent();
    evt->type = type;
    evt->handle_id = handle_id;
    evt->plugin_ctx = ctx;
    evt->callback = callback;
    evt->userdata = userdata;
    evt->handle_closed = closed;
    return evt;
}

// Factory: error event
inline SocketEvent* MakeErrorEvent(SocketEventType type, int handle_id,
                                    IPluginContext* ctx, funcid_t callback,
                                    int userdata, bool closed,
                                    int error_code, const char* error_msg) {
    auto* evt = MakeSocketEvent(type, handle_id, ctx, callback, userdata, closed);
    evt->error_code = error_code;
    evt->error_msg = error_msg;
    return evt;
}

#endif
