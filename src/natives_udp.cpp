#include <cstring>

#include "smsdk_ext.h"
#include "extension.h"
#include "udp_socket.h"
#include "event_loop.h"
#include "natives.h"

extern EventLoop g_event_loop;

// async2_UdpNew(any userdata = 0) -> UdpSocket
static cell_t Native_UdpNew(IPluginContext* pContext, const cell_t* params) {
    auto* sock = new UdpSocket(pContext, params[1]);
    int handle = g_handle_manager.CreateHandle(static_cast<void*>(sock), HANDLE_UDP_SOCKET);
    if (handle == 0) {
        delete sock;
        return 0;
    }
    sock->handle_id = handle;
    return handle;
}

// async2_UdpSetCallbacks(socket, onData, onError, onClose)
static cell_t Native_UdpSetCallbacks(IPluginContext* pContext, const cell_t* params) {
    UdpSocket* sock = g_handle_manager.GetUdpSocket(params[1]);
    if (!sock) return 0;

    sock->on_data = params[2];
    sock->on_error = params[3];
    sock->on_close = params[4];
    return 0;
}

// async2_UdpBind(socket, addr, port)
static cell_t Native_UdpBind(IPluginContext* pContext, const cell_t* params) {
    UdpSocket* sock = g_handle_manager.GetUdpSocket(params[1]);
    if (!sock) return 0;

    char* addr;
    pContext->LocalToString(params[2], &addr);
    int port = params[3];

    auto* op = new UdpOp();
    op->type = UdpOpType::BIND;
    op->handle_id = sock->handle_id;
    op->addr = addr;
    op->port = port;
    op->socket_ptr = sock;

    g_event_loop.EnqueueUdpOp(op);
    return 0;
}

// async2_UdpSend(socket, data, length, addr, port)
static cell_t Native_UdpSend(IPluginContext* pContext, const cell_t* params) {
    UdpSocket* sock = g_handle_manager.GetUdpSocket(params[1]);
    if (!sock) return 0;

    cell_t* data_addr;
    pContext->LocalToPhysAddr(params[2], &data_addr);
    int length = params[3];
    if (length <= 0) return 0;

    char* addr;
    pContext->LocalToString(params[4], &addr);
    int port = params[5];

    auto* op = new UdpOp();
    op->type = UdpOpType::SEND;
    op->handle_id = sock->handle_id;
    op->data.assign(reinterpret_cast<uint8_t*>(data_addr), reinterpret_cast<uint8_t*>(data_addr) + length);
    op->addr = addr;
    op->port = port;
    op->socket_ptr = nullptr;

    g_event_loop.EnqueueUdpOp(op);
    return 0;
}

// async2_UdpClose(socket)
static cell_t Native_UdpClose(IPluginContext* pContext, const cell_t* params) {
    UdpSocket* sock = g_handle_manager.GetUdpSocket(params[1]);
    if (!sock) return 0;

    sock->handle_closed = true;

    auto* op = new UdpOp();
    op->type = UdpOpType::CLOSE;
    op->handle_id = sock->handle_id;

    g_event_loop.EnqueueUdpOp(op);
    return 0;
}

// async2_UdpSetOption(socket, option, value)
static cell_t Native_UdpSetOption(IPluginContext* pContext, const cell_t* params) {
    UdpSocket* sock = g_handle_manager.GetUdpSocket(params[1]);
    if (!sock) return 0;

    int option = params[2];
    int value = params[3];

    bool route_to_event_thread = false;

    switch (option) {
        case 0: // UDP_BROADCAST
            sock->broadcast = (value != 0);
            break;
        case 1: // UDP_TTL
            sock->ttl = value > 0 ? value : 0;
            break;
        case 2: // UDP_SEND_BUFSIZE
            sock->send_bufsize = value > 0 ? value : 0;
            route_to_event_thread = true;
            break;
        case 3: // UDP_RECV_BUFSIZE
            sock->recv_bufsize = value > 0 ? value : 0;
            route_to_event_thread = true;
            break;
        default:
            break;
    }

    if (route_to_event_thread) {
        auto* op = new UdpOp();
        op->type = UdpOpType::SET_OPTION;
        op->handle_id = sock->handle_id;
        op->option_id = option;
        op->option_value = value;
        g_event_loop.EnqueueUdpOp(op);
    }

    return 0;
}

sp_nativeinfo_t g_UdpNatives[] = {
    {"async2_UdpNew",           Native_UdpNew},
    {"async2_UdpSetCallbacks",  Native_UdpSetCallbacks},
    {"async2_UdpBind",          Native_UdpBind},
    {"async2_UdpSend",          Native_UdpSend},
    {"async2_UdpClose",         Native_UdpClose},
    {"async2_UdpSetOption",     Native_UdpSetOption},
    {nullptr,                   nullptr},
};
