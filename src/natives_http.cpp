#include <cstring>
#include <cstdio>
#include <algorithm>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "smsdk_ext.h"
#include "extension.h"
#include "http_request.h"
#include "data_handle.h"
#include "event_loop.h"
#include "natives.h"

extern EventLoop g_event_loop;

static int g_default_timeout = 10000;

static cell_t Native_HttpNew(IPluginContext* pContext, const cell_t* params) {
    CURL* c = HttpRequest::AcquireCurl();
    if (!c)
        return 0;

    HttpRequest* request = new HttpRequest(c, pContext);
    int handle = g_handle_manager.CreateHandle(static_cast<void*>(request), HANDLE_HTTP_REQUEST, pContext);
    if (handle == 0) {
        delete request;
        return 0;
    }

    request->handle_id = handle;
    request->userdata = params[1];

    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, static_cast<long>(g_default_timeout));
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    static std::string user_agent;
    if (user_agent.empty()) {
        curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
        user_agent = std::string("async2/" SMEXT_CONF_VERSION " (curl/") + info->version + ")";
    }
    curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");

    return handle;
}

static cell_t Native_HttpClose(IPluginContext* pContext, const cell_t* params) {
    HttpRequest* request = g_handle_manager.GetHttpRequest(params[1]);
    if (!request)
        return 2;

    UntrackClientHandle(request->client_index, request->handle_id);
    if (request->in_event_thread) {
        request->handle_closed = true;
        g_handle_manager.MarkHandleClosed(params[1]);
        g_event_loop.CancelRequest(request);
    } else {
        g_handle_manager.FreeHandle(params[1]);
    }
    return 0;
}

static cell_t Native_HttpExecute(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    char* method;
    char* url;
    pContext->LocalToString(params[2], &method);
    pContext->LocalToString(params[3], &url);

    // Auto-uppercase method and store on request so pointer stays alive
    request->method = method;
    std::transform(request->method.begin(), request->method.end(), request->method.begin(), ::toupper);

    curl_easy_setopt(request->curl, CURLOPT_URL, url);
    curl_easy_setopt(request->curl, CURLOPT_CUSTOMREQUEST, request->method.c_str());

    // For POST/PUT/PATCH, enable POST mode if body is set
    if (!request->post_body.empty() || request->body_node) {
        curl_easy_setopt(request->curl, CURLOPT_POST, 1L);
    }

    request->callback_id = params[4];
    g_event_loop.EnqueueRequest(request);
    return 0;
}

static cell_t Native_SetBody(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    char* data;
    pContext->LocalToString(params[2], &data);
    int length = params[3];
    request->SetBody(data, length);
    return 0;
}

static cell_t Native_SetBodyString(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    char* str;
    pContext->LocalToString(params[2], &str);
    request->SetBodyString(str);
    return 0;
}

static cell_t Native_SetBodyJSON(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    DataHandle* json = g_handle_manager.GetDataHandle(params[2]);
    if (!json || !json->node)
        return 2;

    request->SetBodyNode(StealOrCopyNode(json), BodyFormat::JSON);
    request->SetHeader("Content-Type", "application/json");
    g_handle_manager.FreeHandle(params[2]);
    return 0;
}

static cell_t Native_SetCompression(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    request->compress_body = params[2] != 0;
    return 0;
}

static cell_t Native_SetHeader(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    char* key;
    char* value;
    pContext->LocalToString(params[2], &key);
    pContext->LocalToString(params[3], &value);

    request->SetHeader(key, value);
    return 0;
}

static cell_t Native_RemoveHeader(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    char* key;
    pContext->LocalToString(params[2], &key);

    request->RemoveHeader(key);
    return 0;
}

static cell_t Native_ClearHeaders(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    request->ClearHeaders();
    return 0;
}

static cell_t Native_GetString(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    char* buffer;
    pContext->LocalToString(params[2], &buffer);
    if (params[3] <= 0) return 0;
    size_t buffer_size = static_cast<size_t>(params[3]);
    size_t data_size = request->response_body.size();
    size_t max_copy = buffer_size - 1;
    size_t copy_size = data_size < max_copy ? data_size : max_copy;

    memcpy(buffer, request->response_body.data(), copy_size);
    buffer[copy_size] = 0;

    return 0;
}

static cell_t Native_GetRawData(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()

    char* buffer;
    pContext->LocalToString(params[2], &buffer);
    if (params[3] <= 0) return 0;
    size_t buffer_size = static_cast<size_t>(params[3]);
    size_t data_size = request->response_body.size();
    size_t copy_size = buffer_size < data_size ? buffer_size : data_size;

    memcpy(buffer, request->response_body.data(), copy_size);
    return static_cast<cell_t>(copy_size);
}

static cell_t Native_GetResponseLength(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    return static_cast<cell_t>(request->response_body.size());
}

