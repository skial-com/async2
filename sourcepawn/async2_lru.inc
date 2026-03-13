/**
 * async2_lru — LRU+TTL cache built on LinkedList + IntObject/Json
 *
 * Two cache variants:
 *   LruCache     — string-keyed (e.g., SteamID strings, URLs)
 *   IntLruCache  — int-keyed (e.g., entity indices, user IDs)
 *
 * Each cache uses just 2 handles (LinkedList + IntObject). All operations
 * are O(1) amortized. Eviction is automatic on insert when over capacity.
 * TTL is optional (set to 0 to disable). Expired entries are lazily evicted
 * on access, or actively swept with EvictExpired() (O(n)).
 *
 * TTL uses async2_GetTime() (monotonic epoch milliseconds, int64). Never
 * wraps, not affected by map changes or server uptime limits.
 *
 * REQUIRES: async2 extension (LinkedList, IntObject, Json, Time natives)
 *
 * Usage:
 *   LruCache cache;
 *   cache.Init(100, 60);  // 100 entries, 60s TTL
 *   cache.SetString("key", "value");
 *   char buf[64];
 *   if (cache.GetString("key", buf, sizeof(buf)))
 *       PrintToServer("got: %s", buf);
 *   cache.Destroy();
 */

#if defined _async2_lru_included
    #endinput
#endif
#define _async2_lru_included

#include <sourcemod>
#include <async2>

#if !defined LRU_MAX_KEY_LENGTH
    #define LRU_MAX_KEY_LENGTH 256
#endif

// ============================================================================
// Internal helpers
// ============================================================================

// Store current timestamp into a JSON entry's "e" field (int64).
static stock void LruCache_StampEntry_(Json entry) {
    Timestamp ts;
    ts.Now();
    int buf[2];
    ts.ToInt64(buf);
    entry.SetInt64("e", buf);
}

// Check if entry is within TTL. Returns true if not expired (or TTL disabled).
static stock bool LruCache_CheckStamp_(int ttlMs, Json entry) {
    if (ttlMs <= 0) return true;
    int buf[2];
    entry.GetInt64("e", buf);
    Timestamp stamp;
    stamp.low = buf[0];
    stamp.high = buf[1];
    return stamp.ElapsedMs() < ttlMs;
}

// ============================================================================
// String-keyed LRU Cache
// ============================================================================
// Data layout in IntObject:
//   [-1] → Json Object { "key1": nodeId1, "key2": nodeId2, ... }  (forward mapping)
//   [nodeId1] → { k: "key1", v: <value>, e: <int64_timestamp> }
//   [nodeId2] → { k: "key2", v: <value>, e: <int64_timestamp> }

enum struct LruCache {
    LinkedList lru;
    IntObject data;
    int maxEntries;
    int ttlMs;
    int evictions;
    int hits;
    int misses;

    void Init(int maxEntries, int ttlSeconds) {
        this.lru = LinkedList.Create();
        this.data = IntObject.Create();
        this.maxEntries = maxEntries;
        this.ttlMs = ttlSeconds * 1000;
        this.evictions = 0;
        this.hits = 0;
        this.misses = 0;

        Json mapping = Json.CreateObject();
        this.data.SetObject(-1, mapping);
        mapping.Close();
    }

    void Destroy() {
        this.data.Close();
        this.lru.Close();
    }

    void SetValue(const char[] key, any value) {
        Json mapping = this.data.GetObject(-1);

        if (mapping.HasKey(key)) {
            int nodeId = mapping.GetInt(key);
            this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
            Json entry = this.data.GetObject(nodeId);
            entry.SetInt("v", view_as<int>(value));
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            entry.Close();
        } else {
            LinkedListNode node = this.lru.PushFront(0);
            int nodeId = view_as<int>(node);

            Json entry = Json.CreateObject();
            entry.SetString("k", key);
            entry.SetInt("v", view_as<int>(value));
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            this.data.SetObject(nodeId, entry);
            entry.Close();

            mapping.SetInt(key, nodeId);

            while (this.lru.Size > this.maxEntries) {
                LruCache_EvictOldest_(this, mapping);
            }
        }

        mapping.Close();
    }

