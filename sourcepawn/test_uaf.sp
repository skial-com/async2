/**
 * test_uaf.sp — Stress test to trigger use-after-free bugs under ASan.
 *
 * Targets:
 *   1. OnRetryTimer double-push: cancel HTTP during retry wait
 *   2. WS close during reconnect: close while reconnect timer is pending
 *   3. WS rapid reconnect cycles: repeated close+connect on new handles
 *
 * Usage:
 *   sm_uaf_start          — start all tests (loops continuously)
 *   sm_uaf_stop           — stop
 *   sm_uaf_retry [N]      — run N HTTP retry+cancel iterations (default 100)
 *   sm_uaf_ws_reconnect [N] — run N WS reconnect+cancel iterations
 *
 * Requires: go run test/test_server.go
 */

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <async2>

#define TEST_URL      "http://127.0.0.1:8787"
#define TEST_WS_URL   "ws://127.0.0.1:8792"
// Unreachable endpoint — forces connection failure for retry/reconnect testing
#define DEAD_URL      "http://127.0.0.1:19999"
#define DEAD_WS_URL   "ws://127.0.0.1:19999"

// ============================================================================
// State
// ============================================================================

bool g_running;
Handle g_loopTimer;

// Retry+cancel test
int g_retryIter;
int g_retryMax;
int g_retryDone;
int g_retryCancelled;

// WS reconnect+cancel test
int g_wsReconnIter;
int g_wsReconnMax;
int g_wsReconnDone;
WsSocket g_wsReconnHandles[64];  // track active handles for cleanup

// WS rapid cycle test
int g_wsRapidIter;
int g_wsRapidMax;
int g_wsRapidDone;

public Plugin myinfo = {
    name = "UAF Stress Test",
    author = "async2",
    description = "Trigger use-after-free bugs under ASan",
    version = "1.0"
};

public void OnPluginStart() {
    RegServerCmd("sm_uaf_start", Cmd_Start, "Start all UAF tests (loops)");
    RegServerCmd("sm_uaf_stop", Cmd_Stop, "Stop UAF tests");
    RegServerCmd("sm_uaf_retry", Cmd_RetryCancel, "HTTP retry+cancel test [iterations]");
    RegServerCmd("sm_uaf_ws_reconnect", Cmd_WsReconnect, "WS reconnect+cancel test [iterations]");
    RegServerCmd("sm_uaf_ws_rapid", Cmd_WsRapid, "WS rapid close+new+connect cycles [iterations]");
}

public void OnPluginEnd() {
    StopAll();
}

void StopAll() {
    g_running = false;
    if (g_loopTimer != null) {
        delete g_loopTimer;
        g_loopTimer = null;
    }
    // Clean up any active WS reconnect handles
    for (int i = 0; i < sizeof(g_wsReconnHandles); i++) {
        if (g_wsReconnHandles[i] != null) {
            g_wsReconnHandles[i].Close();
            g_wsReconnHandles[i] = null;
        }
    }
}

// ============================================================================
// Commands
// ============================================================================

public Action Cmd_Start(int args) {
    StopAll();
    g_running = true;
    PrintToServer("[UAF] Starting all tests — looping continuously. sm_uaf_stop to end.");
    RunAllTests();
    g_loopTimer = CreateTimer(2.0, Timer_Loop, _, TIMER_REPEAT);
    return Plugin_Handled;
}

public Action Cmd_Stop(int args) {
    StopAll();
    PrintToServer("[UAF] Stopped. retry=%d/%d ws_reconn=%d/%d ws_rapid=%d/%d",
        g_retryDone, g_retryMax, g_wsReconnDone, g_wsReconnMax, g_wsRapidDone, g_wsRapidMax);
    return Plugin_Handled;
}

public Action Cmd_RetryCancel(int args) {
    int n = 100;
    if (args >= 1) {
        char buf[16];
        GetCmdArg(1, buf, sizeof(buf));
        n = StringToInt(buf);
        if (n <= 0) n = 100;
    }
    PrintToServer("[UAF] HTTP retry+cancel: %d iterations", n);
    StartRetryCancel(n);
    return Plugin_Handled;
}

