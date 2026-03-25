#if defined _async2_http_included
    #endinput
#endif
#define _async2_http_included

#define CURL_ERROR_SIZE 256

// Callback signatures
typeset WebCallback {
    function void(WebRequest request, int curlcode, int httpcode, int size);
    function void(WebRequest request, int curlcode, int httpcode, int size, any userdata);
    function void(WebRequest request, int curlcode, int httpcode, Json data);
    function void(WebRequest request, int curlcode, int httpcode, Json data, any userdata);
};

// ============================================================================
// HTTP Natives
// ============================================================================

/**
 * Creates a new HTTP request handle.
 * Defaults: 10s timeout, follow redirects (max 10), auto decompression.
 * Must be closed with async2_HttpClose when done.
 *
 * @param userdata  Passed through to the callback as the last parameter.
 * @return          New handle, or 0 on failure.
 */
native WebRequest async2_HttpNew(any userdata = 0);

/**
 * Closes an HTTP request handle and frees its resources.
 *
 * After Execute(), the handle is automatically freed when the callback returns —
 * you do NOT need to call Close(). Close() is only needed to:
 *   - Free a handle that was never executed (created but not used)
 *   - Cancel an in-flight request (callback fires with curlcode 42).
 *     The handle is invalid in the cancel callback — do not read response data.
 *
 * Calling Close() inside the callback is safe (no-op, returns 2).
 *
 * @return  0 on success, 2 if invalid/already-closed handle.
 */
native int async2_HttpClose(WebRequest handle);

/**
 * Sends the HTTP request. The method string is auto-uppercased.
 * After calling Execute, the handle is in-flight and cannot be reconfigured.
 * The callback fires on the game thread when the request completes or fails.
 *
 * @param method    HTTP method ("GET", "POST", "PUT", "DELETE", "PATCH", etc.)
 * @param url       Full URL including scheme (http:// or https://)
 * @param callback  function void(WebRequest req, int curlcode, int httpcode, int size[, any userdata])
 *                  With SetResponseType: function void(WebRequest req, int curlcode, int httpcode, Json data[, any userdata])
 * @return          0 on success, 2 if invalid or in-flight handle.
 */
native int async2_HttpExecute(WebRequest handle, const char[] method, const char[] url, WebCallback callback);

/**
 * Sets the request body from raw bytes. Use for binary data.
 * @return  0 on success, 2 if invalid or in-flight handle.
 */
native int async2_SetBody(WebRequest handle, const char[] data, int length);

/**
 * Sets the request body from a null-terminated string.
 * @return  0 on success, 2 if invalid or in-flight handle.
 */
native int async2_SetBodyString(WebRequest handle, const char[] str);

/**
 * Sets a JSON handle as the request body and consumes it.
 * Also sets Content-Type: application/json. The JSON handle is consumed
 * (freed) — Close() after is a safe no-op. Use .Copy() before this call
 * if you need the data afterward.
 *
 * @return  0 on success, 2 if invalid/in-flight.
 */
native int async2_SetBodyJSON(WebRequest handle, Json jsonHandle);

/**
 * Enables deflate compression on the request body.
 * Must be called AFTER setting a body. Compression runs on the event thread.
 * @return  0 on success, 2 if invalid or in-flight handle.
 */
native int async2_SetCompression(WebRequest handle, bool compress);

/**
 * Sets a request header. Replaces any existing header with the same key (case-insensitive).
 * Safe to call while in-flight — changes apply on next retry attempt.
 * @return  0 on success, 2 if invalid handle.
 */
native int async2_SetHeader(WebRequest handle, const char[] key, const char[] value);

/**
 * Removes a request header by key (case-insensitive).
 * @return  0 on success, 2 if invalid handle.
 */
native int async2_RemoveHeader(WebRequest handle, const char[] key);

/**
 * Removes all request headers.
 * @return  0 on success, 2 if invalid handle.
 */
native int async2_ClearHeaders(WebRequest handle);

/**
 * Copies the response body into buffer as a null-terminated string.
 * WARNING: Large stack buffers may overflow — use "#pragma dynamic" if needed.
 * @return  0 on success, 2 if invalid or in-flight handle.
 */
native int async2_GetString(WebRequest handle, char[] buffer, int maxlen);

/**
 * Copies the raw response body bytes (may contain nulls).
 * @return  Number of bytes copied, or 2 if invalid or in-flight handle.
 */
native int async2_GetRawData(WebRequest handle, char[] buffer, int maxlen);

/**
 * @return  Response body size in bytes, or 2 if invalid or in-flight handle.
 */
native int async2_GetResponseLength(WebRequest handle);

/**
 * Reads a response header value. Header names are case-insensitive.
 * @return  Header value length, -1 if not found, 2 if invalid or in-flight.
 */
native int async2_GetResponseHeader(WebRequest handle, const char[] name, char[] output, int maxlen);

/**
 * @return  Header value length, -1 if not found, 2 if invalid or in-flight.
 */
native int async2_GetResponseHeaderLength(WebRequest handle, const char[] name);

