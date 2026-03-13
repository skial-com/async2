#include <uv.h>
#include "smsdk_ext.h"
#include "natives.h"

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

sp_nativeinfo_t g_TimeNatives[] = {
    {"async2_GetTime", Native_GetTime},
    {nullptr,          nullptr},
};
