// WebSocket Tests (async, requires test server on port 8792)

void Test_WS_TextEcho() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(1);
    ws.SetCallbacks(OnWsTestConnect, OnWsTestMessage, OnWsTestError, OnWsTestClose);
    ws.Connect("ws://127.0.0.1:8792");
}

void Test_WS_BinaryEcho() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(2);
    ws.SetCallbacks(OnWsBinConnect, OnWsBinMessage, OnWsTestError, OnWsTestClose);
    ws.Connect("ws://127.0.0.1:8792");
}

void Test_WS_CloseHandshake() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(3);
    ws.SetCallbacks(OnWsCloseConnect, OnWsTestMessage, OnWsTestError, OnWsTestClose);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsTestConnect(WsSocket socket, any userdata) {
    Assert(true, "WS connected");
    socket.SendText("hello websocket");
}

public void OnWsTestMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    if (userdata == 1) {
        char expected[] = "hello websocket";
        bool match = (length == 15);
        if (match) {
            for (int i = 0; i < 15; i++) {
                if (data[i] != expected[i]) { match = false; break; }
            }
        }
        Assert(match, "WS text echo data matches");
        Assert(!isBinary, "WS text echo is text");
        socket.Close();
    }
}

public void OnWsBinConnect(WsSocket socket, any userdata) {
    char bindata[4];
    bindata[0] = 0x01;
    bindata[1] = 0x02;
    bindata[2] = 0x03;
    bindata[3] = 0x04;
    socket.SendBinary(bindata, 4);
}

public void OnWsBinMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    Assert(isBinary, "WS binary echo is binary");
    AssertEq(length, 4, "WS binary echo length");
    bool match = (data[0] == 0x01 && data[1] == 0x02 && data[2] == 0x03 && data[3] == 0x04);
    Assert(match, "WS binary echo data matches");
    socket.Close();
}

public void OnWsCloseConnect(WsSocket socket, any userdata) {
    Assert(true, "WS close handshake connected");
    socket.Close(1000, "test close");
}

public void OnWsTestError(WsSocket socket, int error, const char[] msg, any userdata) {
    g_failed++;
    PrintToServer("[FAIL] WS error: %d %s (userdata=%d)", error, msg, userdata);
    g_ws_test_pending--;
}

public void OnWsTestClose(WsSocket socket, int code, const char[] reason, any userdata) {
    g_ws_test_pending--;
    if (g_ws_test_pending == 0)
        PrintToServer("  WS tests done.");
    MaybeFinishAll();
}

// --- Reconnect Tests ---

bool g_ws_reconnect_first;
bool g_ws_reconnect_notified;

// Test reconnect after server drops connection.
// Flow: connect → send "force_close" → server drops TCP → reconnect → onConnect fires again → echo → close
void Test_WS_Reconnect() {
    g_ws_test_pending++;
    g_ws_reconnect_first = true;
    g_ws_reconnect_notified = false;
    WsSocket ws = new WsSocket(10);
    ws.SetCallbacks(OnWsReconnectConnect, OnWsReconnectMessage, OnWsReconnectError, OnWsReconnectClose);
    ws.SetReconnect(3, 100);  // 3 attempts, 100ms initial delay
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsReconnectConnect(WsSocket socket, any userdata) {
    if (g_ws_reconnect_first) {
        g_ws_reconnect_first = false;
        // First connect — trigger server drop
        socket.SendText("force_close");
    } else {
        // Reconnected — verify it works
        Assert(true, "WS reconnect succeeded");
        Assert(g_ws_reconnect_notified, "WS clean TCP drop fired WS_ERROR_RECONNECTING");
        socket.SendText("hello after reconnect");
    }
}

public void OnWsReconnectMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    char expected[] = "hello after reconnect";
    bool match = (length == 21);
    if (!match) {
        PrintToServer("  [DEBUG] WS reconnect echo: got length %d (expected 21), data='%s'", length, data);
    } else {
        for (int i = 0; i < 21; i++) {
            if (data[i] != expected[i]) { match = false; break; }
        }
    }
    Assert(match, "WS reconnect echo matches");
    socket.Close();
}

public void OnWsReconnectError(WsSocket socket, int error, const char[] msg, any userdata) {
    // Errors during reconnect attempts are expected, don't fail
    if (error == WS_ERROR_RECONNECTING)
        g_ws_reconnect_notified = true;
}

