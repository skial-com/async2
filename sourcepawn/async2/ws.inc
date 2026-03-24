#if defined _async2_ws_included
    #endinput
#endif
#define _async2_ws_included

// ============================================================================
// WebSocket Natives
// ============================================================================
// WsNew(userdata) → SetCallbacks → [SetHeader] → Connect(url) → [onConnect] → Send → [onMessage] → Close
// Headers can also be set/removed after Connect — changes apply on next connect/reconnect.
//
// Uses curl's built-in WebSocket API (CONNECT_ONLY=2).
// Supports ws:// and wss:// (TLS via curl/OpenSSL).
// All callbacks fire on the game thread.

// WebSocket connection state
enum WsState {
    WS_STATE_CREATED = 0,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED,
    WS_STATE_CLOSING,
    WS_STATE_CLOSED,
    WS_STATE_RECONNECTING,
};

// Parse failure error code — delivered via onError when SetParseMessages is active
// and the incoming message cannot be parsed as JSON/MsgPack.
#define WS_ERROR_PARSE_FAILED -1

// WebSocket close status codes (RFC 6455 Section 7.4.1)
#define WS_CLOSE_NORMAL            1000  // Normal closure
#define WS_CLOSE_GOING_AWAY        1001  // Server shutting down or page navigating away
#define WS_CLOSE_PROTOCOL_ERROR    1002  // Protocol error
#define WS_CLOSE_UNSUPPORTED       1003  // Unsupported data type
#define WS_CLOSE_NO_STATUS         1005  // No status code present (never sent on wire)
#define WS_CLOSE_ABNORMAL          1006  // Abnormal closure, no close frame (never sent on wire)
#define WS_CLOSE_INVALID_DATA      1007  // Invalid frame payload data
#define WS_CLOSE_POLICY            1008  // Policy violation
#define WS_CLOSE_TOO_LARGE         1009  // Message too big
#define WS_CLOSE_MISSING_EXT       1010  // Missing expected extension
#define WS_CLOSE_INTERNAL          1011  // Internal server error

// Callback typesets for WebSocket
//
// Quick reference — all callback signatures:
//   onConnect: (WsSocket socket, any userdata)
//   onMessage: (WsSocket socket, const char[] data, int length, bool isBinary, any userdata)  [raw mode]
//              (WsSocket socket, Json data, any userdata)                                      [parsed mode]
//   onError:   (WsSocket socket, int error, const char[] msg, any userdata)
//   onClose:   (WsSocket socket, int code, const char[] reason, any userdata)
//   onPong:    (WsSocket socket, const char[] data, int length, any userdata)

typeset WsConnectCallback {
    function void(WsSocket socket, any userdata);
};

// WARNING: The active overload depends on SetParseMessages mode.
// Raw mode (default): uses the 5-param overload (data, length, isBinary).
// Parsed mode (1=JSON, 2=MsgPack): uses the 3-param overload (Json data).
// If the wrong signature is used, parameters will be misaligned at runtime.
// If parsing fails, onError fires with WS_ERROR_PARSE_FAILED (-1) instead.
typeset WsMessageCallback {
    function void(WsSocket socket, const char[] data, int length, bool isBinary, any userdata);
    function void(WsSocket socket, Json data, any userdata);
};

typeset WsErrorCallback {
    function void(WsSocket socket, int error, const char[] msg, any userdata);
};

// Always fires when the connection closes, including user-initiated WsClose().
// If using manual reconnect logic in onClose, check whether the close was
// user-initiated. The built-in SetReconnect handles this automatically.
typeset WsCloseCallback {
    function void(WsSocket socket, int code, const char[] reason, any userdata);
};

typeset WsPongCallback {
    function void(WsSocket socket, const char[] data, int length, any userdata);
};

/**
 * Creates a new WebSocket handle. Must be closed with async2_WsClose when done.
 * @param userdata  Passed through to all callbacks as the last parameter.
 */
native WsSocket async2_WsNew(any userdata = 0);

/**
 * Sets all event callbacks. Must be called before Connect.
 * @param onPong  Optional — set to INVALID_FUNCTION to ignore pong events.
 */
