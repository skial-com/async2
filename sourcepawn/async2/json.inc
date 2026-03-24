#if defined _async2_json_included
    #endinput
#endif
#define _async2_json_included

// JSON value types
enum JsonType {
    JSON_TYPE_NONE = 0,
    JSON_TYPE_NULL,
    JSON_TYPE_BOOL,
    JSON_TYPE_NUMBER,
    JSON_TYPE_STRING,
    JSON_TYPE_ARRAY,
    JSON_TYPE_OBJECT,
    JSON_TYPE_BINARY,
    JSON_TYPE_INTOBJECT,
};

// ============================================================================
// JSON Natives
// ============================================================================
//
// Getters (GetObject, GetArray, ArrayGetObject, ObjectValueByIndex)
// return shallow handles that point into the same tree. Mutations through the
// child affect the parent.
//
// Setters (SetObject, ArrayAppendObject) and Copy
// deep-copy the node, producing an independent subtree.
//
// Parses the response body of an HTTP request as JSON.
// Returns a new JSON handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_JsonParseResponse(WebRequest httpHandle);

// Parses a JSON string.
// Returns a new JSON handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_JsonParseString(const char[] str);

// Parses a JSON file (path relative to game directory).
// Returns a new JSON handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_JsonParseFile(const char[] path);

// Creates an empty JSON object ({}).
// Returns a new JSON handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_JsonCreateObject();

// Creates an empty JSON array ([]).
// Returns a new JSON handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_JsonCreateArray();

// Closes a JSON handle. If this is a child handle obtained from GetObject/GetArray/etc,
// the underlying JSON tree remains valid as long as other handles still reference it.
native int async2_JsonClose(Json jsonHandle);

// Returns JSON_TYPE_NONE if handle is invalid.
native JsonType async2_JsonGetType(Json jsonHandle);

// Returns 0 if not an object or handle is invalid.
native int async2_JsonObjectSize(Json jsonHandle);

// Returns 0 if not an array or handle is invalid.
native int async2_JsonArrayLength(Json jsonHandle);

native bool async2_JsonHasKey(Json jsonHandle, const char[] key);

// Returns "" if key not found or value is not a string.
native int async2_JsonGetString(Json jsonHandle, const char[] key, char[] buf, int maxlen);

// Returns 0 if key not found. Float values are truncated to int.
native int async2_JsonGetInt(Json jsonHandle, const char[] key);
// Reads a 64-bit integer into val. Uses int64 if SOURCEMOD_INT64 is defined, int[2] otherwise.
// int[2] layout: val[0] = low 32 bits, val[1] = high 32 bits.
#if defined SOURCEMOD_INT64
native int async2_JsonGetInt64(Json jsonHandle, const char[] key, int64 &val);
#else
native int async2_JsonGetInt64(Json jsonHandle, const char[] key, int val[2]);
#endif
native float async2_JsonGetFloat(Json jsonHandle, const char[] key);
native bool async2_JsonGetBool(Json jsonHandle, const char[] key);

// Shallow: returns a new handle pointing into the same tree, not a copy.
// The returned handle shares ownership of the tree — closing the parent does not invalidate it.
// Must be closed with async2_JsonClose when done.
native Json async2_JsonGetObject(Json jsonHandle, const char[] key);

// Shallow: returns a new handle pointing into the same tree, not a copy.
// The returned handle shares ownership of the tree — closing the parent does not invalidate it.
// Must be closed with async2_JsonClose when done.
native Json async2_JsonGetArray(Json jsonHandle, const char[] key);

// Array getters — return default values (0, 0.0, false, "") for out-of-bounds or wrong type.
native int async2_JsonArrayGetString(Json jsonHandle, int index, char[] buf, int maxlen);
native int async2_JsonArrayGetInt(Json jsonHandle, int index);
#if defined SOURCEMOD_INT64
native int async2_JsonArrayGetInt64(Json jsonHandle, int index, int64 &val);
#else
native int async2_JsonArrayGetInt64(Json jsonHandle, int index, int val[2]);
#endif
native float async2_JsonArrayGetFloat(Json jsonHandle, int index);
native bool async2_JsonArrayGetBool(Json jsonHandle, int index);

// Shallow: returns a new handle pointing into the same tree, not a copy.
// The returned handle shares ownership of the tree — closing the parent does not invalidate it.
// Must be closed with async2_JsonClose when done.
native Json async2_JsonArrayGetObject(Json jsonHandle, int index);