    bool GetValue(const char[] key, any &value) {
        Json mapping = this.data.GetObject(-1);
        if (!mapping.HasKey(key)) {
            mapping.Close();
            this.misses++;
            return false;
        }

        int nodeId = mapping.GetInt(key);
        Json entry = this.data.GetObject(nodeId);

        if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
            entry.Close();
            mapping.RemoveKey(key);
            mapping.Close();
            this.data.RemoveKey(nodeId);
            this.lru.Remove(view_as<LinkedListNode>(nodeId));
            this.evictions++;
            this.misses++;
            return false;
        }

        this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
        value = entry.GetInt("v");
        entry.Close();
        mapping.Close();
        this.hits++;
        return true;
    }

    void SetString(const char[] key, const char[] str) {
        Json mapping = this.data.GetObject(-1);

        if (mapping.HasKey(key)) {
            int nodeId = mapping.GetInt(key);
            this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
            Json entry = this.data.GetObject(nodeId);
            entry.SetString("v", str);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            entry.Close();
        } else {
            LinkedListNode node = this.lru.PushFront(0);
            int nodeId = view_as<int>(node);

            Json entry = Json.CreateObject();
            entry.SetString("k", key);
            entry.SetString("v", str);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            this.data.SetObject(nodeId, entry);
            entry.Close();

            mapping.SetInt(key, nodeId);

            while (this.lru.Size > this.maxEntries) {
                LruCache_EvictOldest_(this, mapping);
            }
        }

        mapping.Close();
    }

    bool GetString(const char[] key, char[] buf, int maxlen) {
        Json mapping = this.data.GetObject(-1);
        if (!mapping.HasKey(key)) {
            mapping.Close();
            this.misses++;
            return false;
        }

        int nodeId = mapping.GetInt(key);
        Json entry = this.data.GetObject(nodeId);

        if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
            entry.Close();
            mapping.RemoveKey(key);
            mapping.Close();
            this.data.RemoveKey(nodeId);
            this.lru.Remove(view_as<LinkedListNode>(nodeId));
            this.evictions++;
            this.misses++;
            return false;
        }

        this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
        entry.GetString("v", buf, maxlen);
        entry.Close();
        mapping.Close();
        this.hits++;
        return true;
    }

    void SetJson(const char[] key, Json json) {
        Json mapping = this.data.GetObject(-1);

        if (mapping.HasKey(key)) {
            int nodeId = mapping.GetInt(key);
            this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
            Json entry = this.data.GetObject(nodeId);
            entry.SetObject("v", json);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            entry.Close();
        } else {
            LinkedListNode node = this.lru.PushFront(0);
            int nodeId = view_as<int>(node);

            Json entry = Json.CreateObject();
            entry.SetString("k", key);
            entry.SetObject("v", json);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            this.data.SetObject(nodeId, entry);
            entry.Close();

            mapping.SetInt(key, nodeId);

            while (this.lru.Size > this.maxEntries) {
                LruCache_EvictOldest_(this, mapping);
            }
        }

        mapping.Close();
    }

    Json GetJson(const char[] key) {
        Json mapping = this.data.GetObject(-1);
        if (!mapping.HasKey(key)) {
            mapping.Close();
            this.misses++;
            return view_as<Json>(0);
        }

        int nodeId = mapping.GetInt(key);
        Json entry = this.data.GetObject(nodeId);

        if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
            entry.Close();
            mapping.RemoveKey(key);
            mapping.Close();
            this.data.RemoveKey(nodeId);
            this.lru.Remove(view_as<LinkedListNode>(nodeId));
            this.evictions++;
            this.misses++;
            return view_as<Json>(0);
        }

        this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
        Json val = entry.GetObject("v");
        entry.Close();
        mapping.Close();
        this.hits++;
        return val;
    }

    bool Has(const char[] key) {
        Json mapping = this.data.GetObject(-1);
        if (!mapping.HasKey(key)) {
            mapping.Close();
            return false;
        }

        if (this.ttlMs > 0) {
            int nodeId = mapping.GetInt(key);
            Json entry = this.data.GetObject(nodeId);
            if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
                entry.Close();
                mapping.RemoveKey(key);
                mapping.Close();
                this.data.RemoveKey(nodeId);
                this.lru.Remove(view_as<LinkedListNode>(nodeId));
                this.evictions++;
                return false;
            }
            entry.Close();
        }

        mapping.Close();
        return true;
    }

    void Remove(const char[] key) {
        Json mapping = this.data.GetObject(-1);
        if (!mapping.HasKey(key)) {
            mapping.Close();
            return;
        }

        int nodeId = mapping.GetInt(key);
        mapping.RemoveKey(key);
        mapping.Close();
        this.data.RemoveKey(nodeId);
        this.lru.Remove(view_as<LinkedListNode>(nodeId));
    }

    int EvictExpired() {
        if (this.ttlMs <= 0) return 0;

        int count = 0;
        Json mapping = this.data.GetObject(-1);

        LinkedListNode node = this.lru.Last();
        while (node != INVALID_LINKED_LIST_NODE) {
            LinkedListNode prev = this.lru.Prev(node);

            int nodeId = view_as<int>(node);
            Json entry = this.data.GetObject(nodeId);

            if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
                char key[LRU_MAX_KEY_LENGTH];
                entry.GetString("k", key, sizeof(key));
                entry.Close();

                mapping.RemoveKey(key);
                this.data.RemoveKey(nodeId);
                this.lru.Remove(node);
                this.evictions++;
                count++;
            } else {
                entry.Close();
            }

            node = prev;
        }

        mapping.Close();
        return count;
    }

    int Size() {
        return this.lru.Size;
    }

    void Stats(int &hits, int &misses, int &evictions) {
        hits = this.hits;
        misses = this.misses;
        evictions = this.evictions;
    }
}

