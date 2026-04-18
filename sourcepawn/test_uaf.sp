/**
 * test_uaf.sp — Stress test to trigger use-after-free bugs under ASan.
 *
 * Targets:
 *   1. OnRetryTimer double-push: cancel HTTP during retry wait
 *   2. WS close during reconnect: close while reconnect timer is pending
 *   3. WS rapid reconnect cycles: repeated close+connect on new handles
 *   4. HTTP concurrent fire+cancel storm: many requests cancelled at random stages
 *   5. JSON body handoff + cancel: SetBodyJSON then cancel before event thread serializes
 *   6. JSON response parsing: GetJson/PostJsonResponse with concurrent cancel
 *   7. WS send+close race: SendText/SendJson during close sequence
 *   8. DNS cache concurrent lookups: many requests to different hosts simultaneously
 *
 * Usage:
 *   sm_uaf_start          — start all tests (loops continuously)
 *   sm_uaf_stop           — stop
 *   sm_uaf_retry [N]      — run N HTTP retry+cancel iterations (default 100)
 *   sm_uaf_ws_reconnect [N] — run N WS reconnect+cancel iterations
 *   sm_uaf_ws_rapid [N]   — run N WS rapid close+new+connect cycles
 *   sm_uaf_storm [N]      — run N HTTP concurrent fire+cancel iterations
 *   sm_uaf_json_body [N]  — run N JSON body handoff+cancel iterations
 *   sm_uaf_json_resp [N]  — run N JSON response parse+cancel iterations
 *   sm_uaf_ws_send [N]    — run N WS send+close race iterations
 *   sm_uaf_dns [N]        — run N DNS cache stress iterations
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

// Test 1: Retry+cancel
int g_retryIter;
int g_retryMax;
int g_retryDone;
int g_retryCancelled;

// Test 2: WS reconnect+cancel
int g_wsReconnIter;
int g_wsReconnMax;
int g_wsReconnDone;
WsSocket g_wsReconnHandles[64];

// Test 3: WS rapid cycle
int g_wsRapidIter;
int g_wsRapidMax;
int g_wsRapidDone;

// Test 4: HTTP storm
int g_stormIter;
int g_stormMax;
int g_stormDone;

// Test 5: JSON body+cancel
int g_jsonBodyIter;
int g_jsonBodyMax;
int g_jsonBodyDone;

// Test 6: JSON response+cancel
int g_jsonRespIter;
int g_jsonRespMax;
int g_jsonRespDone;

// Test 7: WS send+close
int g_wsSendIter;
int g_wsSendMax;
int g_wsSendDone;

// Test 8: DNS stress
int g_dnsIter;
int g_dnsMax;
int g_dnsDone;

public Plugin myinfo = {
    name = "UAF Stress Test",
    author = "async2",
    description = "Trigger use-after-free bugs under ASan",
    version = "2.0"
};

public void OnPluginStart() {
    RegServerCmd("sm_uaf_start", Cmd_Start, "Start all UAF tests (loops)");
    RegServerCmd("sm_uaf_stop", Cmd_Stop, "Stop UAF tests");
    RegServerCmd("sm_uaf_retry", Cmd_RetryCancel, "HTTP retry+cancel test [iterations]");
    RegServerCmd("sm_uaf_ws_reconnect", Cmd_WsReconnect, "WS reconnect+cancel test [iterations]");
    RegServerCmd("sm_uaf_ws_rapid", Cmd_WsRapid, "WS rapid close+new+connect cycles [iterations]");
    RegServerCmd("sm_uaf_storm", Cmd_Storm, "HTTP concurrent fire+cancel storm [iterations]");
    RegServerCmd("sm_uaf_json_body", Cmd_JsonBody, "JSON body handoff+cancel test [iterations]");
    RegServerCmd("sm_uaf_json_resp", Cmd_JsonResp, "JSON response parse+cancel test [iterations]");
    RegServerCmd("sm_uaf_ws_send", Cmd_WsSend, "WS send+close race test [iterations]");
    RegServerCmd("sm_uaf_dns", Cmd_Dns, "DNS cache stress test [iterations]");
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
    for (int i = 0; i < sizeof(g_wsReconnHandles); i++) {
        if (g_wsReconnHandles[i] != null) {
            g_wsReconnHandles[i].Close();
            g_wsReconnHandles[i] = null;
        }
    }
}

int ParseArg(int args, int def) {
    if (args >= 1) {
        char buf[16];
        GetCmdArg(1, buf, sizeof(buf));
        int n = StringToInt(buf);
        if (n > 0) return n;
    }
    return def;
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
    PrintToServer("[UAF] Stopped. retry=%d/%d storm=%d/%d json_body=%d/%d json_resp=%d/%d ws_reconn=%d/%d ws_rapid=%d/%d ws_send=%d/%d dns=%d/%d",
        g_retryDone, g_retryMax, g_stormDone, g_stormMax,
        g_jsonBodyDone, g_jsonBodyMax, g_jsonRespDone, g_jsonRespMax,
        g_wsReconnDone, g_wsReconnMax, g_wsRapidDone, g_wsRapidMax,
        g_wsSendDone, g_wsSendMax, g_dnsDone, g_dnsMax);
    return Plugin_Handled;
}

public Action Cmd_RetryCancel(int args) {
    int n = ParseArg(args, 100);
    PrintToServer("[UAF] HTTP retry+cancel: %d iterations", n);
    StartRetryCancel(n);
    return Plugin_Handled;
}

public Action Cmd_WsReconnect(int args) {
    int n = ParseArg(args, 50);
    PrintToServer("[UAF] WS reconnect+cancel: %d iterations", n);
    StartWsReconnect(n);
    return Plugin_Handled;
}

public Action Cmd_WsRapid(int args) {
    int n = ParseArg(args, 100);
    PrintToServer("[UAF] WS rapid cycle: %d iterations", n);
    StartWsRapid(n);
    return Plugin_Handled;
}

public Action Cmd_Storm(int args) {
    int n = ParseArg(args, 200);
    PrintToServer("[UAF] HTTP storm: %d iterations", n);
    StartStorm(n);
    return Plugin_Handled;
}

public Action Cmd_JsonBody(int args) {
    int n = ParseArg(args, 100);
    PrintToServer("[UAF] JSON body+cancel: %d iterations", n);
    StartJsonBody(n);
    return Plugin_Handled;
}

public Action Cmd_JsonResp(int args) {
    int n = ParseArg(args, 100);
    PrintToServer("[UAF] JSON response+cancel: %d iterations", n);
    StartJsonResp(n);
    return Plugin_Handled;
}

public Action Cmd_WsSend(int args) {
    int n = ParseArg(args, 100);
    PrintToServer("[UAF] WS send+close: %d iterations", n);
    StartWsSend(n);
    return Plugin_Handled;
}

public Action Cmd_Dns(int args) {
    int n = ParseArg(args, 200);
    PrintToServer("[UAF] DNS stress: %d iterations", n);
    StartDns(n);
    return Plugin_Handled;
}

public Action Timer_Loop(Handle timer) {
    if (!g_running) return Plugin_Stop;
    RunAllTests();
    return Plugin_Continue;
}

void RunAllTests() {
    StartRetryCancel(50);
    StartStorm(100);
    StartJsonBody(50);
    StartJsonResp(50);
    StartWsReconnect(20);
    StartWsRapid(50);
    StartWsSend(50);
    StartDns(100);
}

// ============================================================================
// Test 1: HTTP retry + cancel during retry wait
// ============================================================================

void StartRetryCancel(int n) {
    g_retryMax = n;
    g_retryIter = 0;
    g_retryDone = 0;
    g_retryCancelled = 0;

    int batch = n < 10 ? n : 10;
    for (int i = 0; i < batch; i++) {
        FireRetryRequest();
    }
}

void FireRetryRequest() {
    if (g_retryIter >= g_retryMax) return;
    g_retryIter++;

    WebRequest req = async2_HttpNew();
    req.SetRetry(3, 50, 1.0, 100);
    req.Execute("GET", TEST_URL ... "/status/500", OnRetryCallback);

    DataPack dp = new DataPack();
    dp.WriteCell(req);
    CreateTimer(0.03 + GetRandomFloat(0.0, 0.08), Timer_CancelRetry, dp);
}

public Action Timer_CancelRetry(Handle timer, DataPack dp) {
    dp.Reset();
    WebRequest req = dp.ReadCell();
    delete dp;

    async2_HttpClose(req);
    g_retryCancelled++;
    FireRetryRequest();
    return Plugin_Stop;
}

public void OnRetryCallback(WebRequest req, int curlcode, int httpcode, int size) {
    g_retryDone++;
    if (g_retryDone % 50 == 0)
        PrintToServer("[UAF] retry: %d/%d done (%d cancelled)", g_retryDone, g_retryMax, g_retryCancelled);
}

// ============================================================================
// Test 2: WS close during reconnect
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
    ws.SetReconnect(-1, 100, 1.5, 500);
    ws.SetOption(WS_CONNECT_TIMEOUT, 1);
    ws.Connect(DEAD_WS_URL);

    if (slot < sizeof(g_wsReconnHandles))
        g_wsReconnHandles[slot] = ws;

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

public void OnWsReconn_Connect(WsSocket ws, any slot) {}
public void OnWsReconn_Msg(WsSocket ws, const char[] data, int len, bool bin, any slot) {}
public void OnWsReconn_Error(WsSocket ws, int error, const char[] msg, any slot) {}

public void OnWsReconn_Close(WsSocket ws, int code, const char[] reason, any slot) {
    g_wsReconnDone++;
    if (slot < sizeof(g_wsReconnHandles))
        g_wsReconnHandles[view_as<int>(slot)] = null;

    if (g_wsReconnDone % 20 == 0)
        PrintToServer("[UAF] ws_reconn: %d/%d done", g_wsReconnDone, g_wsReconnMax);

    if (g_running || g_wsReconnIter < g_wsReconnMax)
        FireWsReconnect(view_as<int>(slot));
}

// ============================================================================
// Test 3: WS rapid close + new handle + connect cycles
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
    ws.Close();
}

public void OnWsRapid_Connect(WsSocket ws, any data) {
    ws.SendText("rapid test");
}

public void OnWsRapid_Msg(WsSocket ws, const char[] data, int len, bool bin, any data2) {}
public void OnWsRapid_Error(WsSocket ws, int error, const char[] msg, any data) {}

public void OnWsRapid_Close(WsSocket ws, int code, const char[] reason, any data) {
    g_wsRapidDone++;
    if (g_wsRapidDone % 50 == 0)
        PrintToServer("[UAF] ws_rapid: %d/%d done", g_wsRapidDone, g_wsRapidMax);
    FireWsRapid();
}

// ============================================================================
// Test 4: HTTP concurrent fire+cancel storm
//
// Many requests to various endpoints, cancelled at random times.
// Tests the event thread completing requests while the game thread
// is simultaneously cancelling different ones.
// ============================================================================

void StartStorm(int n) {
    g_stormMax = n;
    g_stormIter = 0;
    g_stormDone = 0;

    int batch = n < 20 ? n : 20;
    for (int i = 0; i < batch; i++) {
        FireStormRequest();
    }
}

void FireStormRequest() {
    if (g_stormIter >= g_stormMax) return;
    g_stormIter++;

    WebRequest req = async2_HttpNew();

    // Mix of endpoints: fast, slow, error, echo
    char url[128];
    int r = GetRandomInt(0, 3);
    switch (r) {
        case 0: FormatEx(url, sizeof(url), "%s/json", TEST_URL);
        case 1: FormatEx(url, sizeof(url), "%s/slow", TEST_URL);
        case 2: FormatEx(url, sizeof(url), "%s/status/503", TEST_URL);
        case 3: FormatEx(url, sizeof(url), "%s/echo", TEST_URL);
    }

    if (r == 3) {
        req.SetBodyString("storm test payload body data");
        req.Execute("POST", url, OnStormCallback);
    } else {
        req.Execute("GET", url, OnStormCallback);
    }

    // Cancel ~50% at random delay, let ~50% complete naturally
    if (GetRandomFloat(0.0, 1.0) > 0.5) {
        DataPack dp = new DataPack();
        dp.WriteCell(req);
        CreateTimer(GetRandomFloat(0.0, 0.15), Timer_CancelStorm, dp);
    }
}

public Action Timer_CancelStorm(Handle timer, DataPack dp) {
    dp.Reset();
    WebRequest req = dp.ReadCell();
    delete dp;
    async2_HttpClose(req);
    return Plugin_Stop;
}

public void OnStormCallback(WebRequest req, int curlcode, int httpcode, int size) {
    g_stormDone++;
    if (g_stormDone % 50 == 0)
        PrintToServer("[UAF] storm: %d/%d done", g_stormDone, g_stormMax);
    FireStormRequest();
}

// ============================================================================
// Test 5: JSON body handoff + cancel
//
// Create a Json body, SetBodyJSON (consumes handle, event thread serializes),
// then cancel before the event thread picks it up.
// Tests body_node lifetime and early-cancel Decref path.
// ============================================================================

void StartJsonBody(int n) {
    g_jsonBodyMax = n;
    g_jsonBodyIter = 0;
    g_jsonBodyDone = 0;

    int batch = n < 10 ? n : 10;
    for (int i = 0; i < batch; i++) {
        FireJsonBodyRequest();
    }
}

void FireJsonBodyRequest() {
    if (g_jsonBodyIter >= g_jsonBodyMax) return;
    g_jsonBodyIter++;

    // Build a non-trivial JSON tree
    Json arr = Json.CreateArray();
    for (int i = 0; i < 10; i++) {
        Json item = Json.CreateObject();
        item.SetInt("id", i);
        item.SetString("name", "stress test item with some data padding");
        item.SetBool("active", true);
        arr.ArrayAppendObject(item);  // consumes item
    }

    Json body = Json.CreateObject();
    body.SetString("test", "json_body_handoff");
    body.SetInt("iteration", g_jsonBodyIter);
    body.SetObject("items", arr);  // consumes arr

    WebRequest req = async2_HttpNew();
    req.SetBodyJSON(body);  // consumes body, event thread will serialize
    req.Execute("POST", TEST_URL ... "/echo", OnJsonBodyCallback);

    // Cancel immediately ~70% of the time to hit the pre-pickup cancel path
    if (GetRandomFloat(0.0, 1.0) > 0.3) {
        async2_HttpClose(req);
    } else {
        // Cancel with small delay for the rest
        DataPack dp = new DataPack();
        dp.WriteCell(req);
        CreateTimer(GetRandomFloat(0.0, 0.05), Timer_CancelJsonBody, dp);
    }
}

public Action Timer_CancelJsonBody(Handle timer, DataPack dp) {
    dp.Reset();
    WebRequest req = dp.ReadCell();
    delete dp;
    async2_HttpClose(req);
    return Plugin_Stop;
}

public void OnJsonBodyCallback(WebRequest req, int curlcode, int httpcode, int size) {
    g_jsonBodyDone++;
    if (g_jsonBodyDone % 50 == 0)
        PrintToServer("[UAF] json_body: %d/%d done", g_jsonBodyDone, g_jsonBodyMax);
    FireJsonBodyRequest();
}

// ============================================================================
// Test 6: JSON response parsing + cancel
//
// GetJson requests parse the response into a DataNode on the event thread.
// Cancel some while the event thread is mid-parse or after parse but before
// game thread callback.
// ============================================================================

void StartJsonResp(int n) {
    g_jsonRespMax = n;
    g_jsonRespIter = 0;
    g_jsonRespDone = 0;

    int batch = n < 10 ? n : 10;
    for (int i = 0; i < batch; i++) {
        FireJsonRespRequest();
    }
}

void FireJsonRespRequest() {
    if (g_jsonRespIter >= g_jsonRespMax) return;
    g_jsonRespIter++;

    WebRequest req = WebRequest.GetJson(TEST_URL ... "/json", OnJsonRespCallback);

    // Cancel ~40% immediately, ~30% with delay, let ~30% complete
    float roll = GetRandomFloat(0.0, 1.0);
    if (roll < 0.4) {
        async2_HttpClose(req);
    } else if (roll < 0.7) {
        DataPack dp = new DataPack();
        dp.WriteCell(req);
        CreateTimer(GetRandomFloat(0.0, 0.1), Timer_CancelJsonResp, dp);
    }
}

public Action Timer_CancelJsonResp(Handle timer, DataPack dp) {
    dp.Reset();
    WebRequest req = dp.ReadCell();
    delete dp;
    async2_HttpClose(req);
    return Plugin_Stop;
}

public void OnJsonRespCallback(WebRequest req, int curlcode, int httpcode, Json data) {
    g_jsonRespDone++;

    // Actually use the parsed data to ensure it's valid
    if (data != null) {
        char buf[256];
        data.Serialize(buf, sizeof(buf));
        data.Close();
    }

    if (g_jsonRespDone % 50 == 0)
        PrintToServer("[UAF] json_resp: %d/%d done", g_jsonRespDone, g_jsonRespMax);
    FireJsonRespRequest();
}

// ============================================================================
// Test 7: WS send+close race
//
// Connect to real WS server, rapidly send messages then close.
// Tests WsSend queueing on game thread while event thread processes close.
// Also tests SendJson which consumes a DataNode handle.
// ============================================================================

void StartWsSend(int n) {
    g_wsSendMax = n;
    g_wsSendIter = 0;
    g_wsSendDone = 0;

    int batch = n < 10 ? n : 10;
    for (int i = 0; i < batch; i++) {
        FireWsSend();
    }
}

void FireWsSend() {
    if (g_wsSendIter >= g_wsSendMax) return;
    g_wsSendIter++;

    WsSocket ws = new WsSocket(0);
    ws.SetCallbacks(OnWsSend_Connect, OnWsSend_Msg, OnWsSend_Error, OnWsSend_Close);
    ws.Connect(TEST_WS_URL);
}

public void OnWsSend_Connect(WsSocket ws, any data) {
    // Blast messages then close — race send ops with close
    for (int i = 0; i < 5; i++) {
        ws.SendText("send+close race test message");
    }

    // SendJson consumes the handle — tests DataNode handoff during close
    Json j = Json.CreateObject();
    j.SetString("test", "ws_send_close_race");
    j.SetInt("value", GetRandomInt(0, 9999));
    ws.SendJson(j);  // consumes j

    // Close immediately after sends — event thread may still be processing sends
    ws.Close();
}

public void OnWsSend_Msg(WsSocket ws, const char[] d, int len, bool bin, any data) {}
public void OnWsSend_Error(WsSocket ws, int error, const char[] msg, any data) {}

public void OnWsSend_Close(WsSocket ws, int code, const char[] reason, any data) {
    g_wsSendDone++;
    if (g_wsSendDone % 50 == 0)
        PrintToServer("[UAF] ws_send: %d/%d done", g_wsSendDone, g_wsSendMax);
    FireWsSend();
}

// ============================================================================
// Test 8: DNS cache stress
//
// Many concurrent requests to different hostnames to exercise the DNS
// resolver's cache and concurrent lookup paths. Uses both real and
// unreachable hosts.
// ============================================================================

char g_dnsHosts[][] = {
    "http://127.0.0.1:8787/json",
    "http://localhost:8787/json",
    "http://127.0.0.1:8787/echo",
    "http://localhost:8787/echo",
    "http://127.0.0.1:8787/status/200",
    "http://localhost:8787/status/200",
    "http://127.0.0.1:19999/timeout",       // unreachable
    "http://localhost:19999/timeout"         // unreachable
};

void StartDns(int n) {
    g_dnsMax = n;
    g_dnsIter = 0;
    g_dnsDone = 0;

    int batch = n < 20 ? n : 20;
    for (int i = 0; i < batch; i++) {
        FireDnsRequest();
    }
}

void FireDnsRequest() {
    if (g_dnsIter >= g_dnsMax) return;
    g_dnsIter++;

    int idx = GetRandomInt(0, sizeof(g_dnsHosts) - 1);

    WebRequest req = async2_HttpNew();
    req.SetOptInt(CURLOPT_CONNECTTIMEOUT, 2);
    req.SetOptInt(CURLOPT_TIMEOUT, 3);
    req.Execute("GET", g_dnsHosts[idx], OnDnsCallback);

    // Cancel ~30%
    if (GetRandomFloat(0.0, 1.0) < 0.3) {
        async2_HttpClose(req);
    }
}

public void OnDnsCallback(WebRequest req, int curlcode, int httpcode, int size) {
    g_dnsDone++;
    if (g_dnsDone % 50 == 0)
        PrintToServer("[UAF] dns: %d/%d done", g_dnsDone, g_dnsMax);
    FireDnsRequest();
}
