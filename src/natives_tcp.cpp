#include <cstring>

#include "smsdk_ext.h"
#include "extension.h"
#include "tcp_socket.h"
#include "event_loop.h"
#include "natives.h"

extern EventLoop g_event_loop;

// async2_TcpNew(any userdata = 0) -> TcpSocket
static cell_t Native_TcpNew(IPluginContext* pContext, const cell_t* params) {
    auto* sock = new TcpSocket(pContext, params[1]);
    int handle = g_handle_manager.CreateHandle(static_cast<void*>(sock), HANDLE_TCP_SOCKET, pContext);
    if (handle == 0) {
        delete sock;
        return 0;
    }
    sock->handle_id = handle;
    return handle;
}

// async2_TcpSetCallbacks(socket, onConnect, onData, onError, onClose)
static cell_t Native_TcpSetCallbacks(IPluginContext* pContext, const cell_t* params) {
    TcpSocket* sock = g_handle_manager.GetTcpSocket(params[1]);
    if (!sock) return 0;

    sock->on_connect = params[2];
    sock->on_data = params[3];
    sock->on_error = params[4];
    sock->on_close = params[5];
    return 0;
}

// async2_TcpConnect(socket, host, port)
static cell_t Native_TcpConnect(IPluginContext* pContext, const cell_t* params) {
    TcpSocket* sock = g_handle_manager.GetTcpSocket(params[1]);
    if (!sock) return 0;

    char* host;
    pContext->LocalToString(params[2], &host);
    int port = params[3];

    auto* op = new TcpOp();
    op->type = TcpOpType::CONNECT;
    op->handle_id = sock->handle_id;
    op->host = host;
    op->port = port;
    op->socket_ptr = sock;

    g_event_loop.EnqueueTcpOp(op);
    return 0;
}

// async2_TcpBind(socket, addr, port)
static cell_t Native_TcpBind(IPluginContext* pContext, const cell_t* params) {
    TcpSocket* sock = g_handle_manager.GetTcpSocket(params[1]);
    if (!sock) return 0;

    char* addr;
    pContext->LocalToString(params[2], &addr);
    int port = params[3];

    auto* op = new TcpOp();
    op->type = TcpOpType::BIND;
    op->handle_id = sock->handle_id;
    op->host = addr;
    op->port = port;
    op->socket_ptr = sock;

    g_event_loop.EnqueueTcpOp(op);
    return 0;
}

// async2_TcpSend(socket, data, length)
static cell_t Native_TcpSend(IPluginContext* pContext, const cell_t* params) {
    TcpSocket* sock = g_handle_manager.GetTcpSocket(params[1]);
    if (!sock) return 0;

    cell_t* addr;
    if (pContext->LocalToPhysAddr(params[2], &addr) != SP_ERROR_NONE)
        return 0;
    int length = params[3];
    if (length <= 0) return 0;

    auto* op = new TcpOp();
    op->type = TcpOpType::SEND;
    op->handle_id = sock->handle_id;
    op->data.assign(reinterpret_cast<uint8_t*>(addr), reinterpret_cast<uint8_t*>(addr) + length);

    g_event_loop.EnqueueTcpOp(op);
    return 0;
}

// async2_TcpClose(socket)
static cell_t Native_TcpClose(IPluginContext* pContext, const cell_t* params) {
    TcpSocket* sock = g_handle_manager.GetTcpSocket(params[1]);
    if (!sock) return 0;

    sock->handle_closed = true;

    auto* op = new TcpOp();
    op->type = TcpOpType::CLOSE;
    op->handle_id = sock->handle_id;

    g_event_loop.EnqueueTcpOp(op);
    return 0;
}

// async2_TcpListen(socket, addr, port, onAccept, backlog=128)
static cell_t Native_TcpListen(IPluginContext* pContext, const cell_t* params) {
    TcpSocket* sock = g_handle_manager.GetTcpSocket(params[1]);
    if (!sock) return 0;

    char* addr;
    pContext->LocalToString(params[2], &addr);
    int port = params[3];
    int backlog = params[5];

    auto* op = new TcpOp();
    op->type = TcpOpType::LISTEN;
    op->handle_id = sock->handle_id;
    op->host = addr;
    op->port = port;
    op->on_accept = params[4];
    op->backlog = backlog;
    op->socket_ptr = sock;

    g_event_loop.EnqueueTcpOp(op);
    return 0;
}

// async2_TcpSetOption(socket, option, value)
static cell_t Native_TcpSetOption(IPluginContext* pContext, const cell_t* params) {
    TcpSocket* sock = g_handle_manager.GetTcpSocket(params[1]);
    if (!sock) return 0;

    int option = params[2];
    int value = params[3];

    bool route_to_event_thread = false;

    switch (option) {
        case 0: // TCP_CHUNK_SIZE
            sock->max_chunk_size = value > 0 ? value : 4096;
            break;
        case 1: // TCP_NODELAY
            sock->nodelay = (value != 0);
            break;
        case 2: // TCP_KEEPALIVE
            sock->keepalive = (value != 0);
            break;
        case 3: // TCP_KEEPALIVE_DELAY (seconds)
            sock->keepalive_delay = value > 0 ? value : 60;
            break;
        case 4: // TCP_SEND_BUFSIZE
            sock->send_bufsize = value > 0 ? value : 0;
            route_to_event_thread = true;
            break;
        case 5: // TCP_RECV_BUFSIZE
            sock->recv_bufsize = value > 0 ? value : 0;
            route_to_event_thread = true;
            break;
        default:
            break;
    }

    if (route_to_event_thread) {
        auto* op = new TcpOp();
        op->type = TcpOpType::SET_OPTION;
        op->handle_id = sock->handle_id;
        op->option_id = option;
        op->option_value = value;
        g_event_loop.EnqueueTcpOp(op);
    }

    return 0;
}

sp_nativeinfo_t g_TcpNatives[] = {
    {"async2_TcpNew",           Native_TcpNew},
    {"async2_TcpSetCallbacks",  Native_TcpSetCallbacks},
    {"async2_TcpConnect",       Native_TcpConnect},
    {"async2_TcpBind",          Native_TcpBind},
    {"async2_TcpSend",          Native_TcpSend},
    {"async2_TcpClose",         Native_TcpClose},
    {"async2_TcpListen",        Native_TcpListen},
    {"async2_TcpSetOption",     Native_TcpSetOption},
    {nullptr,                   nullptr},
};