public Action Cmd_WsReconnect(int args) {
    int n = 50;
    if (args >= 1) {
        char buf[16];
        GetCmdArg(1, buf, sizeof(buf));
        n = StringToInt(buf);
        if (n <= 0) n = 50;
    }
    PrintToServer("[UAF] WS reconnect+cancel: %d iterations", n);
    StartWsReconnect(n);
    return Plugin_Handled;
}

public Action Cmd_WsRapid(int args) {
    int n = 100;
    if (args >= 1) {
        char buf[16];
        GetCmdArg(1, buf, sizeof(buf));
        n = StringToInt(buf);
        if (n <= 0) n = 100;
    }
    PrintToServer("[UAF] WS rapid cycle: %d iterations", n);
    StartWsRapid(n);
    return Plugin_Handled;
}

public Action Timer_Loop(Handle timer) {
    if (!g_running) return Plugin_Stop;
    RunAllTests();
    return Plugin_Continue;
}

void RunAllTests() {
    StartRetryCancel(50);
    StartWsReconnect(20);
    StartWsRapid(50);
}

// ============================================================================
// Test 1: HTTP retry + cancel during retry wait
//
// The bug: OnRetryTimer fires (libuv timers phase), sees handle_closed,
// pushes to done_queue but doesn't erase from active_http_requests_.
// Then OnAsyncCancel fires (libuv poll phase), finds the request still
// in active_http_requests_, double-pushes. Game thread drains both →
// second OnCompletedGameThread is use-after-free.
//
// Strategy: Hit an endpoint that returns 500 with retry enabled.
// Cancel (HttpClose) immediately — the request will be in retry wait
// (short delay). If the timer and cancel race, ASan catches the UAF.
// ============================================================================

void StartRetryCancel(int n) {
    g_retryMax = n;
    g_retryIter = 0;
    g_retryDone = 0;
    g_retryCancelled = 0;

    // Fire a batch — don't flood, keep ~10 in flight
    int batch = n < 10 ? n : 10;
    for (int i = 0; i < batch; i++) {
        FireRetryRequest();
    }
}

void FireRetryRequest() {
    if (g_retryIter >= g_retryMax) return;
    g_retryIter++;

    WebRequest req = async2_HttpNew();
    // Short retry delay to maximize race window
    req.SetRetry(3, 50, 1.0, 100);
    req.Execute("GET", TEST_URL ... "/status/500", OnRetryCallback);

    // Cancel after a tiny delay — want to hit the retry wait window.
    // The 50ms retry delay means the request will be waiting when this fires.
    DataPack dp = new DataPack();
    dp.WriteCell(req);
    CreateTimer(0.03 + GetRandomFloat(0.0, 0.08), Timer_CancelRetry, dp);
}

public Action Timer_CancelRetry(Handle timer, DataPack dp) {
    dp.Reset();
    WebRequest req = dp.ReadCell();
    delete dp;

    int result = async2_HttpClose(req);
    if (result == 0 || result == 1) {
        g_retryCancelled++;
    }
    // result == 2 means already completed, close is a no-op (safe)

    // Fire next
    FireRetryRequest();
    return Plugin_Stop;
}

public void OnRetryCallback(WebRequest req, int curlcode, int httpcode, int size) {
    g_retryDone++;
    // curlcode 42 = CURLE_ABORTED_BY_CALLBACK (cancelled)
    // curlcode 0 + httpcode 500 = all retries exhausted
    // Both are expected.

    if (g_retryDone % 50 == 0) {
        PrintToServer("[UAF] retry: %d/%d done (%d cancelled)", g_retryDone, g_retryMax, g_retryCancelled);
    }
}

// ============================================================================
// Test 2: WS close during reconnect
//
// Strategy: Connect to an unreachable endpoint with auto-reconnect.
// The connection fails, reconnect timer starts. Close the handle
// during the reconnect wait. Tests the OnWsReconnectTimer handle_closed
// path and ProcessWsClose RECONNECTING state path.
// ============================================================================

void StartWsReconnect(int n) {
    g_wsReconnMax = n;
    g_wsReconnIter = 0;
    g_wsReconnDone = 0;

    int batch = n < 8 ? n : 8;
    for (int i = 0; i < batch; i++) {
        FireWsReconnect(i);
    }
}

