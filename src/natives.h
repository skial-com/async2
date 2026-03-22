#ifndef ASYNC2_NATIVES_H
#define ASYNC2_NATIVES_H

#include "smsdk_ext.h"
#include "handle_manager.h"
#include "http_request.h"
#include "linked_list.h"

extern HandleManager g_handle_manager;

#define GET_HTTP_REQUEST() \
    HttpRequest* request = g_handle_manager.GetHttpRequest(params[1]); \
    if (!request || request->in_event_thread) \
        return 2;

#define GET_LINKED_LIST() \
    LinkedList* list = g_handle_manager.GetLinkedList(params[1]); \
    if (!list) \
        return 0;

extern sp_nativeinfo_t g_HttpNatives[];
extern sp_nativeinfo_t g_JsonNatives[];
extern sp_nativeinfo_t g_CurlNatives[];
extern sp_nativeinfo_t g_MsgPackNatives[];
extern sp_nativeinfo_t g_TcpNatives[];
extern sp_nativeinfo_t g_UdpNatives[];
extern sp_nativeinfo_t g_DnsNatives[];
extern sp_nativeinfo_t g_CryptoNatives[];
extern sp_nativeinfo_t g_WsNatives[];
extern sp_nativeinfo_t g_LinkedListNatives[];
extern sp_nativeinfo_t g_UrlNatives[];
extern sp_nativeinfo_t g_TimeNatives[];
extern sp_nativeinfo_t g_HjsonNatives[];

void InitAnchoredClock();

#endif