/**
 * Configures automatic retry with exponential backoff.
 * Must be called BEFORE Execute. Retries on: transport errors, HTTP 429, HTTP 5xx.
 * The callback only fires once — on final success or failure after all retries.
 * async2_HttpClose cancels pending retries. The callback fires with curlcode = CURLE_ABORTED_BY_CALLBACK (42).
 *
 * @param maxRetries  Maximum number of retry attempts (0 = disabled, -1 = unlimited).
 * @param delayMs     Initial delay before first retry in milliseconds.
 * @param backoff     Multiplier applied to delay after each retry (e.g., 2.0 = double each time).
 * @param maxDelayMs  Maximum delay between retries in milliseconds (default 30000).
 * @param logRetries  If true, log each retry attempt to the SourceMod log with reason, delay, and caller location.
 * @return            0 on success, 2 if invalid or in-flight handle.
 */
native int async2_SetRetry(WebRequest handle, int maxRetries, int delayMs = 1000, float backoff = 2.0, int maxDelayMs = 30000, bool logRetries = false);

/**
 * Returns the number of retries performed for this request.
 * Useful in the callback to check how many attempts were needed.
 * @return  Retry count (0 = no retries), or -1 if invalid handle.
 */
native int async2_GetRetryCount(WebRequest handle);

/**
 * Returns HTTP connection pool statistics.
 * @param active     Number of requests currently in curl_multi.
 * @param pending    Number of requests queued but not yet started.
 * @param completed  Lifetime count of completed requests.
 * @param retries    Lifetime count of retry attempts.
 */
native int async2_GetHttpPoolStats(int &active, int &pending, int &completed, int &retries);

/**
 * Sets the debug log level. Logs are written via SourceMod's logging system.
 * @param level  0 = off, 1 = errors only, 2 = info, 3 = debug (verbose).
 */
native void async2_SetLogLevel(int level);

/**
 * Associates a client index with this HTTP request.
 * When the client disconnects, the request is automatically cancelled
 * (callback fires with curlcode 42 if in-flight, handle freed if not yet sent).
 *
 * @param client  Client index (1-N), or 0 to clear.
 * @return        0 on success, 2 if invalid or in-flight handle.
 */
native int async2_SetOwner(WebRequest handle, int client);

/**
 * Sets the response parsing mode. When set to RESPONSE_JSON or RESPONSE_MSGPACK,
 * the response body is parsed on the event thread and delivered as a Json handle
 * in the callback's 4th parameter (instead of body size).
 *
 * Use with the Json data callback overload:
 *   function void(WebRequest req, int curlcode, int httpcode, Json data[, any userdata])
 *
 * The Json handle is owned by the callback — close it with JsonClose() when done.
 * If parsing fails, data is INVALID_HANDLE_VALUE (0) and raw body is accessible
 * via GetString/GetRawData. On curl error or Close()-cancel, the raw callback fires.
 *
 * @param mode  RESPONSE_RAW (0, default), RESPONSE_JSON (1), or RESPONSE_MSGPACK (2).
 * @return      0 on success, 1 if invalid mode, 2 if invalid or in-flight handle.
 */
native int async2_SetResponseType(WebRequest handle, int mode);

// Response type constants for async2_SetResponseType
#define RESPONSE_RAW      0
#define RESPONSE_JSON     1
#define RESPONSE_MSGPACK  2

#define LOG_NONE    0
#define LOG_ERROR   1
#define LOG_INFO    2
#define LOG_DEBUG   3

// ============================================================================
// Curl Option Natives
// ============================================================================
// These expose raw curl options for advanced use. See the CURLOPT/CURLINFO
// enums at the bottom of this file for available options.
//
// Common options:
//   CURLOPT_TIMEOUT_MS         - total request timeout in milliseconds
//   CURLOPT_CONNECTTIMEOUT_MS  - connection timeout in milliseconds
//   CURLOPT_SSL_VERIFYPEER     - set to 0 to skip SSL cert verification
//   CURLOPT_SSL_VERIFYHOST     - set to 0 to skip SSL hostname verification
//   CURLOPT_FRESH_CONNECT      - set to 1 to force new TCP+TLS per request
//   CURLOPT_NOBODY             - set to 1 for HEAD request (no body)
//   CURLOPT_USERAGENT          - custom user agent string (SetOptString)
//
// All return CURLcode (0 = CURLE_OK), or 2 if invalid or in-flight handle.

native int async2_SetOptInt(WebRequest handle, CURLOPT option, int value);
native int async2_SetOptString(WebRequest handle, CURLOPT option, const char[] value);
native int async2_GetInfoInt(WebRequest handle, CURLINFO option, int &value);
native int async2_GetInfoString(WebRequest handle, CURLINFO option, char[] buffer, int maxlen);

/**
 * Reads curl's detailed error message for the last transfer.
 * More descriptive than async2_CurlErrorString for diagnosing specific failures.
 */
native int async2_GetErrorMessage(WebRequest handle, char[] buffer, int maxlen);

/**
 * Sets a curl_multi option globally (e.g., CURLMOPT_MAX_HOST_CONNECTIONS).
 * Applied asynchronously on the event thread.
 */
native int async2_SetMultiOpt(CURLMOPT option, int value);