// Internal: evict the oldest (tail) entry from a string-keyed cache.
static stock void LruCache_EvictOldest_(LruCache cache, Json mapping) {
    LinkedListNode tail = cache.lru.Last();
    if (tail == INVALID_LINKED_LIST_NODE) return;

    int nodeId = view_as<int>(tail);
    Json entry = cache.data.GetObject(nodeId);
    char key[LRU_MAX_KEY_LENGTH];
    entry.GetString("k", key, sizeof(key));
    entry.Close();

    mapping.RemoveKey(key);
    cache.data.RemoveKey(nodeId);
    cache.lru.Remove(tail);
    cache.evictions++;
}

// ============================================================================
// Int-keyed LRU Cache
// ============================================================================
// Data layout in IntObject:
//   [cacheKey1] → { v: <value>, n: nodeId1, e: <int64_timestamp> }
//   [cacheKey2] → { v: <value>, n: nodeId2, e: <int64_timestamp> }
// LinkedList node values store the cache key for reverse lookup on eviction.

enum struct IntLruCache {
    LinkedList lru;
    IntObject data;
    int maxEntries;
    int ttlMs;
    int evictions;
    int hits;
    int misses;

    void Init(int maxEntries, int ttlSeconds) {
        this.lru = LinkedList.Create();
        this.data = IntObject.Create();
        this.maxEntries = maxEntries;
        this.ttlMs = ttlSeconds * 1000;
        this.evictions = 0;
        this.hits = 0;
        this.misses = 0;
    }

    void Destroy() {
        this.data.Close();
        this.lru.Close();
    }

    void SetValue(int key, any value) {
        if (this.data.HasKey(key)) {
            Json entry = this.data.GetObject(key);
            int nodeId = entry.GetInt("n");
            this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
            entry.SetInt("v", view_as<int>(value));
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            entry.Close();
        } else {
            LinkedListNode node = this.lru.PushFront(key);
            int nodeId = view_as<int>(node);

            Json entry = Json.CreateObject();
            entry.SetInt("v", view_as<int>(value));
            entry.SetInt("n", nodeId);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            this.data.SetObject(key, entry);
            entry.Close();

            while (this.lru.Size > this.maxEntries) {
                IntLruCache_EvictOldest_(this);
            }
        }
    }

    bool GetValue(int key, any &value) {
        if (!this.data.HasKey(key)) {
            this.misses++;
            return false;
        }

        Json entry = this.data.GetObject(key);
        int nodeId = entry.GetInt("n");

        if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
            entry.Close();
            this.data.RemoveKey(key);
            this.lru.Remove(view_as<LinkedListNode>(nodeId));
            this.evictions++;
            this.misses++;
            return false;
        }