public void OnWsReconnectClose(WsSocket socket, int code, const char[] reason, any userdata) {
    g_ws_test_pending--;
    MaybeFinishAll();
}

// Test reconnect exhaustion: connect to bad port, all attempts fail, onClose fires.
int g_ws_exhausted_errors;
int g_ws_exhausted_reconnecting;

void Test_WS_Reconnect_Exhausted() {
    g_ws_test_pending++;
    g_ws_exhausted_errors = 0;
    g_ws_exhausted_reconnecting = 0;
    WsSocket ws = new WsSocket(11);
    ws.SetCallbacks(OnWsExhaustedConnect, OnWsTestMessage, OnWsExhaustedError, OnWsExhaustedClose);
    ws.SetReconnect(2, 100);  // 2 attempts, 100ms delay
    ws.Connect("ws://127.0.0.1:1");  // bad port
}

public void OnWsExhaustedConnect(WsSocket socket, any userdata) {
    g_failed++;
    PrintToServer("[FAIL] WS reconnect exhausted: should not connect");
}

public void OnWsExhaustedError(WsSocket socket, int error, const char[] msg, any userdata) {
    if (error == WS_ERROR_RECONNECTING)
        g_ws_exhausted_reconnecting++;
    else
        g_ws_exhausted_errors++;
}

public void OnWsExhaustedClose(WsSocket socket, int code, const char[] reason, any userdata) {
    // 3 errors: 1 initial + 2 reconnects
    AssertEq(g_ws_exhausted_errors, 3, "WS reconnect exhausted error count");
    // A retry was scheduled after the 1st and 2nd failures, not after the last
    AssertEq(g_ws_exhausted_reconnecting, 2, "WS reconnect exhausted WS_ERROR_RECONNECTING count");
    g_ws_test_pending--;
    MaybeFinishAll();
}

// --- Header Tests ---
// Tests SetHeader, RemoveHeader, ClearHeaders. Server echoes back upgrade headers
// when it receives a "get_headers" text message.

void Test_WS_SetHeader() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(30);
    ws.SetCallbacks(OnWsHeaderConnect, OnWsHeaderMessage, OnWsTestError, OnWsTestClose);
    ws.SetHeader("X-Custom-One", "hello");
    ws.SetHeader("X-Custom-Two", "world");
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsHeaderConnect(WsSocket socket, any userdata) {
    socket.SendText("get_headers");
}

public void OnWsHeaderMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    // Server returns JSON like {"x-custom-one":"hello","x-custom-two":"world",...}
    Json json = async2_JsonParseString(data);
    Assert(json != null, "WS header response is valid JSON");

    char val[128];
    json.GetString("x-custom-one", val, sizeof(val));
    AssertStrEq(val, "hello", "WS SetHeader x-custom-one");

    json.GetString("x-custom-two", val, sizeof(val));
    AssertStrEq(val, "world", "WS SetHeader x-custom-two");

    json.Close();
    socket.Close();
}

void Test_WS_ReplaceHeader() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(31);
    ws.SetCallbacks(OnWsReplaceConnect, OnWsReplaceMessage, OnWsTestError, OnWsTestClose);
    ws.SetHeader("X-Token", "old-value");
    ws.SetHeader("X-Token", "new-value");  // should replace
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsReplaceConnect(WsSocket socket, any userdata) {
    socket.SendText("get_headers");
}

public void OnWsReplaceMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    Json json = async2_JsonParseString(data);
    Assert(json != null, "WS replace header response is valid JSON");

    char val[128];
    json.GetString("x-token", val, sizeof(val));
    AssertStrEq(val, "new-value", "WS SetHeader replaces existing key");

    json.Close();
    socket.Close();
}

void Test_WS_RemoveHeader() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(32);
    ws.SetCallbacks(OnWsRemoveConnect, OnWsRemoveMessage, OnWsTestError, OnWsTestClose);
    ws.SetHeader("X-Keep", "yes");
    ws.SetHeader("X-Remove", "bye");
    ws.RemoveHeader("X-Remove");
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsRemoveConnect(WsSocket socket, any userdata) {
    socket.SendText("get_headers");
}

public void OnWsRemoveMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    Json json = async2_JsonParseString(data);
    Assert(json != null, "WS remove header response is valid JSON");

    char val[128];
    json.GetString("x-keep", val, sizeof(val));
    AssertStrEq(val, "yes", "WS RemoveHeader keeps other headers");

    // x-remove should not exist — GetString returns empty on missing key
    json.GetString("x-remove", val, sizeof(val));
    AssertStrEq(val, "", "WS RemoveHeader removes target header");

    json.Close();
    socket.Close();
}