/**
 * Converts a CURLcode to a human-readable string (e.g., 7 → "Couldn't connect to server").
 */
native int async2_CurlErrorString(int curlcode, char[] buffer, int maxlen);

/**
 * Returns the curl version string (e.g., "libcurl/8.x.x OpenSSL/3.x.x ...").
 */
native int async2_CurlVersion(char[] buffer, int maxlen);

// ============================================================================
// Convenience Functions
// ============================================================================
// Creates and executes a GET request. Returns a handle that must be closed with async2_HttpClose.
stock WebRequest async2_Get(const char[] url, WebCallback callback, any userdata = 0) {
    WebRequest request = async2_HttpNew(userdata);
    if (request) async2_HttpExecute(request, "GET", url, callback);
    return request;
}

// Creates and executes a POST request. Returns a handle that must be closed with async2_HttpClose.
stock WebRequest async2_Post(const char[] url, const char[] body, WebCallback callback, any userdata = 0) {
    WebRequest request = async2_HttpNew(userdata);
    if (request) {
        async2_SetBodyString(request, body);
        async2_HttpExecute(request, "POST", url, callback);
    }
    return request;
}

// Creates and executes a POST request with a JSON body. Returns a handle that must be closed with async2_HttpClose.
// The JSON handle is consumed (freed) — Close() after is a safe no-op.
stock WebRequest async2_PostJson(const char[] url, Json &jsonHandle, WebCallback callback, any userdata = 0) {
    WebRequest request = async2_HttpNew(userdata);
    if (request) {
        async2_SetBodyJSON(request, jsonHandle);
        async2_HttpExecute(request, "POST", url, callback);
    }
    jsonHandle = null;
    return request;
}

methodmap WebRequest __nullable__ {
    public WebRequest(any userdata = 0) {
        return view_as<WebRequest>(async2_HttpNew(userdata));
    }

    public void Close() {
        async2_HttpClose(this);
    }

    public void Execute(const char[] method, const char[] url, WebCallback cb) {
        async2_HttpExecute(this, method, url, cb);
    }

    public static WebRequest Get(const char[] url, WebCallback cb, any userdata = 0) {
        return async2_Get(url, cb, userdata);
    }

    public static WebRequest Post(const char[] url, const char[] body, WebCallback cb, any userdata = 0) {
        return async2_Post(url, body, cb, userdata);
    }

    public static WebRequest PostJson(const char[] url, Json &json, WebCallback cb, any userdata = 0) {
        return async2_PostJson(url, json, cb, userdata);
    }

    public void SetBody(const char[] data, int length) {
        async2_SetBody(this, data, length);
    }

    public void SetBodyString(const char[] str) {
        async2_SetBodyString(this, str);
    }

    public void SetBodyJSON(Json &json) {
        async2_SetBodyJSON(this, json);
        json = null;
    }

    public void SetBodyMsgPack(Json &json) {
        async2_SetBodyMsgPack(this, json);
        json = null;
    }

    public void SetCompression(bool compress) {
        async2_SetCompression(this, compress);
    }

    public void SetHeader(const char[] key, const char[] value) {
        async2_SetHeader(this, key, value);
    }

    public void RemoveHeader(const char[] key) {
        async2_RemoveHeader(this, key);
    }

    public void ClearHeaders() {
        async2_ClearHeaders(this);
    }

    public void GetString(char[] buffer, int maxlen) {
        async2_GetString(this, buffer, maxlen);
    }

    public int GetRawData(char[] buffer, int maxlen) {
        return async2_GetRawData(this, buffer, maxlen);
    }

    property int ResponseLength {
        public get() {
            return async2_GetResponseLength(this);
        }
    }

    public int GetResponseHeader(const char[] name, char[] output, int maxlen) {
        return async2_GetResponseHeader(this, name, output, maxlen);
    }

    public int GetResponseHeaderLength(const char[] name) {
        return async2_GetResponseHeaderLength(this, name);
    }

    public int SetOptInt(CURLOPT option, int value) {
        return async2_SetOptInt(this, option, value);
    }

    public int SetOptString(CURLOPT option, const char[] value) {
        return async2_SetOptString(this, option, value);
    }

    public int GetInfoInt(CURLINFO option, int &value) {
        return async2_GetInfoInt(this, option, value);
    }

    public int GetInfoString(CURLINFO option, char[] buffer, int maxlen) {
        return async2_GetInfoString(this, option, buffer, maxlen);
    }

    public void GetErrorMessage(char[] buffer, int maxlen) {
        async2_GetErrorMessage(this, buffer, maxlen);
    }

    public void SetRetry(int maxRetries, int delayMs = 1000, float backoff = 2.0, int maxDelayMs = 30000, bool logRetries = false) {
        async2_SetRetry(this, maxRetries, delayMs, backoff, maxDelayMs, logRetries);
    }

    property int RetryCount {
        public get() {
            return async2_GetRetryCount(this);
        }
    }

    public void SetOwner(int client) {
        async2_SetOwner(this, client);
    }

    public void SetResponseType(int mode) {
        async2_SetResponseType(this, mode);
    }
}
