#if defined _async2_intobject_included
    #endinput
#endif
#define _async2_intobject_included

// ============================================================================
// IntObject Natives
// ============================================================================
// IntObject is a sparse integer-keyed map (int64 keys → JSON values).
// Ideal for SteamID→data, entity index→data, item ID→data mappings.
// IntObject handles use the same handle type as Json — close with JsonClose.
// JSON serialize outputs null (JSON can't represent int keys).
// MsgPack round-trips losslessly with native int keys.
//
// Limitations:
//   - JsonPath* varargs traverse IntObject keys as 32-bit ints (cell_t).
//     For 64-bit keys (SteamIDs), use GetObject64/GetArray64 then navigate.
//   - Empty IntObject serialized to MsgPack and parsed back becomes an Object
//     (no keys to inspect, so the parser defaults to Object).

// Creates an empty IntObject. Must be closed with async2_JsonClose.
native Json async2_IntObjectCreate();

// 32-bit key getters — key is sign-extended from cell_t to int64
native int async2_IntObjectGetString(Json handle, int key, char[] buf, int maxlen);
native int async2_IntObjectGetInt(Json handle, int key);
native float async2_IntObjectGetFloat(Json handle, int key);
native bool async2_IntObjectGetBool(Json handle, int key);
native Json async2_IntObjectGetObject(Json handle, int key);
native Json async2_IntObjectGetArray(Json handle, int key);

// 64-bit key getters — key is int[2] (low, high)
native int async2_IntObject64GetString(Json handle, int key[2], char[] buf, int maxlen);
native int async2_IntObject64GetInt(Json handle, int key[2]);
native int async2_IntObject64GetInt64(Json handle, int key[2], int val[2]);
native float async2_IntObject64GetFloat(Json handle, int key[2]);
native bool async2_IntObject64GetBool(Json handle, int key[2]);
native Json async2_IntObject64GetObject(Json handle, int key[2]);
native Json async2_IntObject64GetArray(Json handle, int key[2]);

// 32-bit key setters
native int async2_IntObjectSetString(Json handle, int key, const char[] value);
native int async2_IntObjectSetInt(Json handle, int key, int value);
native int async2_IntObjectSetFloat(Json handle, int key, float value);
native int async2_IntObjectSetBool(Json handle, int key, bool value);
native int async2_IntObjectSetNull(Json handle, int key);
// Deep copy: copies the child tree into this intmap under key. The child handle is not consumed.
native int async2_IntObjectSetObject(Json handle, int key, Json child);

// 64-bit key setters
native int async2_IntObject64SetString(Json handle, int key[2], const char[] value);
native int async2_IntObject64SetInt(Json handle, int key[2], int value);
native int async2_IntObject64SetInt64(Json handle, int key[2], int val[2]);
native int async2_IntObject64SetFloat(Json handle, int key[2], float value);
native int async2_IntObject64SetBool(Json handle, int key[2], bool value);
native int async2_IntObject64SetNull(Json handle, int key[2]);
// Deep copy: copies the child tree into this intmap under key. The child handle is not consumed.
native int async2_IntObject64SetObject(Json handle, int key[2], Json child);

// Mutation
native bool async2_IntObjectRemoveKey(Json handle, int key);
native bool async2_IntObject64RemoveKey(Json handle, int key[2]);
native int async2_IntObjectClear(Json handle);
// Merges keys from another intmap into this one. Deep copies values.
// If overwrite is true, existing keys are replaced; otherwise they are kept.
// The other handle is not consumed.
native int async2_IntObjectMerge(Json handle, Json other, bool overwrite);
native int async2_IntObjectSize(Json handle);
native bool async2_IntObjectHasKey(Json handle, int key);
native bool async2_IntObject64HasKey(Json handle, int key[2]);

// Iterator — same pattern as ObjectIter but yields int keys.
// Do not add or remove keys while iterating.
native int async2_IntObjectIterReset(Json handle);
native bool async2_IntObjectIterNext(Json handle, int &key);
native bool async2_IntObject64IterNext(Json handle, int key[2]);

