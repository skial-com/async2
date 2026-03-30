#include <cstdio>
#include <cstring>
#include <uv.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "smsdk_ext.h"
#include "natives.h"

// ===== Version =============================================================

// Bump when the include needs to detect new runtime behavior.
// Also referenced by extension.cpp for "sm async2 version" output.
int g_async2_api_version = 5;

static cell_t Native_GetVersion(IPluginContext*, const cell_t*) {
    return g_async2_api_version;
}

// ===== Time ================================================================

// Anchored clock state (game thread only — no synchronization needed)
static int64_t g_clock_offset;      // realtime_ms - monotonic_ms at anchor
static int64_t g_last_returned;     // monotonicity guard
static int64_t g_last_sync_mono;    // monotonic ms at last sync

static const int64_t SYNC_INTERVAL_MS = 10000;   // re-check every 10s
static const int64_t SNAP_THRESHOLD_MS = 500;     // snap if drift > 500ms

static int64_t GetMonotonicMs() {
    uv_timespec64_t ts;
    uv_clock_gettime(UV_CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int64_t GetRealtimeMs() {
    uv_timespec64_t ts;
    uv_clock_gettime(UV_CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void InitAnchoredClock() {
    int64_t mono = GetMonotonicMs();
    int64_t real = GetRealtimeMs();
    g_clock_offset = real - mono;
    g_last_returned = real;
    g_last_sync_mono = mono;
}

static int64_t GetAnchoredTimeMs() {
    int64_t mono = GetMonotonicMs();
    int64_t candidate = mono + g_clock_offset;

    if (mono - g_last_sync_mono >= SYNC_INTERVAL_MS) {
        g_last_sync_mono = mono;
        int64_t real = GetRealtimeMs();
        int64_t drift = real - candidate;

        if (drift > SNAP_THRESHOLD_MS || drift < -SNAP_THRESHOLD_MS) {
            // Big change (NTP step, suspend/resume) — snap
            g_clock_offset = real - mono;
            candidate = real;
            g_last_returned = candidate;
        } else if (drift > 0) {
            // Behind wall clock — re-anchor forward (safe, monotonic)
            g_clock_offset = real - mono;
            candidate = real;
        }
        // Small ahead: do nothing, max() below handles it
    }

    if (candidate > g_last_returned) {
        g_last_returned = candidate;
    }
    return g_last_returned;
}

// async2_GetTime(int result[2])
static cell_t Native_GetTime(IPluginContext* pContext, const cell_t* params) {
    cell_t* result;
    pContext->LocalToPhysAddr(params[1], &result);

    int64_t ms = GetAnchoredTimeMs();
    memcpy(result, &ms, sizeof(int64_t));
    return 0;
}

// ===== Memory / diagnostics ================================================

static cell_t Native_GetRss(IPluginContext* pContext, const cell_t* params) {
    size_t rss;
    if (uv_resident_set_memory(&rss) != 0) return 0;
    return static_cast<cell_t>(rss / 1024);
}

static cell_t Native_GetVss(IPluginContext* pContext, const cell_t* params) {
#ifdef _WIN32
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0;
    return static_cast<cell_t>((ms.ullTotalVirtual - ms.ullAvailVirtual) / 1024);
#else
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long pages;
    int ret = fscanf(f, "%lu", &pages);
    fclose(f);
    if (ret != 1) return 0;
    return static_cast<cell_t>(pages * (sysconf(_SC_PAGESIZE) / 1024));
#endif
}

static cell_t Native_GetHandleCount(IPluginContext* pContext, const cell_t* params) {
    return static_cast<cell_t>(g_handle_manager.GetHandles().size());
}

// async2_SetHandlePlugin(int handle, Handle plugin = INVALID_HANDLE) -> int
static cell_t Native_SetHandlePlugin(IPluginContext* pContext, const cell_t* params) {
    IPluginContext* target = pContext;

    if (params[0] >= 2 && params[2] != 0) {
        HandleError err;
        IPlugin* plugin = plsys->PluginFromHandle(params[2], &err);
        if (!plugin || err != HandleError_None)
            return 0;
        target = plugin->GetBaseContext();
    }

    return g_handle_manager.TransferHandle(params[1], target) ? 1 : 0;
}

// ===== Native table ========================================================

sp_nativeinfo_t g_UtilsNatives[] = {
    {"async2_GetVersion",         Native_GetVersion},
    {"async2_GetTime",            Native_GetTime},
    {"async2_GetRss",             Native_GetRss},
    {"async2_GetVss",             Native_GetVss},
    {"async2_GetHandleCount",     Native_GetHandleCount},
    {"async2_SetHandlePlugin",    Native_SetHandlePlugin},
    {nullptr,                     nullptr},
};
