#include "smsdk_ext.h"
#include "extension.h"
#include "event_loop.h"
#include "natives.h"

extern EventLoop g_event_loop;

// async2_DnsSetTimeout(int timeout_ms, int tries = 2)
static cell_t Native_DnsSetTimeout(IPluginContext* pContext, const cell_t* params) {
    auto* op = new DnsOp();
    op->type = DnsOpType::SET_TIMEOUT;
    op->timeout_ms = params[1];
    op->tries = params[2];
    g_event_loop.EnqueueDnsOp(op);
    return 0;
}

// async2_DnsCacheFlush()
static cell_t Native_DnsCacheFlush(IPluginContext* pContext, const cell_t* params) {
    g_event_loop.DnsCacheFlush();
    return 0;
}

// async2_DnsCacheStats(&entries, &memory)
static cell_t Native_DnsCacheStats(IPluginContext* pContext, const cell_t* params) {
    int count, memory;
    g_event_loop.DnsCacheStats(count, memory);

    cell_t* addr;
    pContext->LocalToPhysAddr(params[1], &addr);
    *addr = static_cast<cell_t>(count);
    pContext->LocalToPhysAddr(params[2], &addr);
    *addr = static_cast<cell_t>(memory);
    return 0;
}

// async2_DnsCacheSetTtl(int seconds)
static cell_t Native_DnsCacheSetTtl(IPluginContext* pContext, const cell_t* params) {
    g_event_loop.DnsCacheSetTtl(params[1]);
    return 0;
}

sp_nativeinfo_t g_DnsNatives[] = {
    {"async2_DnsSetTimeout",    Native_DnsSetTimeout},
    {"async2_DnsCacheFlush",    Native_DnsCacheFlush},
    {"async2_DnsCacheStats",    Native_DnsCacheStats},
    {"async2_DnsCacheSetTtl",   Native_DnsCacheSetTtl},
    {nullptr,                   nullptr},
};