methodmap IntObject < Json {
    public static IntObject Create() {
        return view_as<IntObject>(async2_IntObjectCreate());
    }

    // Close() inherited from Json

    property int Size {
        public get() { return async2_IntObjectSize(this); }
    }

    // 32-bit key methods
    public bool HasKey(int key) {
        return async2_IntObjectHasKey(this, key);
    }

    public int GetInt(int key) {
        return async2_IntObjectGetInt(this, key);
    }

    public void GetString(int key, char[] buf, int maxlen) {
        async2_IntObjectGetString(this, key, buf, maxlen);
    }

    public float GetFloat(int key) {
        return async2_IntObjectGetFloat(this, key);
    }

    public bool GetBool(int key) {
        return async2_IntObjectGetBool(this, key);
    }

    public Json GetObject(int key) {
        return view_as<Json>(async2_IntObjectGetObject(this, key));
    }

    public Json GetArray(int key) {
        return view_as<Json>(async2_IntObjectGetArray(this, key));
    }

    public void SetString(int key, const char[] value) {
        async2_IntObjectSetString(this, key, value);
    }

    public void SetInt(int key, int value) {
        async2_IntObjectSetInt(this, key, value);
    }

    public void SetFloat(int key, float value) {
        async2_IntObjectSetFloat(this, key, value);
    }

    public void SetBool(int key, bool value) {
        async2_IntObjectSetBool(this, key, value);
    }

    public void SetNull(int key) {
        async2_IntObjectSetNull(this, key);
    }

    public void SetObject(int key, Json child) {
        async2_IntObjectSetObject(this, key, child);
    }

    public bool RemoveKey(int key) {
        return async2_IntObjectRemoveKey(this, key);
    }

    public void Clear() {
        async2_IntObjectClear(this);
    }

    public void MergeNew(IntObject other) {
        async2_IntObjectMerge(this, other, false);
    }

    public void MergeReplace(IntObject other) {
        async2_IntObjectMerge(this, other, true);
    }

    // 64-bit key methods (for SteamIDs)
    public bool HasKey64(int key[2]) {
        return async2_IntObject64HasKey(this, key);
    }

    public int GetInt64(int key[2]) {
        return async2_IntObject64GetInt(this, key);
    }

    public void GetInt64Value(int key[2], int val[2]) {
        async2_IntObject64GetInt64(this, key, val);
    }

    public void GetString64(int key[2], char[] buf, int maxlen) {
        async2_IntObject64GetString(this, key, buf, maxlen);
    }

    public float GetFloat64(int key[2]) {
        return async2_IntObject64GetFloat(this, key);
    }

    public bool GetBool64(int key[2]) {
        return async2_IntObject64GetBool(this, key);
    }

    public Json GetObject64(int key[2]) {
        return view_as<Json>(async2_IntObject64GetObject(this, key));
    }

    public Json GetArray64(int key[2]) {
        return view_as<Json>(async2_IntObject64GetArray(this, key));
    }

    public void SetString64(int key[2], const char[] value) {
        async2_IntObject64SetString(this, key, value);
    }

    public void SetInt64(int key[2], int value) {
        async2_IntObject64SetInt(this, key, value);
    }

    public void SetInt64Value(int key[2], int val[2]) {
        async2_IntObject64SetInt64(this, key, val);
    }

    public void SetFloat64(int key[2], float value) {
        async2_IntObject64SetFloat(this, key, value);
    }

    public void SetBool64(int key[2], bool value) {
        async2_IntObject64SetBool(this, key, value);
    }

    public void SetNull64(int key[2]) {
        async2_IntObject64SetNull(this, key);
    }

    public void SetObject64(int key[2], Json child) {
        async2_IntObject64SetObject(this, key, child);
    }

    public bool RemoveKey64(int key[2]) {
        return async2_IntObject64RemoveKey(this, key);
    }

    // Iterator
    public void IterReset() {
        async2_IntObjectIterReset(this);
    }

    public bool IterNext(int &key) {
        return async2_IntObjectIterNext(this, key);
    }

    public bool IterNext64(int key[2]) {
        return async2_IntObject64IterNext(this, key);
    }

    // Inherited from Json: Copy(), Equals(), Serialize(), Type, Close()
}
