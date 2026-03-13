// LRU Cache Tests — string-keyed LruCache and int-keyed IntLruCache

#include <async2_lru>

// ============================================================================
// String-keyed LruCache tests
// ============================================================================

void Test_LruCache_Basic() {
    LruCache cache;
    cache.Init(3, 0);

    cache.SetValue("a", 1);
    cache.SetValue("b", 2);
    cache.SetValue("c", 3);

    any val;
    Assert(cache.GetValue("a", val), "LruCache Get a exists");
    AssertEq(view_as<int>(val), 1, "LruCache Get a value");
    Assert(cache.GetValue("b", val), "LruCache Get b exists");
    AssertEq(view_as<int>(val), 2, "LruCache Get b value");
    Assert(cache.GetValue("c", val), "LruCache Get c exists");
    AssertEq(view_as<int>(val), 3, "LruCache Get c value");

    AssertEq(cache.Size(), 3, "LruCache size");

    cache.Destroy();
}

void Test_LruCache_Eviction() {
    LruCache cache;
    cache.Init(2, 0);

    cache.SetValue("a", 1);
    cache.SetValue("b", 2);
    cache.SetValue("c", 3);  // Should evict "a" (oldest)

    any val;
    Assert(!cache.GetValue("a", val), "LruCache evicted a");
    Assert(cache.GetValue("b", val), "LruCache b survives");
    Assert(cache.GetValue("c", val), "LruCache c survives");

    AssertEq(cache.Size(), 2, "LruCache size after eviction");

    int hits, misses, evictions;
    cache.Stats(hits, misses, evictions);
    AssertEq(evictions, 1, "LruCache 1 eviction");

    cache.Destroy();
}

void Test_LruCache_LruOrder() {
    LruCache cache;
    cache.Init(2, 0);

    cache.SetValue("a", 1);
    cache.SetValue("b", 2);

    // Access "a" to make it most recent
    any val;
    cache.GetValue("a", val);

    // Add "c" — should evict "b" (least recently used)
    cache.SetValue("c", 3);

    Assert(cache.GetValue("a", val), "LruCache LRU a survives");
    Assert(!cache.GetValue("b", val), "LruCache LRU b evicted");
    Assert(cache.GetValue("c", val), "LruCache LRU c exists");

    cache.Destroy();
}

void Test_LruCache_String() {
    LruCache cache;
    cache.Init(3, 0);

    cache.SetString("greeting", "hello world");

    char buf[64];
    Assert(cache.GetString("greeting", buf, sizeof(buf)), "LruCache GetString exists");
    AssertStrEq(buf, "hello world", "LruCache GetString value");

    Assert(!cache.GetString("missing", buf, sizeof(buf)), "LruCache GetString missing");

    cache.Destroy();
}

void Test_LruCache_Json() {
    LruCache cache;
    cache.Init(3, 0);

    Json obj = Json.CreateObject();
    obj.SetInt("score", 42);
    cache.SetJson("player", obj);
    obj.Close();

    Json got = cache.GetJson("player");
    Assert(view_as<int>(got) != 0, "LruCache GetJson returns non-zero");
    AssertEq(got.GetInt("score"), 42, "LruCache GetJson score");
    got.Close();

    Json missing = cache.GetJson("nope");
    Assert(view_as<int>(missing) == 0, "LruCache GetJson missing returns 0");

    cache.Destroy();
}

void Test_LruCache_Has() {
    LruCache cache;
    cache.Init(3, 0);

    cache.SetValue("key", 42);
    Assert(cache.Has("key"), "LruCache Has existing");
    Assert(!cache.Has("nope"), "LruCache Has missing");

    cache.Destroy();
}

void Test_LruCache_Remove() {
    LruCache cache;
    cache.Init(3, 0);

    cache.SetValue("key", 42);
    cache.Remove("key");

    any val;
    Assert(!cache.GetValue("key", val), "LruCache Remove");
    AssertEq(cache.Size(), 0, "LruCache size after remove");

    cache.Destroy();
}

void Test_LruCache_Update() {
    LruCache cache;
    cache.Init(3, 0);

    cache.SetValue("key", 42);
    cache.SetValue("key", 99);

    any val;
    Assert(cache.GetValue("key", val), "LruCache Update exists");
    AssertEq(view_as<int>(val), 99, "LruCache Update value");
    AssertEq(cache.Size(), 1, "LruCache size after update (no dup)");

    cache.Destroy();
}

void Test_LruCache_UpdateString() {
    LruCache cache;
    cache.Init(3, 0);

    cache.SetString("key", "first");
    cache.SetString("key", "second");

    char buf[64];
    Assert(cache.GetString("key", buf, sizeof(buf)), "LruCache UpdateString exists");
    AssertStrEq(buf, "second", "LruCache UpdateString value");
    AssertEq(cache.Size(), 1, "LruCache size after string update");

    cache.Destroy();
}

