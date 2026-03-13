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

// Test reconnect after server drops connection.
// Flow: connect → send "force_close" → server drops TCP → reconnect → onConnect fires again → echo → close
void Test_WS_Reconnect() {
    g_ws_test_pending++;
    g_ws_reconnect_first = true;
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
}

public void OnWsReconnectClose(WsSocket socket, int code, const char[] reason, any userdata) {
    g_ws_test_pending--;
    MaybeFinishAll();
}

// Test reconnect exhaustion: connect to bad port, all attempts fail, onClose fires.
int g_ws_exhausted_errors;

void Test_WS_Reconnect_Exhausted() {
    g_ws_test_pending++;
    g_ws_exhausted_errors = 0;
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
    g_ws_exhausted_errors++;
}

public void OnWsExhaustedClose(WsSocket socket, int code, const char[] reason, any userdata) {
    // 3 errors: 1 initial + 2 reconnects
    AssertEq(g_ws_exhausted_errors, 3, "WS reconnect exhausted error count");
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
}