native int async2_WsSetCallbacks(WsSocket socket, WsConnectCallback onConnect,
    WsMessageCallback onMessage, WsErrorCallback onError, WsCloseCallback onClose,
    WsPongCallback onPong = INVALID_FUNCTION);

/**
 * Sets a custom header on the HTTP upgrade request.
 * Replaces any existing header with the same key.
 * Safe to call before or after Connect (applied on next connect/reconnect).
 */
native int async2_WsSetHeader(WsSocket socket, const char[] key, const char[] value);

/**
 * Removes a custom header by key.
 */
native int async2_WsRemoveHeader(WsSocket socket, const char[] key);

/**
 * Removes all custom headers.
 */
native int async2_WsClearHeaders(WsSocket socket);

/**
 * Connects to a WebSocket server. URL must start with ws:// or wss://.
 * The onConnect callback fires when the WebSocket upgrade is complete.
 */
native int async2_WsConnect(WsSocket socket, const char[] url);

/**
 * Sends a UTF-8 text message.
 * @param data    Null-terminated string to send.
 * @param length  Number of bytes to send (-1 = auto strlen).
 * @return  Always 0. The send is asynchronous — network errors are delivered via onError callback.
 */
native int async2_WsSendText(WsSocket socket, const char[] data, int length = -1);

/**
 * Sends a binary message.
 * @param data    Raw byte buffer (may contain nulls).
 * @param length  Number of bytes to send.
 * @return  Always 0. The send is asynchronous — network errors are delivered via onError callback.
 */
native int async2_WsSendBinary(WsSocket socket, const char[] data, int length);

/**
 * Sends a ping frame. Max 125 bytes payload per RFC 6455.
 * @param data    Optional ping payload.
 * @param length  Payload size in bytes.
 * @return  Always 0. The send is asynchronous — network errors are delivered via onError callback.
 */
native int async2_WsSendPing(WsSocket socket, const char[] data = "", int length = 0);

/**
 * Sends a Json handle as a UTF-8 JSON text message and consumes it.
 * The Json handle is consumed (freed) — Close() after is a safe no-op.
 * @return  0 on success, 2 if invalid handle.
 */
native int async2_WsSendJson(WsSocket socket, Json jsonHandle);

/**
 * Sends a Json handle as a binary MsgPack message and consumes it.
 * The Json handle is consumed (freed) — Close() after is a safe no-op.
 * @return  0 on success, 2 if invalid handle.
 */
native int async2_WsSendMsgPack(WsSocket socket, Json jsonHandle);

/**
 * Closes the WebSocket connection. Sends a close frame with the given code and reason.
 * The onClose callback always fires after the connection is fully closed,
 * even for user-initiated close. Safe to call at any time.
 *
 * @param code    Close status code (default 1000 = Normal Closure).
 * @param reason  Close reason string (max 123 bytes per RFC 6455).
 */
native int async2_WsClose(WsSocket socket, int code = WS_CLOSE_NORMAL, const char[] reason = "");

/**
 * Sets a WebSocket option. Most options should be set before Connect.
 *
 * @param option    Option identifier (use WS_* constants).
 * @param value     Option value.
 */
native int async2_WsSetOption(WsSocket socket, int option, int value);

/**
 * Configures automatic reconnection when the connection drops unexpectedly.
 * Must be called before Connect. Does NOT trigger on user-initiated WsClose.
 * Headers and options are preserved across reconnects. onConnect fires again
 * on success. onError fires for each failed attempt. onClose fires only after
 * all retries are exhausted.
 *
 * @param maxReconnects  Maximum attempts (0 = disabled, -1 = unlimited).
 * @param delayMs        Initial delay before first reconnect in milliseconds.
 * @param backoff        Multiplier applied to delay after each attempt.
 * @param maxDelayMs     Maximum delay between attempts.
 * @return               0 on success, 2 if invalid handle.
 */
native int async2_WsSetReconnect(WsSocket socket, int maxReconnects, int delayMs = 1000, float backoff = 2.0, int maxDelayMs = 30000);

/**
 * Returns the number of reconnection attempts since the last successful connect.
 * Resets to 0 on each successful connection. Useful in onConnect to detect reconnects.
 * @return  Reconnect count, or -1 if invalid handle.
 */