        this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
        value = entry.GetInt("v");
        entry.Close();
        this.hits++;
        return true;
    }

    void SetString(int key, const char[] str) {
        if (this.data.HasKey(key)) {
            Json entry = this.data.GetObject(key);
            int nodeId = entry.GetInt("n");
            this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
            entry.SetString("v", str);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            entry.Close();
        } else {
            LinkedListNode node = this.lru.PushFront(key);
            int nodeId = view_as<int>(node);

            Json entry = Json.CreateObject();
            entry.SetString("v", str);
            entry.SetInt("n", nodeId);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            this.data.SetObject(key, entry);
            entry.Close();

            while (this.lru.Size > this.maxEntries) {
                IntLruCache_EvictOldest_(this);
            }
        }
    }

    bool GetString(int key, char[] buf, int maxlen) {
        if (!this.data.HasKey(key)) {
            this.misses++;
            return false;
        }

        Json entry = this.data.GetObject(key);
        int nodeId = entry.GetInt("n");

        if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
            entry.Close();
            this.data.RemoveKey(key);
            this.lru.Remove(view_as<LinkedListNode>(nodeId));
            this.evictions++;
            this.misses++;
            return false;
        }

        this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
        entry.GetString("v", buf, maxlen);
        entry.Close();
        this.hits++;
        return true;
    }

    void SetJson(int key, Json json) {
        if (this.data.HasKey(key)) {
            Json entry = this.data.GetObject(key);
            int nodeId = entry.GetInt("n");
            this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
            entry.SetObject("v", json);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            entry.Close();
        } else {
            LinkedListNode node = this.lru.PushFront(key);
            int nodeId = view_as<int>(node);

            Json entry = Json.CreateObject();
            entry.SetObject("v", json);
            entry.SetInt("n", nodeId);
            if (this.ttlMs > 0)
                LruCache_StampEntry_(entry);
            this.data.SetObject(key, entry);
            entry.Close();

            while (this.lru.Size > this.maxEntries) {
                IntLruCache_EvictOldest_(this);
            }
        }
    }

    Json GetJson(int key) {
        if (!this.data.HasKey(key)) {
            this.misses++;
            return view_as<Json>(0);
        }

        Json entry = this.data.GetObject(key);
        int nodeId = entry.GetInt("n");

        if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
            entry.Close();
            this.data.RemoveKey(key);
            this.lru.Remove(view_as<LinkedListNode>(nodeId));
            this.evictions++;
            this.misses++;
            return view_as<Json>(0);
        }

        this.lru.MoveToFront(view_as<LinkedListNode>(nodeId));
        Json val = entry.GetObject("v");
        entry.Close();
        this.hits++;
        return val;
    }

    bool Has(int key) {
        if (!this.data.HasKey(key))
            return false;

        if (this.ttlMs > 0) {
            Json entry = this.data.GetObject(key);
            if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
                int nodeId = entry.GetInt("n");
                entry.Close();
                this.data.RemoveKey(key);
                this.lru.Remove(view_as<LinkedListNode>(nodeId));
                this.evictions++;
                return false;
            }
            entry.Close();
        }

        return true;
    }

    void Remove(int key) {
        if (!this.data.HasKey(key))
            return;

        Json entry = this.data.GetObject(key);
        int nodeId = entry.GetInt("n");
        entry.Close();
        this.data.RemoveKey(key);
        this.lru.Remove(view_as<LinkedListNode>(nodeId));
    }

    int EvictExpired() {
        if (this.ttlMs <= 0) return 0;

        int count = 0;

        LinkedListNode node = this.lru.Last();
        while (node != INVALID_LINKED_LIST_NODE) {
            LinkedListNode prev = this.lru.Prev(node);

            int cacheKey = this.lru.GetValue(node);
            Json entry = this.data.GetObject(cacheKey);

            if (!LruCache_CheckStamp_(this.ttlMs, entry)) {
                entry.Close();
                this.data.RemoveKey(cacheKey);
                this.lru.Remove(node);
                this.evictions++;
                count++;
            } else {
                entry.Close();
            }

            node = prev;
        }

        return count;
    }

    int Size() {
        return this.lru.Size;
    }

    void Stats(int &hits, int &misses, int &evictions) {
        hits = this.hits;
        misses = this.misses;
        evictions = this.evictions;
    }
}

// Internal: evict the oldest (tail) entry from an int-keyed cache.
static stock void IntLruCache_EvictOldest_(IntLruCache cache) {
    LinkedListNode tail = cache.lru.Last();
    if (tail == INVALID_LINKED_LIST_NODE) return;

    int cacheKey = cache.lru.GetValue(tail);
    cache.data.RemoveKey(cacheKey);
    cache.lru.Remove(tail);
    cache.evictions++;
}
