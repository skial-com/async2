#include <cstring>

#include "smsdk_ext.h"
#include "extension.h"
#include "ws_connection.h"
#include "event_loop.h"
#include "natives.h"
#include "data/data_node.h"
#include "data/data_handle.h"

extern EventLoop g_event_loop;

// async2_WsNew(any userdata = 0) -> WsSocket
static cell_t Native_WsNew(IPluginContext* pContext, const cell_t* params) {
    auto* conn = new WsConnection(pContext, params[1]);
    int handle = g_handle_manager.CreateHandle(static_cast<void*>(conn), HANDLE_WS_SOCKET);
    if (handle == 0) {
        delete conn;
        return 0;
    }
    conn->handle_id = handle;
    return handle;
}

// async2_WsSetCallbacks(socket, onConnect, onMessage, onError, onClose, onPong=INVALID_FUNCTION)
static cell_t Native_WsSetCallbacks(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    conn->on_connect = params[2];
    conn->on_message = params[3];
    conn->on_error = params[4];
    conn->on_close = params[5];
    conn->on_pong = params[6];
    return 0;
}

// async2_WsSetHeader(socket, key, value)
static cell_t Native_WsSetHeader(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    char* key;
    char* value;
    pContext->LocalToString(params[2], &key);
    pContext->LocalToString(params[3], &value);

    conn->SetHeader(key, value);
    return 0;
}

// async2_WsRemoveHeader(socket, key)
static cell_t Native_WsRemoveHeader(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    char* key;
    pContext->LocalToString(params[2], &key);

    conn->RemoveHeader(key);
    return 0;
}

// async2_WsClearHeaders(socket)
static cell_t Native_WsClearHeaders(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    conn->ClearHeaders();
    return 0;
}

// async2_WsConnect(socket, url)
static cell_t Native_WsConnect(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    char* url;
    pContext->LocalToString(params[2], &url);

    auto* op = new WsOp();
    op->type = WsOpType::CONNECT;
    op->handle_id = conn->handle_id;
    op->url = url;
    op->conn_ptr = conn;

    g_event_loop.EnqueueWsOp(op);
    return 0;
}

// async2_WsSendText(socket, data, length=-1)
static cell_t Native_WsSendText(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    char* data;
    pContext->LocalToString(params[2], &data);
    int length = params[3];
    if (length < 0)
        length = strlen(data);

    auto* op = new WsOp();
    op->type = WsOpType::SEND_TEXT;
    op->handle_id = conn->handle_id;
    op->data.assign(reinterpret_cast<uint8_t*>(data), reinterpret_cast<uint8_t*>(data) + length);

    g_event_loop.EnqueueWsOp(op);
    return 0;
}

// async2_WsSendBinary(socket, data, length)
static cell_t Native_WsSendBinary(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    cell_t* addr;
    if (pContext->LocalToPhysAddr(params[2], &addr) != SP_ERROR_NONE)
        return 0;
    int length = params[3];
    if (length <= 0) return 0;

    auto* op = new WsOp();
    op->type = WsOpType::SEND_BINARY;
    op->handle_id = conn->handle_id;
    op->data.assign(reinterpret_cast<uint8_t*>(addr), reinterpret_cast<uint8_t*>(addr) + length);

    g_event_loop.EnqueueWsOp(op);
    return 0;
}

// async2_WsSendPing(socket, data="", length=0)
static cell_t Native_WsSendPing(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    cell_t* addr;
    if (pContext->LocalToPhysAddr(params[2], &addr) != SP_ERROR_NONE)
        return 0;
    int length = params[3];
    if (length > 125) length = 125;  // RFC 6455: control frames max 125 bytes

    auto* op = new WsOp();
    op->type = WsOpType::SEND_PING;
    op->handle_id = conn->handle_id;
    if (length > 0)
        op->data.assign(reinterpret_cast<uint8_t*>(addr), reinterpret_cast<uint8_t*>(addr) + length);

    g_event_loop.EnqueueWsOp(op);
    return 0;
}

// async2_WsClose(socket, code=1000, reason="")
static cell_t Native_WsClose(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    conn->handle_closed = true;

    char* reason;
    pContext->LocalToString(params[3], &reason);

    auto* op = new WsOp();
    op->type = WsOpType::CLOSE;
    op->handle_id = conn->handle_id;
    op->close_code = params[2];
    op->close_reason = reason;

    g_event_loop.EnqueueWsOp(op);
    return 0;
}