void Test_WS_ClearHeaders() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(33);
    ws.SetCallbacks(OnWsClearConnect, OnWsClearMessage, OnWsTestError, OnWsTestClose);
    ws.SetHeader("X-Gone", "disappear");
    ws.ClearHeaders();
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsClearConnect(WsSocket socket, any userdata) {
    socket.SendText("get_headers");
}

public void OnWsClearMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    Json json = async2_JsonParseString(data);
    Assert(json != null, "WS clear headers response is valid JSON");

    char val[128];
    json.GetString("x-gone", val, sizeof(val));
    AssertStrEq(val, "", "WS ClearHeaders removes all custom headers");

    json.Close();
    socket.Close();
}

void Test_WS_CaseInsensitiveHeader() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(34);
    ws.SetCallbacks(OnWsCaseConnect, OnWsCaseMessage, OnWsTestError, OnWsTestClose);
    ws.SetHeader("X-Case-Test", "first");
    ws.SetHeader("x-case-test", "second");  // same key, different case — should replace
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsCaseConnect(WsSocket socket, any userdata) {
    socket.SendText("get_headers");
}

public void OnWsCaseMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    Json json = async2_JsonParseString(data);
    Assert(json != null, "WS case insensitive header response is valid JSON");

    char val[128];
    json.GetString("x-case-test", val, sizeof(val));
    AssertStrEq(val, "second", "WS SetHeader is case-insensitive (replaces)");

    json.Close();
    socket.Close();
}

// --- Parse Failure Test ---
// Sends invalid JSON with SetParseMessages(1). The echo server returns it as-is,
// parse fails, and onError should fire with WS_ERROR_PARSE_FAILED (-1).

bool g_ws_parse_fail_got_error;