native int async2_WsGetReconnectCount(WsSocket socket);

/**
 * Returns the current connection state.
 * @return  WsState value, or -1 if invalid handle.
 */
native WsState async2_WsGetState(WsSocket socket);

// Option constants for async2_WsSetOption
#define WS_MAX_MESSAGE_SIZE  0  // Max message bytes before 1009 close (default 16MB)
#define WS_AUTO_PONG         1  // Auto-reply to pings (default 1=on, curl handles it)
#define WS_PING_INTERVAL     2  // Auto-ping interval in seconds (0=disabled)
#define WS_CLOSE_TIMEOUT     3  // Close handshake timeout in seconds (default 5)
#define WS_SSL_VERIFYPEER    4  // SSL peer verification (default 1)
#define WS_SSL_VERIFYHOST    5  // SSL host verification (default 1)
#define WS_PARSE_MESSAGES    6  // 0=raw (default), 1=JSON, 2=MsgPack
#define WS_CONNECT_TIMEOUT   7  // Connect timeout in seconds (default 10)

methodmap WsSocket __nullable__ {
    public WsSocket(any userdata = 0) {
        return view_as<WsSocket>(async2_WsNew(userdata));
    }

    public void SetCallbacks(WsConnectCallback onConnect, WsMessageCallback onMessage,
        WsErrorCallback onError, WsCloseCallback onClose,
        WsPongCallback onPong = INVALID_FUNCTION) {
        async2_WsSetCallbacks(this, onConnect, onMessage, onError, onClose, onPong);
    }

    public void SetHeader(const char[] key, const char[] value) {
        async2_WsSetHeader(this, key, value);
    }

    public void RemoveHeader(const char[] key) {
        async2_WsRemoveHeader(this, key);
    }

    public void ClearHeaders() {
        async2_WsClearHeaders(this);
    }

    public void Connect(const char[] url) {
        async2_WsConnect(this, url);
    }

    public void SendText(const char[] data, int length = -1) {
        async2_WsSendText(this, data, length);
    }

    public void SendBinary(const char[] data, int length) {
        async2_WsSendBinary(this, data, length);
    }

    public void SendPing(const char[] data = "", int length = 0) {
        async2_WsSendPing(this, data, length);
    }

    public void Close(int code = WS_CLOSE_NORMAL, const char[] reason = "") {
        async2_WsClose(this, code, reason);
    }

    public void SetOption(int option, int value) {
        async2_WsSetOption(this, option, value);
    }

    /**
     * Auto-parse incoming messages on the event thread.
     * When enabled, onMessage receives a Json handle instead of raw data.
     * Binary frames are delivered as JSON_TYPE_BINARY handles.
     * Must be called before Connect.
     *
     * WARNING: This changes which WsMessageCallback overload is invoked.
     *   mode 0 (raw):    onMessage(WsSocket, const char[] data, int length, bool isBinary, any)
     *   mode 1/2 (parsed): onMessage(WsSocket, Json data, any)
     * Using the wrong signature causes misaligned parameters at runtime.
     * If parsing fails, onError fires with WS_ERROR_PARSE_FAILED (-1) instead.
     *
     * @param mode  0=raw (default), 1=JSON, 2=MsgPack.
     */
    public void SetParseMessages(int mode = 1) {
        async2_WsSetOption(this, WS_PARSE_MESSAGES, mode);
    }

    public int SendJson(Json &data) {
        int ret = async2_WsSendJson(this, data);
        if (!Async2_HandlesAutoConsumed()) data.Close();
        data = null;
        return ret;
    }

    public int SendMsgPack(Json &data) {
        int ret = async2_WsSendMsgPack(this, data);
        if (!Async2_HandlesAutoConsumed()) data.Close();
        data = null;
        return ret;
    }

    public void SetReconnect(int maxReconnects, int delayMs = 1000, float backoff = 2.0, int maxDelayMs = 30000) {
        async2_WsSetReconnect(this, maxReconnects, delayMs, backoff, maxDelayMs);
    }

    property int ReconnectCount {
        public get() {
            return async2_WsGetReconnectCount(this);
        }
    }

    property WsState State {
        public get() {
            return async2_WsGetState(this);
        }
    }
}