// Object setters — set or overwrite a key. If the key exists, the old value is destroyed.
native int async2_JsonSetString(Json jsonHandle, const char[] key, const char[] value);
native int async2_JsonSetInt(Json jsonHandle, const char[] key, int value);
#if defined SOURCEMOD_INT64
native int async2_JsonSetInt64(Json jsonHandle, const char[] key, int64 val);
#else
native int async2_JsonSetInt64(Json jsonHandle, const char[] key, int val[2]);
#endif
native int async2_JsonSetFloat(Json jsonHandle, const char[] key, float value);
native int async2_JsonSetBool(Json jsonHandle, const char[] key, bool value);
native int async2_JsonSetNull(Json jsonHandle, const char[] key);

// Moves the child tree into this object under key. The child is consumed —
// it becomes null after this call (Close is safe but reads return null/0).
// If you need the child afterward, use SetObject(key, child.Copy()).
// Self-insert (same handle as parent and child) is a no-op.
native int async2_JsonSetObject(Json jsonHandle, const char[] key, Json childHandle);

// Removes a key from an object. Returns true if the key was found and removed.
native bool async2_JsonRemoveKey(Json jsonHandle, const char[] key);

// Removes all keys from an object.
native int async2_JsonObjectClear(Json jsonHandle);

// Merges keys from another object into this one. Deep copies values.
// If overwrite is true, existing keys are replaced; otherwise they are kept.
// The other handle is not consumed.
native int async2_JsonObjectMerge(Json jsonHandle, Json otherHandle, bool overwrite);

// Removes an element from an array by index. Elements after it shift down.
// Returns true if the index was valid and the element was removed.
native bool async2_JsonArrayRemove(Json jsonHandle, int index);

// Replaces an element in an array at the given index.
native int async2_JsonArraySetString(Json jsonHandle, int index, const char[] value);
native int async2_JsonArraySetInt(Json jsonHandle, int index, int value);
#if defined SOURCEMOD_INT64
native int async2_JsonArraySetInt64(Json jsonHandle, int index, int64 val);
#else
native int async2_JsonArraySetInt64(Json jsonHandle, int index, int val[2]);
#endif
native int async2_JsonArraySetFloat(Json jsonHandle, int index, float value);
native int async2_JsonArraySetBool(Json jsonHandle, int index, bool value);
native int async2_JsonArraySetNull(Json jsonHandle, int index);
// Moves the child into this array slot. The child is consumed (see SetObject).
native int async2_JsonArraySetObject(Json jsonHandle, int index, Json childHandle);

// Removes all elements from an array.
native int async2_JsonArrayClear(Json jsonHandle);

// Appends all elements from another array (deep copy). The other handle is not consumed.
native int async2_JsonArrayExtend(Json jsonHandle, Json otherHandle);

native int async2_JsonArrayAppendString(Json jsonHandle, const char[] value);
native int async2_JsonArrayAppendInt(Json jsonHandle, int value);
#if defined SOURCEMOD_INT64
native int async2_JsonArrayAppendInt64(Json jsonHandle, int64 val);
#else
native int async2_JsonArrayAppendInt64(Json jsonHandle, int val[2]);
#endif
native int async2_JsonArrayAppendFloat(Json jsonHandle, float value);
native int async2_JsonArrayAppendBool(Json jsonHandle, bool value);
native int async2_JsonArrayAppendNull(Json jsonHandle);
// Moves the child and appends it to this array. The child is consumed (see SetObject).
native int async2_JsonArrayAppendObject(Json jsonHandle, Json childHandle);

// Recursively compares two JSON values for equality.
// Returns true if both trees have the same structure and values.
native bool async2_JsonEquals(Json jsonHandle, Json otherHandle);

// Deep copy: copies the entire JSON tree rooted at this handle.
// Returns a new independent JSON handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_JsonCopy(Json jsonHandle);

// Returns 1 on success, 0 on failure.
// Set pretty to true for indented, human-readable output.
native int async2_JsonSerialize(Json jsonHandle, char[] buf, int maxlen, bool pretty = false);

/**
 * Reads raw bytes from a JSON_TYPE_BINARY handle into a buffer.
 * Returns number of bytes copied. Use with offset for chunked reading.
 *
 * @param handle  Binary data handle (Type must be JSON_TYPE_BINARY).
 * @param buffer  Output buffer.
 * @param maxlen  Buffer capacity in bytes.
 * @param offset  Byte offset to start reading from (default 0).
 * @return        Bytes copied, or 0 if not a binary handle or offset out of range.
 */
native int async2_JsonGetBuffer(Json handle, char[] buffer, int maxlen, int offset = 0);