void Test_WS_ParseFailed() {
    g_ws_test_pending++;
    g_ws_parse_fail_got_error = false;
    WsSocket ws = new WsSocket(20);
    ws.SetCallbacks(OnWsParseFailConnect, OnWsParseFailMessage, OnWsParseFailError, OnWsParseFailClose);
    ws.SetParseMessages(1);  // JSON parse mode
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsParseFailConnect(WsSocket socket, any userdata) {
    // Send something that is NOT valid JSON
    socket.SendText("this is not json {{");
}

public void OnWsParseFailMessage(WsSocket socket, Json data, any userdata) {
    // Should NOT be called — parse failure goes to onError
    g_failed++;
    PrintToServer("[FAIL] WS parse fail: onMessage called (expected onError)");
    if (data != null) data.Close();
    socket.Close();
}

public void OnWsParseFailError(WsSocket socket, int error, const char[] msg, any userdata) {
    if (error == WS_ERROR_PARSE_FAILED) {
        g_ws_parse_fail_got_error = true;
        Assert(true, "WS parse failure fires onError with WS_ERROR_PARSE_FAILED");
        Assert(strlen(msg) > 0, "WS parse failure error message is non-empty");
        socket.Close();
    }
    // Ignore other errors (e.g. close-related)
}

public void OnWsParseFailClose(WsSocket socket, int code, const char[] reason, any userdata) {
    Assert(g_ws_parse_fail_got_error, "WS parse failure onError fired before onClose");
    g_ws_test_pending--;
    MaybeFinishAll();
}

// --- Send queue / dead-peer tests (API v12+) ---

// Backpressure: the server pauses reading ("slow_read") while we send 8 MB, so our
// socket fills and curl takes frames partially or not at all (CURLE_AGAIN). Every
// frame must still arrive, whole and in order. Before v12 frames were dropped or
// the stream was corrupted ("unaligned frame size").
#define WS_BULK_SIZE  65536
#define WS_BULK_COUNT 128
char g_ws_bulk[WS_BULK_SIZE];
int g_ws_bulk_next;
bool g_ws_bulk_bad;

void Test_WS_SendBackpressure() {
    g_ws_test_pending++;
    g_ws_bulk_next = 0;
    g_ws_bulk_bad = false;
    for (int i = 4; i < WS_BULK_SIZE; i++)
        g_ws_bulk[i] = (i % 251) + 1;
    WsSocket ws = new WsSocket(20);
    ws.SetCallbacks(OnWsBulkConnect, OnWsBulkMessage, OnWsTestError, OnWsTestClose);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsBulkConnect(WsSocket socket, any userdata) {
    socket.SendText("slow_read");
    for (int i = 0; i < WS_BULK_COUNT; i++) {
        g_ws_bulk[0] = i;   // < 128: one byte carries the index
        g_ws_bulk[1] = 0;
        g_ws_bulk[2] = 0;
        g_ws_bulk[3] = 0;
        socket.SendBinary(g_ws_bulk, WS_BULK_SIZE);
    }
}

public void OnWsBulkMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    if (!isBinary || length != WS_BULK_SIZE || data[0] != g_ws_bulk_next
            || data[WS_BULK_SIZE - 1] != g_ws_bulk[WS_BULK_SIZE - 1]) {
        if (!g_ws_bulk_bad)
            PrintToServer("  [DEBUG] WS bulk: frame %d bad (binary=%d len=%d idx=%d)",
                g_ws_bulk_next, isBinary, length, data[0]);
        g_ws_bulk_bad = true;
    }
    g_ws_bulk_next++;
    if (g_ws_bulk_next == WS_BULK_COUNT) {
        Assert(!g_ws_bulk_bad, "WS backpressure: every frame echoed whole and in order");
        socket.Close();
    }
}

// Dead peer: the server stops reading and answering pings but keeps TCP open
// ("stall"). With WS_PING_INTERVAL 1 + WS_PONG_TIMEOUT 2 the connection must
// count as dropped within a few seconds and reconnect.
bool g_ws_pong_first;
bool g_ws_pong_timeout_seen;
float g_ws_pong_stall_at;
float g_ws_pong_elapsed;

void Test_WS_PongTimeout() {
    g_ws_test_pending++;
    g_ws_pong_first = true;
    g_ws_pong_timeout_seen = false;
    WsSocket ws = new WsSocket(21);
    ws.SetCallbacks(OnWsPongConnect, OnWsTestMessage, OnWsPongError, OnWsTestClose);
    ws.SetOption(WS_PING_INTERVAL, 1);
    ws.SetOption(WS_PONG_TIMEOUT, 2);
    ws.SetReconnect(1, 100);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsPongConnect(WsSocket socket, any userdata) {
    if (g_ws_pong_first) {
        g_ws_pong_first = false;
        g_ws_pong_stall_at = GetEngineTime();
        socket.SendText("stall");
        return;
    }
    Assert(g_ws_pong_timeout_seen, "WS pong timeout raised WS_ERROR_PONG_TIMEOUT");
    Assert(g_ws_pong_elapsed > 1.5 && g_ws_pong_elapsed < 6.0, "WS pong timeout fired within ping+timeout");
    Assert(true, "WS reconnected after pong timeout");
    socket.Close();
}

public void OnWsPongError(WsSocket socket, int error, const char[] msg, any userdata) {
    if (error == WS_ERROR_PONG_TIMEOUT && !g_ws_pong_timeout_seen) {
        g_ws_pong_timeout_seen = true;
        g_ws_pong_elapsed = GetEngineTime() - g_ws_pong_stall_at;
        return;
    }
    if (error == WS_ERROR_RECONNECTING && g_ws_pong_timeout_seen)
        return;
    g_failed++;
    PrintToServer("[FAIL] WS pong test: unexpected error %d %s", error, msg);
}

// Send queue cap: the server stalls, we send 16 MB with a 512 KB cap. Once the
// kernel buffers are full, the queue passes the cap and the connection fails with
// WS_ERROR_SEND_QUEUE_FULL; without SetReconnect, onClose follows.
bool g_ws_qfull_seen;

void Test_WS_SendQueueFull() {
    g_ws_test_pending++;
    g_ws_qfull_seen = false;
    WsSocket ws = new WsSocket(22);
    ws.SetCallbacks(OnWsQFullConnect, OnWsTestMessage, OnWsQFullError, OnWsQFullClose);
    ws.SetOption(WS_MAX_SEND_QUEUE, 512 * 1024);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsQFullConnect(WsSocket socket, any userdata) {
    socket.SendText("stall");
    for (int i = 0; i < 256; i++)
        socket.SendBinary(g_ws_bulk, WS_BULK_SIZE);
}

public void OnWsQFullError(WsSocket socket, int error, const char[] msg, any userdata) {
    if (error == WS_ERROR_SEND_QUEUE_FULL) {
        g_ws_qfull_seen = true;
        return;
    }
    g_failed++;
    PrintToServer("[FAIL] WS queue-full test: unexpected error %d %s", error, msg);
}

public void OnWsQFullClose(WsSocket socket, int code, const char[] reason, any userdata) {
    Assert(g_ws_qfull_seen, "WS send queue over cap raised WS_ERROR_SEND_QUEUE_FULL");
    g_ws_test_pending--;
    MaybeFinishAll();
}

// --- Answering server PINGs (API v13+) ---
// curl 8.23's auto-pong was only flushed on our next send, so an idle client
// never answered. The server's "ping" command sends a PING, waits 1s while we
// send nothing, then reports "pong-ok" or "pong-missing".

void Test_WS_PongWhileIdle() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(40);
    ws.SetCallbacks(OnWsIdlePingConnect, OnWsIdlePingMessage, OnWsTestError, OnWsTestClose);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsIdlePingConnect(WsSocket socket, any userdata) {
    socket.SendText("ping");
}

public void OnWsIdlePingMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    if (!StrEqual(data, "pong-ok"))
        PrintToServer("  [DEBUG] WS idle ping: server said '%s'", data);
    Assert(StrEqual(data, "pong-ok"), "WS answers server PING while idle");
    socket.Close();
}

// Empty and maximum (125-byte) PING payloads must come back unchanged.
int g_ws_ping_payload_step;

void Test_WS_PingPayloads() {
    g_ws_test_pending++;
    g_ws_ping_payload_step = 0;
    WsSocket ws = new WsSocket(41);
    ws.SetCallbacks(OnWsPingPayloadConnect, OnWsPingPayloadMessage, OnWsTestError, OnWsTestClose);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsPingPayloadConnect(WsSocket socket, any userdata) {
    socket.SendText("ping_payload:0");
}

public void OnWsPingPayloadMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    if (!StrEqual(data, "pong-ok"))
        PrintToServer("  [DEBUG] WS ping payload step %d: server said '%s'", g_ws_ping_payload_step, data);
    if (g_ws_ping_payload_step == 0) {
        Assert(StrEqual(data, "pong-ok"), "WS PONG echoes empty PING payload");
        g_ws_ping_payload_step = 1;
        socket.SendText("ping_payload:125");
        return;
    }
    Assert(StrEqual(data, "pong-ok"), "WS PONG echoes 125-byte PING payload");
    socket.Close();
}

// PING flood against a full socket: the server stops reading while we send
// 24 MB, then sends 2000 PINGs. Our PONGs wait behind the stuck data and must be
// merged (latest payload wins) instead of queueing one per PING.
#define WS_FLOOD_FRAMES 384
int g_ws_flood_echoed;

void Test_WS_PingFlood() {
    g_ws_test_pending++;
    g_ws_flood_echoed = 0;
    WsSocket ws = new WsSocket(42);
    ws.SetCallbacks(OnWsFloodConnect, OnWsFloodMessage, OnWsTestError, OnWsTestClose);
    ws.SetOption(WS_MAX_SEND_QUEUE, 64 * 1024 * 1024);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsFloodConnect(WsSocket socket, any userdata) {
    socket.SendText("ping_flood");
    for (int i = 0; i < WS_FLOOD_FRAMES; i++)
        socket.SendBinary(g_ws_bulk, WS_BULK_SIZE);
}

public void OnWsFloodMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    if (isBinary) {
        if (++g_ws_flood_echoed == WS_FLOOD_FRAMES)
            socket.SendText("flood_report");
        return;
    }
    // "pongs=N last=K"
    char parts[2][32];
    ExplodeString(data, " ", parts, sizeof(parts), sizeof(parts[]));
    int pongs = StringToInt(parts[0][6]);
    int last = StringToInt(parts[1][5]);
    PrintToServer("  WS ping flood: %s", data);
    Assert(pongs >= 1 && pongs <= 10, "WS PING flood: PONGs merged while socket full");
    AssertEq(last, 1999, "WS PING flood: last PONG answers latest PING");
    socket.Close();
}

// WS_AUTO_PONG 0: no PONG goes out.
void Test_WS_AutoPongOff() {
    g_ws_test_pending++;
    WsSocket ws = new WsSocket(43);
    ws.SetCallbacks(OnWsIdlePingConnect, OnWsNoPongMessage, OnWsTestError, OnWsTestClose);
    ws.SetOption(WS_AUTO_PONG, 0);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsNoPongMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    if (!StrEqual(data, "pong-missing"))
        PrintToServer("  [DEBUG] WS auto-pong off: server said '%s'", data);
    Assert(StrEqual(data, "pong-missing"), "WS_AUTO_PONG 0 sends no PONG");
    socket.Close();
}

// Server CLOSE frame with SetReconnect: before v13 this reconnected with no
// callback at all. Now onError gets WS_ERROR_RECONNECTING naming the close code.
bool g_ws_srvclose_first;
bool g_ws_srvclose_notified;

void Test_WS_ReconnectServerClose() {
    g_ws_test_pending++;
    g_ws_srvclose_first = true;
    g_ws_srvclose_notified = false;
    WsSocket ws = new WsSocket(44);
    ws.SetCallbacks(OnWsSrvCloseConnect, OnWsTestMessage, OnWsSrvCloseError, OnWsTestClose);
    ws.SetReconnect(1, 100);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsSrvCloseConnect(WsSocket socket, any userdata) {
    if (g_ws_srvclose_first) {
        g_ws_srvclose_first = false;
        socket.SendText("server_close");
        return;
    }
    Assert(g_ws_srvclose_notified, "WS server CLOSE frame fired WS_ERROR_RECONNECTING with its code");
    socket.Close();
}

public void OnWsSrvCloseError(WsSocket socket, int error, const char[] msg, any userdata) {
    if (error == WS_ERROR_RECONNECTING && StrContains(msg, "1001 going away") != -1) {
        g_ws_srvclose_notified = true;
        return;
    }
    g_failed++;
    PrintToServer("[FAIL] WS server-close test: unexpected error %d %s", error, msg);
}

// Close() inside onError(WS_ERROR_RECONNECTING) cancels the pending retry:
// onClose fires and onConnect does not fire a second time. The 2s retry delay
// keeps the timer pending when the game thread handles the error.
int g_ws_cancel_connects;
bool g_ws_cancel_closed_in_error;

void Test_WS_CancelReconnect() {
    g_ws_test_pending++;
    g_ws_cancel_connects = 0;
    g_ws_cancel_closed_in_error = false;
    WsSocket ws = new WsSocket(45);
    ws.SetCallbacks(OnWsCancelConnect, OnWsTestMessage, OnWsCancelError, OnWsCancelClose);
    ws.SetReconnect(3, 2000, 1.0, 2000);
    ws.Connect("ws://127.0.0.1:8792");
}

public void OnWsCancelConnect(WsSocket socket, any userdata) {
    if (++g_ws_cancel_connects == 1)
        socket.SendText("force_close");
}

public void OnWsCancelError(WsSocket socket, int error, const char[] msg, any userdata) {
    if (error == WS_ERROR_RECONNECTING && !g_ws_cancel_closed_in_error) {
        g_ws_cancel_closed_in_error = true;
        socket.Close();
        return;
    }
    g_failed++;
    PrintToServer("[FAIL] WS cancel-reconnect test: unexpected error %d %s", error, msg);
}

public void OnWsCancelClose(WsSocket socket, int code, const char[] reason, any userdata) {
    Assert(g_ws_cancel_closed_in_error, "WS cancel reconnect: Close() called from WS_ERROR_RECONNECTING");
    AssertEq(g_ws_cancel_connects, 1, "WS cancel reconnect: Close() in onError stops the retry");
    g_ws_test_pending--;
    MaybeFinishAll();
}

void RunWsTests() {
    Test_WS_TextEcho();
    Test_WS_BinaryEcho();
    Test_WS_CloseHandshake();
    Test_WS_Reconnect();
    Test_WS_Reconnect_Exhausted();
    Test_WS_ParseFailed();
    Test_WS_SetHeader();
    Test_WS_ReplaceHeader();
    Test_WS_RemoveHeader();
    Test_WS_ClearHeaders();
    Test_WS_CaseInsensitiveHeader();
    Test_WS_SendBackpressure();
    Test_WS_PongTimeout();
    Test_WS_SendQueueFull();
    Test_WS_PongWhileIdle();
    Test_WS_PingPayloads();
    Test_WS_PingFlood();
    Test_WS_AutoPongOff();
    Test_WS_ReconnectServerClose();
    Test_WS_CancelReconnect();
}
