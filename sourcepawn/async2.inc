/**
 * async2 — Asynchronous HTTP/S, JSON/MessagePack, TCP, UDP, and WebSocket extension
 *
 * OVERVIEW:
 *   All I/O is fully asynchronous. Operations are dispatched to a background
 *   event loop thread (libuv), and results are delivered via callbacks on the
 *   game thread during OnGameFrame.
 *
 * HTTP LIFECYCLE:
 *   1. WebRequest req = async2_HttpNew(userdata) — create handle
 *   2. Configure: SetHeader, SetBody*, SetOptInt, etc.
 *   3. req.Execute("GET", url, callback)       — sends request (handle becomes in-flight)
 *   4. In-flight handles cannot be reconfigured — only Close is allowed
 *   5. Callback fires with (request, curlcode, httpcode, size, userdata)
 *      - curlcode: 0 = success, non-zero = curl error (use async2_CurlErrorString)
 *      - curlcode 42 (CURLE_ABORTED_BY_CALLBACK) = user called Close() while in-flight
 *        (handle is invalid in this callback — do not call GetString/GetRawData/etc.)
 *      - httpcode: HTTP status code (200, 404, etc.) — only valid when curlcode == 0
 *      - size: response body size in bytes
 *   6. In the callback: read response with GetString/GetRawData, parse with Json.ParseResponse
 *   7. The handle is automatically freed after the callback returns.
 *      Do NOT call Close() in the callback — it is not needed.
 *      Close() is only needed if you create a handle but never call Execute().
 *
 *   IMPORTANT: Do NOT use `new WebRequest()` — use `async2_HttpNew()` instead.
 *   WebRequest is not an object-like methodmap (spcomp 1.13+ enforces this).
 *
 * JSON HANDLE OWNERSHIP:
 *   - Parse/Create/Copy return new handles that must be closed with JsonClose.
 *   - Ref() returns a lightweight reference sharing the same data (no copy).
 *     Mutations through either handle are visible to both. Close independently.
 *   - GetObject/GetArray/ArrayGetObject return child handles into the same tree.
 *     Mutations through child handles are visible through the parent.
 *     Closing the parent frees unreferenced siblings — child subtrees survive.
 *   - SetObject/ArrayAppendObject/SetBodyJSON/SetBodyMsgPack/WsSendJSON/WsSendMsgPack
 *     CONSUME the child/body handle. The handle is freed after the call.
 *     Close() after is a safe no-op. Use .Copy() first if you need the data afterward.
 *
 * TCP LIFECYCLE:
 *   Client: TcpNew → SetCallbacks → Connect → [onConnect] → Send → [onData] → Close
 *   Server: TcpNew → SetCallbacks → Listen → [onAccept(client)] → client.Send → Close
 *   DNS resolution is async (c-ares). Errors delivered via onError callback.
 *
 * UDP LIFECYCLE:
 *   UdpNew → SetCallbacks → Bind → Send/[onData] → Close
 *   DNS resolution for Send targets is async.
 *
 * WEBSOCKET LIFECYCLE:
 *   WsNew → SetCallbacks → [SetHeader] → Connect(url) → [onConnect] → SendText/SendBinary → [onMessage] → Close
 *   Headers can be set/removed at any time — changes apply on next connect/reconnect.
 *   Uses curl's built-in WebSocket API. Supports ws:// and wss://.
 *   Auto-pong enabled by default (curl handles it). Close sends a close frame.
 *
 * DNS CACHING:
 *   There are two independent DNS caches — one for HTTP and one for TCP/UDP.
 *   Both use c-ares internally, but curl creates its own c-ares channel that
 *   cannot share state with ours.
 *
 *   HTTP:     Managed by curl. Default 60s TTL per curl_multi handle.
 *             Per-request: SetOptInt(req, CURLOPT_DNS_CACHE_TIMEOUT, seconds).
 *             0 = disable (bypass cache), -1 = infinite. Cannot be flushed at
 *             runtime (would require destroying the connection pool).
 *   TCP/UDP:  Managed by async2. Default 60s TTL, max 1024 entries.
 *             Configure with async2_DnsCacheSetTtl(). 0 = disable.
 *             Flushable with async2_DnsCacheFlush().
 *
 *   The async2_Dns* natives only affect the TCP/UDP cache.
 *   To control HTTP DNS behavior, use CURLOPT_DNS_CACHE_TIMEOUT per request.
 *
 * RETURN VALUES:
 *   Natives with int return type and no documented return value always return 0.
 *   Handle-returning natives return 0 on failure.
 *
 * HANDLES:
 *   async2 types (WebRequest, Json, TcpSocket, UdpSocket, WsSocket) are NOT
 *   SourceMod handles. They use an internal handle manager, not SM's handlesys.
 *   Do NOT pass them to CloseHandle(), delete, or any API expecting a Handle.
 *   Always use the type's own .Close() method (e.g. json.Close(), ws.Close()).
 */