// ============================================================================
// JSON Path Natives
// ============================================================================
// Traverse deeply nested JSON using varargs. Each path element is automatically
// interpreted as a string key (for objects) or integer index (for arrays) based
// on the current node's type. String keys are limited to 256 characters.
//
// On failure (key not found, index out of bounds, wrong node type), these return
// a default value silently. Use async2_JsonPathFailed() / async2_JsonPathError()
// to check what went wrong.
//
// Example: given {"response": {"items": [{"name": "foo"}]}}
//   async2_JsonPathGetInt(json, "response", "items", 0, "count")
//   async2_JsonPathGetString(json, buf, sizeof(buf), "response", "items", 0, "name")
//   Json item = async2_JsonPathGet(json, "response", "items", 0)
native int async2_JsonPathGetInt(Json jsonHandle, any ...);
// Reads a 64-bit integer at the resolved path into a 2-cell array.
// val[0] = low 32 bits, val[1] = high 32 bits.
native int async2_JsonPathGetInt64(Json jsonHandle, int val[2], any ...);
native float async2_JsonPathGetFloat(Json jsonHandle, any ...);
native bool async2_JsonPathGetBool(Json jsonHandle, any ...);
native int async2_JsonPathGetString(Json jsonHandle, char[] buf, int maxlen, any ...);

// Shallow: returns a new handle pointing into the same tree, not a copy.
// Must be closed with async2_JsonClose when done. Returns 0 on failure.
native Json async2_JsonPathGet(Json jsonHandle, any ...);

// Returns the type of the value at the given path.
// Returns JSON_TYPE_NONE if the path doesn't exist (key missing, index out of bounds).
// Returns JSON_TYPE_NULL if the path exists and the value is a JSON null literal.
// To check "field exists and is not null": != JSON_TYPE_NONE && != JSON_TYPE_NULL.
// Use async2_JsonPathFailed() after this call to distinguish resolution failure.
native JsonType async2_JsonPathGetType(Json jsonHandle, any ...);

// Returns true if the last async2_JsonPath* call failed to resolve the path.
native bool async2_JsonPathFailed();

// Writes the error message from the last failed path operation.
// Example output: "response.items[3] — index out of bounds (length 2)"
native int async2_JsonPathError(char[] buf, int maxlen);

// Returns the estimated memory usage in bytes for a JSON tree rooted at this handle.
// Includes node overhead, string storage, and map/array container capacity.
native int async2_JsonMemorySize(Json jsonHandle);

// Returns pool allocator stats. total = blocks allocated from OS,
// in_use = blocks currently in use (total - free), block_size = bytes per block.
native int async2_JsonPoolStats(int &total, int &in_use, int &block_size);