static const std::string* FindResponseHeader(HttpRequest* request, IPluginContext* pContext, cell_t param) {
    char* name_raw;
    pContext->LocalToString(param, &name_raw);
    std::string name(name_raw);
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    auto it = request->response_headers.find(name);
    if (it == request->response_headers.end())
        return nullptr;
    return &it->second;
}

static cell_t Native_GetResponseHeader(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    const std::string* val = FindResponseHeader(request, pContext, params[2]);
    if (!val) return -1;
    pContext->StringToLocal(params[3], params[4], val->c_str());
    return val->size();
}

static cell_t Native_GetResponseHeaderLength(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    const std::string* val = FindResponseHeader(request, pContext, params[2]);
    if (!val) return -1;
    return val->size();
}

static cell_t Native_SetRetry(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    request->max_retries = params[2];
    if (params[0] >= 3) request->retry_delay_ms = params[3];
    if (params[0] >= 4) request->retry_backoff = sp_ctof(params[4]);
    if (params[0] >= 5) request->retry_max_delay_ms = params[5];
    if (params[0] >= 6 && params[6]) {
        request->log_retries = true;
        IFrameIterator* iter = pContext->CreateFrameIterator();
        for (; !iter->Done(); iter->Next()) {
            if (iter->IsScriptedFrame()) {
                request->log_caller_file = iter->FilePath() ? iter->FilePath() : "unknown";
                request->log_caller_line = iter->LineNumber();
                break;
            }
        }
        pContext->DestroyFrameIterator(iter);
    }
    return 0;
}

static cell_t Native_GetRetryCount(IPluginContext* pContext, const cell_t* params) {
    HttpRequest* request = g_handle_manager.GetHttpRequest(params[1]);
    if (!request)
        return -1;
    return request->retry_count;
}

static cell_t Native_GetHttpPoolStats(IPluginContext* pContext, const cell_t* params) {
    cell_t* active;
    cell_t* pending;
    cell_t* completed;
    cell_t* retries;
    if (pContext->LocalToPhysAddr(params[1], &active) != SP_ERROR_NONE ||
        pContext->LocalToPhysAddr(params[2], &pending) != SP_ERROR_NONE ||
        pContext->LocalToPhysAddr(params[3], &completed) != SP_ERROR_NONE ||
        pContext->LocalToPhysAddr(params[4], &retries) != SP_ERROR_NONE)
        return 0;
    *active = g_event_loop.stats_active.load(std::memory_order_relaxed);
    *pending = g_event_loop.stats_pending.load(std::memory_order_relaxed);
    *completed = g_event_loop.stats_completed.load(std::memory_order_relaxed);
    *retries = g_event_loop.stats_retries.load(std::memory_order_relaxed);
    return 0;
}

static cell_t Native_SetLogLevel(IPluginContext* pContext, const cell_t* params) {
    g_log_level.store(params[1], std::memory_order_relaxed);
    return 0;
}

static cell_t Native_SetOwner(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    int new_client = params[2];
    if (new_client != request->client_index) {
        UntrackClientHandle(request->client_index, request->handle_id);
        request->client_index = new_client;
        TrackClientHandle(new_client, request->handle_id);
    }
    return 0;
}

static cell_t Native_SetResponseType(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    int mode = params[2];
    if (mode < 0 || mode > 2)
        return 1;
    request->parse_mode = mode;
    return 0;
}

sp_nativeinfo_t g_HttpNatives[] = {
    {"async2_HttpNew",                  Native_HttpNew},
    {"async2_HttpClose",                Native_HttpClose},
    {"async2_HttpExecute",              Native_HttpExecute},
    {"async2_SetBody",                  Native_SetBody},
    {"async2_SetBodyString",            Native_SetBodyString},
    {"async2_SetBodyJSON",              Native_SetBodyJSON},
    {"async2_SetCompression",           Native_SetCompression},
    {"async2_SetHeader",                Native_SetHeader},
    {"async2_RemoveHeader",             Native_RemoveHeader},
    {"async2_ClearHeaders",             Native_ClearHeaders},
    {"async2_GetString",                Native_GetString},
    {"async2_GetRawData",               Native_GetRawData},
    {"async2_GetResponseLength",        Native_GetResponseLength},
    {"async2_GetResponseHeader",        Native_GetResponseHeader},
    {"async2_GetResponseHeaderLength",  Native_GetResponseHeaderLength},
    {"async2_SetRetry",                 Native_SetRetry},
    {"async2_GetRetryCount",            Native_GetRetryCount},
    {"async2_GetHttpPoolStats",         Native_GetHttpPoolStats},
    {"async2_SetLogLevel",              Native_SetLogLevel},
    {"async2_SetOwner",                 Native_SetOwner},
    {"async2_SetResponseType",          Native_SetResponseType},
    {nullptr,                           nullptr},
};
