#include <cstring>

#include "smsdk_ext.h"
#include "extension.h"
#include "http_request.h"
#include "event_loop.h"
#include "natives.h"

extern EventLoop g_event_loop;

static cell_t Native_SetOptInt(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    return curl_easy_setopt(request->curl, static_cast<CURLoption>(params[2]), static_cast<long>(params[3]));
}

static cell_t Native_SetOptString(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    char* value;
    pContext->LocalToString(params[3], &value);
    return curl_easy_setopt(request->curl, static_cast<CURLoption>(params[2]), value);
}

static cell_t Native_GetInfoInt(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    long output;
    cell_t* sp_output;
    CURLcode curlcode = curl_easy_getinfo(request->curl, static_cast<CURLINFO>(params[2]), &output);
    if (curlcode == 0) {
        pContext->LocalToPhysAddr(params[3], &sp_output);
        *sp_output = static_cast<cell_t>(output);
    }
    return curlcode;
}

static cell_t Native_GetInfoString(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    char* result;
    CURLcode curlcode = curl_easy_getinfo(request->curl, static_cast<CURLINFO>(params[2]), &result);
    if (curlcode == 0 && result) {
        pContext->StringToLocal(params[3], params[4], result);
    }
    return curlcode;
}

static cell_t Native_GetErrorMessage(IPluginContext* pContext, const cell_t* params) {
    GET_HTTP_REQUEST()
    pContext->StringToLocal(params[2], params[3], request->curl_error_message);
    return 0;
}

static cell_t Native_SetMultiOpt(IPluginContext* pContext, const cell_t* params) {
    g_event_loop.ChangeCurlSetting(static_cast<CURLMoption>(params[1]), params[2]);
    return 0;
}

static cell_t Native_CurlErrorString(IPluginContext* pContext, const cell_t* params) {
    const char* error = curl_easy_strerror(static_cast<CURLcode>(params[1]));
    pContext->StringToLocal(params[2], params[3], error);
    return 0;
}

static cell_t Native_CurlVersion(IPluginContext* pContext, const cell_t* params) {
    char* version = curl_version();
    pContext->StringToLocal(params[1], params[2], version);
    return 0;
}

sp_nativeinfo_t g_CurlNatives[] = {
    {"async2_SetOptInt",        Native_SetOptInt},
    {"async2_SetOptString",     Native_SetOptString},
    {"async2_GetInfoInt",       Native_GetInfoInt},
    {"async2_GetInfoString",    Native_GetInfoString},
    {"async2_GetErrorMessage",  Native_GetErrorMessage},
    {"async2_SetMultiOpt",      Native_SetMultiOpt},
    {"async2_CurlErrorString",  Native_CurlErrorString},
    {"async2_CurlVersion",      Native_CurlVersion},
    {nullptr,                   nullptr},
};