// async2_WsSetOption(socket, option, value)
static cell_t Native_WsSetOption(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 0;

    int option = params[2];
    int value = params[3];

    // Options that can be set before connect — store directly
    switch (option) {
        case 0: // WS_MAX_MESSAGE_SIZE
            conn->max_message_size = value > 0 ? value : 16 * 1024 * 1024;
            break;
        case 1: // WS_AUTO_PONG
            conn->auto_pong = (value != 0);
            break;
        case 2: // WS_PING_INTERVAL
            conn->ping_interval = value > 0 ? value : 0;
            break;
        case 3: // WS_CLOSE_TIMEOUT
            conn->close_timeout = value > 0 ? value : 5;
            break;
        case 4: // WS_SSL_VERIFYPEER
            conn->ssl_verifypeer = (value != 0);
            break;
        case 5: // WS_SSL_VERIFYHOST
            conn->ssl_verifyhost = (value != 0);
            break;
        case 6: // WS_PARSE_MESSAGES
            conn->parse_messages = (value >= 0 && value <= 2) ? value : 0;
            break;
        case 7: // WS_CONNECT_TIMEOUT
            conn->connect_timeout_ms = value > 0 ? value * 1000 : 10000;
            break;
        default:
            break;
    }

    // Route ping_interval changes to event thread (timer management is event-thread-only)
    if (option == 2) {
        auto* op = new WsOp();
        op->type = WsOpType::SET_OPTION;
        op->handle_id = conn->handle_id;
        op->option_id = option;
        op->option_value = value;
        g_event_loop.EnqueueWsOp(op);
    }

    return 0;
}

static cell_t WsSendNode(const cell_t* params, WsOpType type) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 2;

    DataHandle* dh = g_handle_manager.GetDataHandle(params[2]);
    if (!dh || !dh->node) return 2;

    DataNode* copy = dh->node->DeepCopy();
    if (!copy) return 3;

    auto* op = new WsOp();
    op->type = type;
    op->handle_id = conn->handle_id;
    op->body_node = copy;
    g_event_loop.EnqueueWsOp(op);
    return 0;
}

static cell_t Native_WsSendJson(IPluginContext* pContext, const cell_t* params) {
    return WsSendNode(params, WsOpType::SEND_JSON);
}

static cell_t Native_WsSendMsgPack(IPluginContext* pContext, const cell_t* params) {
    return WsSendNode(params, WsOpType::SEND_MSGPACK);
}

static cell_t Native_WsSetReconnect(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return 2;
    conn->max_reconnects = params[2];
    if (params[0] >= 3) conn->reconnect_delay_ms = params[3];
    if (params[0] >= 4) conn->reconnect_backoff = sp_ctof(params[4]);
    if (params[0] >= 5) conn->reconnect_max_delay_ms = params[5];
    return 0;
}

static cell_t Native_WsGetReconnectCount(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return -1;
    return conn->reconnect_count;
}

static cell_t Native_WsGetState(IPluginContext* pContext, const cell_t* params) {
    WsConnection* conn = g_handle_manager.GetWsSocket(params[1]);
    if (!conn) return -1;
    return static_cast<cell_t>(conn->state);
}

sp_nativeinfo_t g_WsNatives[] = {
    {"async2_WsNew",            Native_WsNew},
    {"async2_WsSetCallbacks",   Native_WsSetCallbacks},
    {"async2_WsSetHeader",      Native_WsSetHeader},
    {"async2_WsRemoveHeader",   Native_WsRemoveHeader},
    {"async2_WsClearHeaders",   Native_WsClearHeaders},
    {"async2_WsConnect",        Native_WsConnect},
    {"async2_WsSendText",       Native_WsSendText},
    {"async2_WsSendBinary",     Native_WsSendBinary},
    {"async2_WsSendPing",       Native_WsSendPing},
    {"async2_WsClose",          Native_WsClose},
    {"async2_WsSetOption",      Native_WsSetOption},
    {"async2_WsSendJson",       Native_WsSendJson},
    {"async2_WsSendMsgPack",    Native_WsSendMsgPack},
    {"async2_WsSetReconnect",   Native_WsSetReconnect},
    {"async2_WsGetReconnectCount", Native_WsGetReconnectCount},
    {"async2_WsGetState",       Native_WsGetState},
    {nullptr,                   nullptr},
};