void FireWsReconnect(int slot) {
    if (g_wsReconnIter >= g_wsReconnMax) return;
    g_wsReconnIter++;

    WsSocket ws = new WsSocket(slot);
    ws.SetCallbacks(OnWsReconn_Connect, OnWsReconn_Msg, OnWsReconn_Error, OnWsReconn_Close);
    ws.SetReconnect(-1, 100, 1.5, 500);  // infinite reconnect, short delays
    ws.SetOption(WS_CONNECT_TIMEOUT, 1);  // 1 second timeout
    ws.Connect(DEAD_WS_URL);

    if (slot < sizeof(g_wsReconnHandles))
        g_wsReconnHandles[slot] = ws;

    // Close after a random delay during reconnect
    DataPack dp = new DataPack();
    dp.WriteCell(ws);
    dp.WriteCell(slot);
    CreateTimer(0.1 + GetRandomFloat(0.0, 0.4), Timer_CancelWsReconn, dp);
}

public Action Timer_CancelWsReconn(Handle timer, DataPack dp) {
    dp.Reset();
    WsSocket ws = dp.ReadCell();
    int slot = dp.ReadCell();
    delete dp;

    ws.Close();
    if (slot < sizeof(g_wsReconnHandles))
        g_wsReconnHandles[slot] = null;
    return Plugin_Stop;
}

public void OnWsReconn_Connect(WsSocket ws, any slot) {
    // Shouldn't connect to dead endpoint
}

public void OnWsReconn_Msg(WsSocket ws, const char[] data, int len, bool bin, any slot) {
}

public void OnWsReconn_Error(WsSocket ws, int error, const char[] msg, any slot) {
    // Expected: connection refused
}

public void OnWsReconn_Close(WsSocket ws, int code, const char[] reason, any slot) {
    g_wsReconnDone++;
    if (slot < sizeof(g_wsReconnHandles))
        g_wsReconnHandles[view_as<int>(slot)] = null;

    if (g_wsReconnDone % 20 == 0) {
        PrintToServer("[UAF] ws_reconn: %d/%d done", g_wsReconnDone, g_wsReconnMax);
    }

    // Fire next in this slot
    if (g_running || g_wsReconnIter < g_wsReconnMax)
        FireWsReconnect(view_as<int>(slot));
}

// ============================================================================
// Test 3: WS rapid close + new handle + connect cycles
//
// Strategy: Create WS, connect to real server, immediately close and
// create a new one. Tests handle reuse, stale events in socket_done_queue,
// and FreeHandle racing with pending events.
// ============================================================================

void StartWsRapid(int n) {
    g_wsRapidMax = n;
    g_wsRapidIter = 0;
    g_wsRapidDone = 0;

    int batch = n < 10 ? n : 10;
    for (int i = 0; i < batch; i++) {
        FireWsRapid();
    }
}

void FireWsRapid() {
    if (g_wsRapidIter >= g_wsRapidMax) return;
    g_wsRapidIter++;

    WsSocket ws = new WsSocket(0);
    ws.SetCallbacks(OnWsRapid_Connect, OnWsRapid_Msg, OnWsRapid_Error, OnWsRapid_Close);
    ws.Connect(TEST_WS_URL);

    // Close immediately — before the connection even completes.
    // This tests the CONNECTING → handle_closed path in CheckCompletedJobs
    // and OnWsPollActivity.
    ws.Close();
}

public void OnWsRapid_Connect(WsSocket ws, any data) {
    // Might fire before Close() is processed on event thread
    ws.SendText("rapid test");
}

public void OnWsRapid_Msg(WsSocket ws, const char[] data, int len, bool bin, any data2) {
}

public void OnWsRapid_Error(WsSocket ws, int error, const char[] msg, any data) {
}

public void OnWsRapid_Close(WsSocket ws, int code, const char[] reason, any data) {
    g_wsRapidDone++;
    if (g_wsRapidDone % 50 == 0) {
        PrintToServer("[UAF] ws_rapid: %d/%d done", g_wsRapidDone, g_wsRapidMax);
    }
    FireWsRapid();
}
