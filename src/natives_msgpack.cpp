#include <cstring>

#include <climits>

#include "smsdk_ext.h"
#include "extension.h"
#include "http_request.h"
#include "data_handle.h"
#include "msgpack_parse.h"
#include "msgpack_serialize.h"
#include "natives.h"

// async2_MsgPackParse(WebRequest request) -> Json handle
static cell_t Native_MsgPackParse(IPluginContext* pContext, const cell_t* params) {
    HttpRequest* request = g_handle_manager.GetHttpRequest(params[1]);
    if (!request || request->in_event_thread || request->response_body.empty())
        return 0;

    DataNode* parsed = MsgPackParse(
        reinterpret_cast<const uint8_t*>(request->response_body.data()),
        request->response_body.size());
    if (!parsed)
        return 0;

    DataHandle* handle = new DataHandle(parsed);
    int id = g_handle_manager.CreateHandle(static_cast<void*>(handle), HANDLE_JSON_VALUE);
    if (id == 0) {
        delete handle;
        return 0;
    }
    return id;
}

// async2_MsgPackParseBuffer(const char[] data, int length) -> Json handle
static cell_t Native_MsgPackParseBuffer(IPluginContext* pContext, const cell_t* params) {
    cell_t* data;
    if (pContext->LocalToPhysAddr(params[1], &data) != SP_ERROR_NONE)
        return 0;
    int length = params[2];
    if (length <= 0)
        return 0;

    DataNode* parsed = MsgPackParse(reinterpret_cast<const uint8_t*>(data), length);
    if (!parsed)
        return 0;

    DataHandle* handle = new DataHandle(parsed);
    int id = g_handle_manager.CreateHandle(static_cast<void*>(handle), HANDLE_JSON_VALUE);
    if (id == 0) {
        delete handle;
        return 0;
    }
    return id;
}

// async2_MsgPackSerialize(Json handle, char[] buffer, int maxlen) -> int (bytes written)
static cell_t Native_MsgPackSerialize(IPluginContext* pContext, const cell_t* params) {
    DataHandle* json = g_handle_manager.GetDataHandle(params[1]);
    if (!json || !json->node)
        return 0;

    std::vector<uint8_t> buf = MsgPackSerialize(*json->node);
    int maxlen = params[3];
    if (maxlen <= 0) return 0;

    size_t copy_len = buf.size();
    if (copy_len > static_cast<size_t>(maxlen))
        copy_len = static_cast<size_t>(maxlen);

    cell_t* out;
    if (pContext->LocalToPhysAddr(params[2], &out) != SP_ERROR_NONE)
        return 0;
    memcpy(out, buf.data(), copy_len);
    return copy_len;
}

// async2_SetBodyMsgPack(WebRequest request, Json handle) -> int
static cell_t Native_SetBodyMsgPack(IPluginContext* pContext, const cell_t* params) {
    HttpRequest* request = g_handle_manager.GetHttpRequest(params[1]);
    if (!request || request->in_event_thread)
        return 2;

    DataHandle* json = g_handle_manager.GetDataHandle(params[2]);
    if (!json || !json->node)
        return 2;

    DataNode* copy = json->node->DeepCopy();
    if (!copy)
        return 3;

    request->SetBodyNode(copy, BodyFormat::MSGPACK);
    request->SetHeader("Content-Type", "application/msgpack");
    return 0;
}

// async2_MsgPackSetMaxElements(int limit)
static cell_t Native_MsgPackSetMaxElements(IPluginContext* pContext, const cell_t* params) {
    int limit = params[1];
    MsgPackSetMaxElements(limit > 0 ? static_cast<size_t>(limit) : 1000000);
    return 0;
}

// async2_MsgPackGetMaxElements() -> int
static cell_t Native_MsgPackGetMaxElements(IPluginContext* pContext, const cell_t* params) {
    size_t val = MsgPackGetMaxElements();
    return static_cast<cell_t>(val > static_cast<size_t>(INT_MAX) ? INT_MAX : val);
}

sp_nativeinfo_t g_MsgPackNatives[] = {
    {"async2_MsgPackParse",             Native_MsgPackParse},
    {"async2_MsgPackParseBuffer",       Native_MsgPackParseBuffer},
    {"async2_MsgPackSerialize",         Native_MsgPackSerialize},
    {"async2_SetBodyMsgPack",           Native_SetBodyMsgPack},
    {"async2_MsgPackSetMaxElements",    Native_MsgPackSetMaxElements},
    {"async2_MsgPackGetMaxElements",    Native_MsgPackGetMaxElements},
    {nullptr,                           nullptr},
};
