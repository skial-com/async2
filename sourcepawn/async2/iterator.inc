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

    public void Close() {
        async2_IterClose(this);
    }
}