methodmap Json __nullable__ {
    public static Json ParseResponse(WebRequest req) {
        return view_as<Json>(async2_JsonParseResponse(req));
    }

    public static Json ParseString(const char[] str) {
        return view_as<Json>(async2_JsonParseString(str));
    }

    public static Json CreateObject() {
        return view_as<Json>(async2_JsonCreateObject());
    }

    public static Json CreateArray() {
        return view_as<Json>(async2_JsonCreateArray());
    }

    public void Close() {
        async2_JsonClose(this);
    }

    property JsonType Type {
        public get() {
            return async2_JsonGetType(this);
        }
    }

    property int ObjectSize {
        public get() {
            return async2_JsonObjectSize(this);
        }
    }

    property int ArrayLength {
        public get() {
            return async2_JsonArrayLength(this);
        }
    }

    public bool HasKey(const char[] key) {
        return async2_JsonHasKey(this, key);
    }

    public void GetString(const char[] key, char[] buf, int maxlen) {
        async2_JsonGetString(this, key, buf, maxlen);
    }

    public int GetInt(const char[] key) {
        return async2_JsonGetInt(this, key);
    }

#if defined SOURCEMOD_INT64
    public void GetInt64(const char[] key, int64 &val) {
#else
    public void GetInt64(const char[] key, int val[2]) {
#endif
        async2_JsonGetInt64(this, key, val);
    }

    public float GetFloat(const char[] key) {
        return async2_JsonGetFloat(this, key);
    }

    public bool GetBool(const char[] key) {
        return async2_JsonGetBool(this, key);
    }

    public Json GetObject(const char[] key) {
        return view_as<Json>(async2_JsonGetObject(this, key));
    }

    public Json GetArray(const char[] key) {
        return view_as<Json>(async2_JsonGetArray(this, key));
    }

    public void ArrayGetString(int index, char[] buf, int maxlen) {
        async2_JsonArrayGetString(this, index, buf, maxlen);
    }

    public int ArrayGetInt(int index) {
        return async2_JsonArrayGetInt(this, index);
    }

#if defined SOURCEMOD_INT64
    public void ArrayGetInt64(int index, int64 &val) {
#else
    public void ArrayGetInt64(int index, int val[2]) {
#endif
        async2_JsonArrayGetInt64(this, index, val);
    }

    public float ArrayGetFloat(int index) {
        return async2_JsonArrayGetFloat(this, index);
    }

    public bool ArrayGetBool(int index) {
        return async2_JsonArrayGetBool(this, index);
    }

    public Json ArrayGetObject(int index) {
        return view_as<Json>(async2_JsonArrayGetObject(this, index));
    }

    public void SetString(const char[] key, const char[] value) {
        async2_JsonSetString(this, key, value);
    }

    public void SetInt(const char[] key, int value) {
        async2_JsonSetInt(this, key, value);
    }

#if defined SOURCEMOD_INT64
    public void SetInt64(const char[] key, int64 val) {
#else
    public void SetInt64(const char[] key, int val[2]) {
#endif
        async2_JsonSetInt64(this, key, val);
    }

    public void SetFloat(const char[] key, float value) {
        async2_JsonSetFloat(this, key, value);
    }

    public void SetBool(const char[] key, bool value) {
        async2_JsonSetBool(this, key, value);
    }

    public void SetNull(const char[] key) {
        async2_JsonSetNull(this, key);
    }

    public void SetObject(const char[] key, Json child) {
        async2_JsonSetObject(this, key, child);
    }

    public bool RemoveKey(const char[] key) {
        return async2_JsonRemoveKey(this, key);
    }

    public void ObjectClear() {
        async2_JsonObjectClear(this);
    }

    // merge without replacing existing keys
    public void ObjectMergeNew(Json other) {
        async2_JsonObjectMerge(this, other, false);
    }

    // merge replace existing keys
    public void ObjectMergeReplace(Json other) {
        async2_JsonObjectMerge(this, other, true);
    }

    public bool ArrayRemove(int index) {
        return async2_JsonArrayRemove(this, index);
    }

    public void ArraySetString(int index, const char[] value) {
        async2_JsonArraySetString(this, index, value);
    }

    public void ArraySetInt(int index, int value) {
        async2_JsonArraySetInt(this, index, value);
    }

#if defined SOURCEMOD_INT64
    public void ArraySetInt64(int index, int64 val) {
#else
    public void ArraySetInt64(int index, int val[2]) {
#endif
        async2_JsonArraySetInt64(this, index, val);
    }

    public void ArraySetFloat(int index, float value) {
        async2_JsonArraySetFloat(this, index, value);
    }

    public void ArraySetBool(int index, bool value) {
        async2_JsonArraySetBool(this, index, value);
    }

    public void ArraySetNull(int index) {
        async2_JsonArraySetNull(this, index);
    }

    public void ArraySetObject(int index, Json child) {
        async2_JsonArraySetObject(this, index, child);
    }

    public void ArrayClear() {
        async2_JsonArrayClear(this);
    }

    public void ArrayExtend(Json other) {
        async2_JsonArrayExtend(this, other);
    }

    public void ArrayAppendString(const char[] value) {
        async2_JsonArrayAppendString(this, value);
    }

    public void ArrayAppendInt(int value) {
        async2_JsonArrayAppendInt(this, value);
    }

#if defined SOURCEMOD_INT64
    public void ArrayAppendInt64(int64 val) {
#else
    public void ArrayAppendInt64(int val[2]) {
#endif
        async2_JsonArrayAppendInt64(this, val);
    }

    public void ArrayAppendFloat(float value) {
        async2_JsonArrayAppendFloat(this, value);
    }

    public void ArrayAppendBool(bool value) {
        async2_JsonArrayAppendBool(this, value);
    }

    public void ArrayAppendNull() {
        async2_JsonArrayAppendNull(this);
    }

    public void ArrayAppendObject(Json child) {
        async2_JsonArrayAppendObject(this, child);
    }

    public bool Equals(Json other) {
        return async2_JsonEquals(this, other);
    }

    public Json Copy() {
        return view_as<Json>(async2_JsonCopy(this));
    }

    public bool Serialize(char[] buf, int maxlen, bool pretty = false) {
        return async2_JsonSerialize(this, buf, maxlen, pretty) != 0;
    }

    public int GetBuffer(char[] buffer, int maxlen, int offset = 0) {
        return async2_JsonGetBuffer(this, buffer, maxlen, offset);
    }
}