void Test_LruCache_Stats() {
    LruCache cache;
    cache.Init(2, 0);

    cache.SetValue("a", 1);
    cache.SetValue("b", 2);

    any val;
    cache.GetValue("a", val);   // hit
    cache.GetValue("b", val);   // hit
    cache.GetValue("c", val);   // miss

    cache.SetValue("c", 3);     // evicts "a"
    cache.GetValue("a", val);   // miss (evicted)

    int hits, misses, evictions;
    cache.Stats(hits, misses, evictions);
    AssertEq(hits, 2, "LruCache stats hits");
    AssertEq(misses, 2, "LruCache stats misses");
    AssertEq(evictions, 1, "LruCache stats evictions");

    cache.Destroy();
}

// ============================================================================
// Int-keyed IntLruCache tests
// ============================================================================

void Test_IntLruCache_Basic() {
    IntLruCache cache;
    cache.Init(3, 0);

    cache.SetValue(10, 100);
    cache.SetValue(20, 200);

    any val;
    Assert(cache.GetValue(10, val), "IntLruCache Get exists");
    AssertEq(view_as<int>(val), 100, "IntLruCache Get value");
    Assert(cache.GetValue(20, val), "IntLruCache Get 20");
    AssertEq(view_as<int>(val), 200, "IntLruCache Get 20 value");

    Assert(!cache.GetValue(99, val), "IntLruCache Get missing");

    cache.Destroy();
}

void Test_IntLruCache_Eviction() {
    IntLruCache cache;
    cache.Init(2, 0);

    cache.SetValue(1, 10);
    cache.SetValue(2, 20);
    cache.SetValue(3, 30);  // evicts key=1

    any val;
    Assert(!cache.GetValue(1, val), "IntLruCache evicted 1");
    Assert(cache.GetValue(2, val), "IntLruCache 2 survives");
    Assert(cache.GetValue(3, val), "IntLruCache 3 survives");

    cache.Destroy();
}

void Test_IntLruCache_LruOrder() {
    IntLruCache cache;
    cache.Init(2, 0);

    cache.SetValue(1, 10);
    cache.SetValue(2, 20);

    // Access key=1 to make it most recent
    any val;
    cache.GetValue(1, val);

    // Add key=3 — should evict key=2
    cache.SetValue(3, 30);

    Assert(cache.GetValue(1, val), "IntLruCache LRU 1 survives");
    Assert(!cache.GetValue(2, val), "IntLruCache LRU 2 evicted");
    Assert(cache.GetValue(3, val), "IntLruCache LRU 3 exists");

    cache.Destroy();
}

void Test_IntLruCache_String() {
    IntLruCache cache;
    cache.Init(3, 0);

    cache.SetString(42, "answer");

    char buf[64];
    Assert(cache.GetString(42, buf, sizeof(buf)), "IntLruCache GetString");
    AssertStrEq(buf, "answer", "IntLruCache GetString value");

    cache.Destroy();
}

void Test_IntLruCache_Json() {
    IntLruCache cache;
    cache.Init(3, 0);

    Json obj = Json.CreateObject();
    obj.SetString("name", "test");
    cache.SetJson(1, obj);
    obj.Close();

    Json got = cache.GetJson(1);
    Assert(view_as<int>(got) != 0, "IntLruCache GetJson non-zero");
    char buf[64];
    got.GetString("name", buf, sizeof(buf));
    AssertStrEq(buf, "test", "IntLruCache GetJson name");
    got.Close();

    cache.Destroy();
}

void Test_IntLruCache_Has() {
    IntLruCache cache;
    cache.Init(3, 0);

    cache.SetValue(5, 50);
    Assert(cache.Has(5), "IntLruCache Has existing");
    Assert(!cache.Has(99), "IntLruCache Has missing");

    cache.Destroy();
}

void Test_IntLruCache_Remove() {
    IntLruCache cache;
    cache.Init(3, 0);

    cache.SetValue(5, 50);
    cache.Remove(5);

    any val;
    Assert(!cache.GetValue(5, val), "IntLruCache Remove");
    AssertEq(cache.Size(), 0, "IntLruCache size after remove");

    cache.Destroy();
}

void Test_IntLruCache_Update() {
    IntLruCache cache;
    cache.Init(3, 0);

    cache.SetValue(1, 100);
    cache.SetValue(1, 200);

    any val;
    Assert(cache.GetValue(1, val), "IntLruCache Update exists");
    AssertEq(view_as<int>(val), 200, "IntLruCache Update value");
    AssertEq(cache.Size(), 1, "IntLruCache size after update");

    cache.Destroy();
}

void RunLruCacheTests() {
    Test_LruCache_Basic();
    Test_LruCache_Eviction();
    Test_LruCache_LruOrder();
    Test_LruCache_String();
    Test_LruCache_Json();
    Test_LruCache_Has();
    Test_LruCache_Remove();
    Test_LruCache_Update();
    Test_LruCache_UpdateString();
    Test_LruCache_Stats();
    Test_IntLruCache_Basic();
    Test_IntLruCache_Eviction();
    Test_IntLruCache_LruOrder();
    Test_IntLruCache_String();
    Test_IntLruCache_Json();
    Test_IntLruCache_Has();
    Test_IntLruCache_Remove();
    Test_IntLruCache_Update();
}