#if defined _async2_included
    #endinput
#endif
#define _async2_included

// Define SOURCEMOD_INT64 before including this file to enable native int64
// parameter types (requires SourcePawn int64 support, expected in SM 1.14+).
// Without this, 64-bit integers use int[2] arrays (low, high).
// #define SOURCEMOD_INT64

#include <async2/curl_enums>
#include <async2/json>
#include <async2/intobject>
#include <async2/iterator>
#include <async2/msgpack>
#include <async2/hjson>
#include <async2/http>
#include <async2/tcp>
#include <async2/udp>
#include <async2/ws>
#include <async2/dns>
#include <async2/crypto>
#include <async2/url>
#include <async2/linkedlist>
#include <async2/utils>

// ============================================================================
// Extension registration
// ============================================================================
public Extension:__ext_async2 = {
    name = "async2",
    file = "async2.ext",
#if defined AUTOLOAD_EXTENSIONS
    autoload = 1,
#else
    autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
    required = 1,
#else
    required = 0,
#endif
}

// Natives added after v1.0 — marked optional so plugins compiled against
// newer includes still load on older extensions (that lack these natives).
// Calling an unresolved optional native will ThrowNativeError at runtime.
public void __ext_async2_SetNTVOptional()
{
    // v1.1: HJSON
    MarkNativeAsOptional("async2_HjsonParseString");
    MarkNativeAsOptional("async2_HjsonParseFile");

    // v1.1: JSON file parsing
    MarkNativeAsOptional("async2_JsonParseFile");

    // v1.1: MsgPack limits
    MarkNativeAsOptional("async2_MsgPackSetMaxElements");
    MarkNativeAsOptional("async2_MsgPackGetMaxElements");

    // API version 2: handle ownership, refs, iterators, auto-consume
    MarkNativeAsOptional("async2_SetHandlePlugin");
    MarkNativeAsOptional("async2_LinkedListCopy");
    MarkNativeAsOptional("async2_JsonRef");
    MarkNativeAsOptional("async2_GetVersion");
    MarkNativeAsOptional("async2_ObjectIterCreate");
    MarkNativeAsOptional("async2_IntMapIterCreate");
    MarkNativeAsOptional("async2_IterNext");
    MarkNativeAsOptional("async2_IntMapIterNext");
    MarkNativeAsOptional("async2_IntMapIterNext64");
    MarkNativeAsOptional("async2_IterClose");

    // API version 3: iterator value access
    MarkNativeAsOptional("async2_IterGetType");
    MarkNativeAsOptional("async2_IterGetInt");
    MarkNativeAsOptional("async2_IterGetInt64");
    MarkNativeAsOptional("async2_IterGetFloat");
    MarkNativeAsOptional("async2_IterGetBool");
    MarkNativeAsOptional("async2_IterGetString");
    MarkNativeAsOptional("async2_IterGetObject");

    // API version 4: SetBodyJSON/SetBodyMsgPack/WsSendJson/WsSendMsgPack
    // always DeepCopy (no new natives, behavior change only)

    // API version 5: JsonSerialize returns string length instead of 1
}
