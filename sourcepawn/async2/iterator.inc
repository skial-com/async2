#if defined _async2_iterator_included
    #endinput
#endif
#define _async2_iterator_included

// ============================================================================
// Iterator
// ============================================================================
// Standalone iterator for Object and IntObject containers.
// The iterator holds a reference to the container, keeping it alive
// even if the original handle is closed.
//
// IMPORTANT: Must be closed with iter.Close() when done.
// Do not add or remove keys from the container while iterating.
//
// Object usage:
//   char key[64];
//   Iterator iter = Iterator.FromObject(obj);
//   while (iter.Next(key, sizeof(key))) {
//       int val = obj.GetInt(key);
//   }
//   iter.Close();
//
// IntMap usage:
//   int key;
//   Iterator iter = Iterator.FromIntMap(map);
//   while (iter.NextInt(key)) {
//       int val = map.GetInt(key);
//   }
//   iter.Close();

// Creates an iterator over an Object's key-value pairs.
// Returns 0 if the handle is invalid or not an object.
native Iterator async2_ObjectIterCreate(Json jsonHandle);

// Creates an iterator over an IntObject's key-value pairs.
// Returns 0 if the handle is invalid or not an intobject.
native Iterator async2_IntMapIterCreate(Json intmapHandle);

// Advances an object iterator and copies the key into buf.
// Returns true if a pair was available, false when iteration is complete.
native bool async2_IterNext(Iterator iter, char[] key, int maxlen);

// Advances an intmap iterator and writes the 32-bit key.
// Returns true if a pair was available, false when iteration is complete.
native bool async2_IntMapIterNext(Iterator iter, int &key);

// Advances an intmap iterator and writes the 64-bit key.
// Returns true if a pair was available, false when iteration is complete.
native bool async2_IntMapIterNext64(Iterator iter, int key[2]);

// Closes an iterator handle.
native int async2_IterClose(Iterator iter);

// Value access for the current iterator position (call after Next/NextInt/NextInt64).
// Returns the value at the position that was just advanced to.
native JsonType async2_IterGetType(Iterator iter);
native int async2_IterGetInt(Iterator iter);
native int async2_IterGetInt64(Iterator iter, int val[2]);
native float async2_IterGetFloat(Iterator iter);
native bool async2_IterGetBool(Iterator iter);
native int async2_IterGetString(Iterator iter, char[] buf, int maxlen);
// Returns a new handle to the child object/array/intmap. Must be closed.
native Json async2_IterGetObject(Iterator iter);

methodmap Iterator __nullable__ {
    // Create an iterator over an object's key-value pairs.
    public static Iterator FromObject(Json obj) {
        return view_as<Iterator>(async2_ObjectIterCreate(obj));
    }

    // Create an iterator over an intmap's key-value pairs.
    public static Iterator FromIntMap(Json intmap) {
        return view_as<Iterator>(async2_IntMapIterCreate(intmap));
    }

    // Advance (object iterator). Writes key, returns false when done.
    public bool Next(char[] key, int maxlen) {
        return async2_IterNext(this, key, maxlen);
    }

    // Advance (intmap iterator, 32-bit key). Returns false when done.
    public bool NextInt(int &key) {
        return async2_IntMapIterNext(this, key);
    }

    // Advance (intmap iterator, 64-bit key). Returns false when done.
    public bool NextInt64(int key[2]) {
        return async2_IntMapIterNext64(this, key);
    }

    // Value access for the current position (call after Next/NextInt/NextInt64).
    public JsonType GetType() {
        return async2_IterGetType(this);
    }

    public int GetInt() {
        return async2_IterGetInt(this);
    }

    public void GetInt64(int val[2]) {
        async2_IterGetInt64(this, val);
    }

    public float GetFloat() {
        return async2_IterGetFloat(this);
    }

    public bool GetBool() {
        return async2_IterGetBool(this);
    }

    public int GetString(char[] buf, int maxlen) {
        return async2_IterGetString(this, buf, maxlen);
    }

    // Returns a new handle to the child object/array/intmap. Must be closed.
    public Json GetObject() {
        return view_as<Json>(async2_IterGetObject(this));
    }

    public void Close() {
        async2_IterClose(this);
    }
}
