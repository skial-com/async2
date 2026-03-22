#if defined _async2_msgpack_included
    #endinput
#endif
#define _async2_msgpack_included

// ============================================================================
// MessagePack Natives
// ============================================================================
// Parses the response body of an HTTP request as MessagePack.
// Returns a new Json handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_MsgPackParse(WebRequest httpHandle);

// Parses a raw MessagePack buffer.
// Returns a new Json handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_MsgPackParseBuffer(const char[] data, int length);

// Serializes a Json handle into MessagePack binary format.
// Returns number of bytes written to buffer.
native int async2_MsgPackSerialize(Json jsonHandle, char[] buffer, int maxlen);

// Serializes a Json handle as MessagePack and sets it as the request body.
// Also sets Content-Type to application/msgpack. The JSON tree is deep-copied;
// serialization happens on the event thread.
// The JSON handle is NOT consumed — caller must close it separately.
// Returns 0 on success, 2 if invalid or in-flight handle, 3 if serialization failed.
native int async2_SetBodyMsgPack(WebRequest handle, Json jsonHandle);

// Sets the maximum number of elements allowed in a single msgpack array or map.
// Payloads exceeding this limit will fail to parse (MsgPackParse returns 0).
// Pass 0 to reset to default. Default: 1,000,000.
native void async2_MsgPackSetMaxElements(int limit);

// Returns the current maximum element limit for msgpack array/map parsing.
native int async2_MsgPackGetMaxElements();
