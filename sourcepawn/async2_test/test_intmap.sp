// IntObject Tests — sparse integer-keyed maps, 32/64-bit keys, msgpack, path traversal

void Test_IntObject_Create() {
    IntObject map = IntObject.Create();
    Assert(view_as<int>(map) != 0, "IntObject Create returns non-zero");
    AssertEq(view_as<int>(map.Type), view_as<int>(JSON_TYPE_INTOBJECT), "IntObject type is INTMAP");
    AssertEq(map.Size, 0, "IntObject Create is empty");
    map.Close();
}

void Test_IntObject_SetGetInt() {
    IntObject map = IntObject.Create();
    map.SetInt(1, 100);
    map.SetInt(2, 200);
    map.SetInt(-1, -999);
    map.SetInt(0, 0);

    AssertEq(map.GetInt(1), 100, "IntObject GetInt key=1");
    AssertEq(map.GetInt(2), 200, "IntObject GetInt key=2");
    AssertEq(map.GetInt(-1), -999, "IntObject GetInt key=-1");
    AssertEq(map.GetInt(0), 0, "IntObject GetInt key=0");
    AssertEq(map.Size, 4, "IntObject size after 4 inserts");
    map.Close();
}

void Test_IntObject_SetGetString() {
    IntObject map = IntObject.Create();
    map.SetString(42, "hello");

    char buf[64];
    map.GetString(42, buf, sizeof(buf));
    AssertStrEq(buf, "hello", "IntObject GetString");

    // Missing key returns ""
    map.GetString(999, buf, sizeof(buf));
    AssertStrEq(buf, "", "IntObject GetString missing key");
    map.Close();
}

void Test_IntObject_SetGetFloat() {
    IntObject map = IntObject.Create();
    map.SetFloat(10, 3.14);
    AssertFloatEq(map.GetFloat(10), 3.14, "IntObject GetFloat");

    // Missing key returns 0.0
    AssertFloatEq(map.GetFloat(999), 0.0, "IntObject GetFloat missing key");
    map.Close();
}

void Test_IntObject_SetGetBool() {
    IntObject map = IntObject.Create();
    map.SetBool(1, true);
    map.SetBool(2, false);

    Assert(map.GetBool(1), "IntObject GetBool true");
    Assert(!map.GetBool(2), "IntObject GetBool false");
    Assert(!map.GetBool(999), "IntObject GetBool missing key");
    map.Close();
}

void Test_IntObject_SetNull() {
    IntObject map = IntObject.Create();
    map.SetNull(5);
    Assert(map.HasKey(5), "IntObject SetNull key exists");
    // GetInt on null returns 0 (default)
    AssertEq(map.GetInt(5), 0, "IntObject GetInt on null returns 0");
    map.Close();
}

void Test_IntObject_HasKey() {
    IntObject map = IntObject.Create();
    Assert(!map.HasKey(1), "IntObject HasKey false before insert");
    map.SetInt(1, 100);
    Assert(map.HasKey(1), "IntObject HasKey true after insert");
    Assert(!map.HasKey(2), "IntObject HasKey false for other key");
    map.Close();
}

void Test_IntObject_Overwrite() {
    IntObject map = IntObject.Create();
    map.SetInt(1, 100);
    map.SetInt(1, 200);
    AssertEq(map.GetInt(1), 200, "IntObject overwrite updates value");
    AssertEq(map.Size, 1, "IntObject overwrite doesn't increase size");
    map.Close();
}

void Test_IntObject_RemoveKey() {
    IntObject map = IntObject.Create();
    map.SetInt(1, 100);
    map.SetInt(2, 200);

    Assert(map.RemoveKey(1), "IntObject RemoveKey returns true");
    Assert(!map.HasKey(1), "IntObject RemoveKey key gone");
    AssertEq(map.Size, 1, "IntObject RemoveKey size decreased");
    Assert(!map.RemoveKey(999), "IntObject RemoveKey missing returns false");
    map.Close();
}

void Test_IntObject_Clear() {
    IntObject map = IntObject.Create();
    map.SetInt(1, 100);
    map.SetInt(2, 200);
    map.SetInt(3, 300);
    map.Clear();
    AssertEq(map.Size, 0, "IntObject Clear empties map");
    Assert(!map.HasKey(1), "IntObject Clear removes keys");

    // Can still use after clear
    map.SetInt(99, 1);
    AssertEq(map.Size, 1, "IntObject still usable after Clear");
    map.Close();
}

void Test_IntObject_SetGetObject() {
    IntObject map = IntObject.Create();

    Json child = Json.CreateObject();
    child.SetInt("inner", 42);
    map.SetObject(1, child);
    child.Close();

    Json got = map.GetObject(1);
    Assert(view_as<int>(got) != 0, "IntObject GetObject returns handle");
    AssertEq(got.GetInt("inner"), 42, "IntObject GetObject child value");
    got.Close();

    // GetObject on missing key
    Json missing = map.GetObject(999);
    AssertEq(view_as<int>(missing), 0, "IntObject GetObject missing returns 0");

    map.Close();
}

void Test_IntObject_SetGetArray() {
    IntObject map = IntObject.Create();

    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(10);
    arr.ArrayAppendInt(20);
    map.SetObject(5, arr);
    arr.Close();

    Json got = map.GetArray(5);
    Assert(view_as<int>(got) != 0, "IntObject GetArray returns handle");
    AssertEq(got.ArrayLength, 2, "IntObject GetArray child length");
    AssertEq(got.ArrayGetInt(0), 10, "IntObject GetArray child [0]");
    got.Close();

    // GetArray on wrong type (int value, not array)
    map.SetInt(6, 42);
    Json wrong = map.GetArray(6);
    AssertEq(view_as<int>(wrong), 0, "IntObject GetArray wrong type returns 0");

    map.Close();
}

void Test_IntObject_SetObject_Move() {
    IntObject map = IntObject.Create();
    Json child = Json.CreateObject();
    child.SetInt("val", 1);
    map.SetObject(1, child);

    // Child is now null after move
    AssertEq(view_as<int>(child.Type), view_as<int>(JSON_TYPE_NULL), "IntObject SetObject moves child");
    Json got = map.GetObject(1);
    AssertEq(got.GetInt("val"), 1, "IntObject SetObject data accessible");
    got.Close();
    child.Close();
    map.Close();
}

void Test_IntObject_Iterator() {
    IntObject map = IntObject.Create();
    map.SetInt(10, 100);
    map.SetInt(20, 200);
    map.SetInt(30, 300);

    int sum_keys = 0;
    int sum_vals = 0;
    int count = 0;
    int key;
    map.IterReset();
    while (map.IterNext(key)) {
        sum_keys += key;
        sum_vals += map.GetInt(key);
        count++;
    }
    AssertEq(count, 3, "IntObject iterator count");
    AssertEq(sum_keys, 60, "IntObject iterator key sum");
    AssertEq(sum_vals, 600, "IntObject iterator value sum");
    map.Close();
}

void Test_IntObject_IteratorEmpty() {
    IntObject map = IntObject.Create();
    int key;
    map.IterReset();
    Assert(!map.IterNext(key), "IntObject empty iterator returns false");
    map.Close();
}

void Test_IntObject_MergeNew() {
    IntObject a = IntObject.Create();
    a.SetInt(1, 100);
    a.SetInt(2, 200);

    IntObject b = IntObject.Create();
    b.SetInt(2, 999);
    b.SetInt(3, 300);

    a.MergeNew(b);
    AssertEq(a.GetInt(1), 100, "IntObject MergeNew keeps 1");
    AssertEq(a.GetInt(2), 200, "IntObject MergeNew keeps existing 2");
    AssertEq(a.GetInt(3), 300, "IntObject MergeNew adds 3");
    AssertEq(a.Size, 3, "IntObject MergeNew correct size");
    b.Close();
    a.Close();
}

void Test_IntObject_MergeReplace() {
    IntObject a = IntObject.Create();
    a.SetInt(1, 100);
    a.SetInt(2, 200);

    IntObject b = IntObject.Create();
    b.SetInt(2, 999);
    b.SetInt(3, 300);

    a.MergeReplace(b);
    AssertEq(a.GetInt(1), 100, "IntObject MergeReplace keeps 1");
    AssertEq(a.GetInt(2), 999, "IntObject MergeReplace overwrites 2");
    AssertEq(a.GetInt(3), 300, "IntObject MergeReplace adds 3");
    AssertEq(a.Size, 3, "IntObject MergeReplace correct size");
    b.Close();
    a.Close();
}

void Test_IntObject_Equals() {
    IntObject a = IntObject.Create();
    a.SetInt(1, 100);
    a.SetString(2, "hello");

    IntObject b = IntObject.Create();
    b.SetInt(1, 100);
    b.SetString(2, "hello");

    IntObject c = IntObject.Create();
    c.SetInt(1, 100);
    c.SetString(2, "world");

    Assert(view_as<Json>(a).Equals(view_as<Json>(b)), "IntObject Equals same content");
    Assert(!view_as<Json>(a).Equals(view_as<Json>(c)), "IntObject Equals different content");

    c.Close();
    b.Close();
    a.Close();
}

void Test_IntObject_Copy() {
    IntObject map = IntObject.Create();
    map.SetInt(1, 100);
    map.SetString(2, "hello");

    Json copy = view_as<Json>(map).Copy();
    Assert(view_as<int>(copy) != 0, "IntObject Copy returns non-zero");
    AssertEq(view_as<int>(copy.Type), view_as<int>(JSON_TYPE_INTOBJECT), "IntObject Copy type preserved");

    // Mutate original — copy should be unaffected
    map.SetInt(1, 999);
    IntObject copy_map = view_as<IntObject>(copy);
    AssertEq(copy_map.GetInt(1), 100, "IntObject Copy is deep");

    char buf[64];
    copy_map.GetString(2, buf, sizeof(buf));
    AssertStrEq(buf, "hello", "IntObject Copy string preserved");

    copy.Close();
    map.Close();
}

void Test_IntObject_JsonSerialize() {
    IntObject map = IntObject.Create();
    map.SetInt(1, 100);

    char buf[64];
    bool ok = view_as<Json>(map).Serialize(buf, sizeof(buf));
    Assert(ok, "IntObject Serialize succeeds");
    AssertStrEq(buf, "null", "IntObject Serialize outputs null");
    map.Close();
}

void Test_IntObject_MsgPack_RoundTrip() {
    IntObject map = IntObject.Create();
    map.SetString(1, "one");
    map.SetInt(2, 200);
    map.SetBool(3, true);

    char buf[256];
    int len = async2_MsgPackSerialize(view_as<Json>(map), buf, sizeof(buf));
    Assert(len > 0, "IntObject MsgPack serialize returns bytes");

    Json parsed = async2_MsgPackParseBuffer(buf, len);
    Assert(view_as<int>(parsed) != 0, "IntObject MsgPack parse returns handle");
    AssertEq(view_as<int>(parsed.Type), view_as<int>(JSON_TYPE_INTOBJECT), "IntObject MsgPack parses as IntObject");

    IntObject parsed_map = view_as<IntObject>(parsed);
    char val[64];
    parsed_map.GetString(1, val, sizeof(val));
    AssertStrEq(val, "one", "IntObject MsgPack roundtrip string");
    AssertEq(parsed_map.GetInt(2), 200, "IntObject MsgPack roundtrip int");
    Assert(parsed_map.GetBool(3), "IntObject MsgPack roundtrip bool");

    parsed.Close();
    map.Close();
}

void Test_IntObject_AsChildOfObject() {
    Json obj = Json.CreateObject();
    IntObject map = IntObject.Create();
    map.SetInt(1, 42);
    obj.SetObject("map", view_as<Json>(map));
    map.Close();

    // GetObject checks child->type == Object; IntObject is not Object, returns 0.
    // Use the path API to get a raw handle:
    Json raw = async2_JsonPathGet(obj, "map");
    Assert(view_as<int>(raw) != 0, "IntObject as child of object via PathGet");
    AssertEq(view_as<int>(raw.Type), view_as<int>(JSON_TYPE_INTOBJECT), "IntObject child type preserved");
    IntObject child_map = view_as<IntObject>(raw);
    AssertEq(child_map.GetInt(1), 42, "IntObject child value");
    raw.Close();
    obj.Close();
}

void Test_IntObject_AsChildOfArray() {
    Json arr = Json.CreateArray();
    IntObject map = IntObject.Create();
    map.SetInt(5, 55);
    arr.ArrayAppendObject(view_as<Json>(map));
    map.Close();

    // ArrayGetObject checks type==Object|Array; IntObject is neither, returns 0.
    // Use path API instead:
    Json path_raw = async2_JsonPathGet(arr, 0);
    Assert(view_as<int>(path_raw) != 0, "IntObject as child of array via PathGet");
    AssertEq(view_as<int>(path_raw.Type), view_as<int>(JSON_TYPE_INTOBJECT), "IntObject array child type");
    IntObject child_map = view_as<IntObject>(path_raw);
    AssertEq(child_map.GetInt(5), 55, "IntObject array child value");
    path_raw.Close();
    arr.Close();
}

void Test_IntObject_AsChildOfIntObject() {
    IntObject outer = IntObject.Create();
    IntObject inner = IntObject.Create();
    inner.SetInt(1, 111);
    outer.SetObject(0, view_as<Json>(inner));
    inner.Close();

    // Retrieve via PathGet (IntObject key traversal)
    Json raw = async2_JsonPathGet(view_as<Json>(outer), 0);
    Assert(view_as<int>(raw) != 0, "IntObject nested in IntObject via PathGet");
    AssertEq(view_as<int>(raw.Type), view_as<int>(JSON_TYPE_INTOBJECT), "Nested IntObject type");
    IntObject child = view_as<IntObject>(raw);
    AssertEq(child.GetInt(1), 111, "Nested IntObject value");
    raw.Close();
    outer.Close();
}

void Test_IntObject_PathGetInt() {
    // Object -> IntObject -> value
    Json root = Json.CreateObject();
    IntObject map = IntObject.Create();
    map.SetInt(42, 999);
    root.SetObject("data", view_as<Json>(map));
    map.Close();

    int val = async2_JsonPathGetInt(root, "data", 42);
    Assert(!async2_JsonPathFailed(), "IntObject PathGetInt no error");
    AssertEq(val, 999, "IntObject PathGetInt value");

    // Missing key in intmap
    async2_JsonPathGetInt(root, "data", 100);
    Assert(async2_JsonPathFailed(), "IntObject PathGetInt missing key fails");

    root.Close();
}

void Test_IntObject_PathGetString() {
    Json root = Json.CreateObject();
    IntObject map = IntObject.Create();
    map.SetString(7, "seven");
    root.SetObject("m", view_as<Json>(map));
    map.Close();

    char buf[64];
    async2_JsonPathGetString(root, buf, sizeof(buf), "m", 7);
    Assert(!async2_JsonPathFailed(), "IntObject PathGetString no error");
    AssertStrEq(buf, "seven", "IntObject PathGetString value");
    root.Close();
}

void Test_IntObject_PathGetType() {
    IntObject map = IntObject.Create();
    map.SetInt(1, 42);
    map.SetString(2, "hello");

    JsonType t1 = async2_JsonPathGetType(view_as<Json>(map), 1);
    AssertEq(view_as<int>(t1), view_as<int>(JSON_TYPE_NUMBER), "IntObject PathGetType int");

    JsonType t2 = async2_JsonPathGetType(view_as<Json>(map), 2);
    AssertEq(view_as<int>(t2), view_as<int>(JSON_TYPE_STRING), "IntObject PathGetType string");

    // Type of the intmap itself
    JsonType self = async2_JsonPathGetType(view_as<Json>(map));
    AssertEq(view_as<int>(self), view_as<int>(JSON_TYPE_INTOBJECT), "IntObject PathGetType self");

    map.Close();
}

void Test_IntObject_CrossTypeAccess() {
    // Object natives on IntObject should return silent defaults
    IntObject map = IntObject.Create();
    map.SetInt(1, 42);

    // Use as Json handle — Object getters should return defaults
    Json j = view_as<Json>(map);
    AssertEq(j.GetInt("key"), 0, "IntObject Object GetInt returns 0");
    Assert(!j.HasKey("key"), "IntObject Object HasKey returns false");
    AssertEq(j.ObjectSize, 0, "IntObject ObjectSize returns 0");
    AssertEq(j.ArrayLength, 0, "IntObject ArrayLength returns 0");

    char buf[64];
    j.GetString("key", buf, sizeof(buf));
    AssertStrEq(buf, "", "IntObject Object GetString returns empty");

    map.Close();
}

void Test_IntObject_NegativeKeys() {
    IntObject map = IntObject.Create();
    map.SetInt(-1, 100);
    map.SetInt(-1000, 200);
    map.SetInt(-2147483648, 300); // INT32_MIN

    AssertEq(map.GetInt(-1), 100, "IntObject negative key -1");
    AssertEq(map.GetInt(-1000), 200, "IntObject negative key -1000");
    AssertEq(map.GetInt(-2147483648), 300, "IntObject negative key INT32_MIN");
    AssertEq(map.Size, 3, "IntObject negative keys size");

    // Iterator should visit all
    int count = 0;
    int key;
    map.IterReset();
    while (map.IterNext(key)) {
        count++;
    }
    AssertEq(count, 3, "IntObject negative keys iterator count");
    map.Close();
}

void Test_IntObject_ZeroKey() {
    IntObject map = IntObject.Create();
    map.SetString(0, "zero");
    Assert(map.HasKey(0), "IntObject HasKey 0");

    char buf[64];
    map.GetString(0, buf, sizeof(buf));
    AssertStrEq(buf, "zero", "IntObject GetString key=0");

    // Remove key 0
    Assert(map.RemoveKey(0), "IntObject RemoveKey 0");
    Assert(!map.HasKey(0), "IntObject HasKey 0 after remove");
    map.Close();
}

void Test_IntObject_LargeKey_64bit() {
    IntObject map = IntObject.Create();

    // SteamID-range value: 76561198012345678 = 0x01100001_0BC3E38E
    // low=0x0BC3E38E=197534606, high=0x01100001=17825793
    int key[2];
    key[0] = 197534606;   // low 32 bits
    key[1] = 17825793;    // high 32 bits

    map.SetInt64(key, 42);
    AssertEq(map.GetInt64(key), 42, "IntObject 64-bit key get");
    Assert(map.HasKey64(key), "IntObject 64-bit HasKey");
    AssertEq(map.Size, 1, "IntObject 64-bit key size");

    // Second large key
    int key2[2];
    key2[0] = 197534607;
    key2[1] = 17825793;
    map.SetString64(key2, "player2");

    char buf[64];
    map.GetString64(key2, buf, sizeof(buf));
    AssertStrEq(buf, "player2", "IntObject 64-bit key string");

    AssertEq(map.Size, 2, "IntObject two 64-bit keys");

    // Remove 64-bit key
    Assert(map.RemoveKey64(key), "IntObject RemoveKey64");
    Assert(!map.HasKey64(key), "IntObject HasKey64 after remove");
    AssertEq(map.Size, 1, "IntObject size after 64-bit remove");

    map.Close();
}

void Test_IntObject_64bit_Iterator() {
    IntObject map = IntObject.Create();

    int k1[2]; k1[0] = 1; k1[1] = 0;
    int k2[2]; k2[0] = 0; k2[1] = 1;  // 0x1_00000000
    int k3[2]; k3[0] = -1; k3[1] = -1; // -1 as int64

    map.SetInt64(k1, 10);
    map.SetInt64(k2, 20);
    map.SetInt64(k3, 30);

    int count = 0;
    int key64[2];
    map.IterReset();
    while (map.IterNext64(key64)) {
        count++;
    }
    AssertEq(count, 3, "IntObject 64-bit iterator count");
    map.Close();
}

void Test_IntObject_64bit_SetGetAll() {
    IntObject map = IntObject.Create();
    int key[2]; key[0] = 100; key[1] = 200;

    // Float
    map.SetFloat64(key, 2.5);
    AssertFloatEq(map.GetFloat64(key), 2.5, "IntObject 64-bit float");

    // Bool
    map.SetBool64(key, true);
    Assert(map.GetBool64(key), "IntObject 64-bit bool");

    // Null
    map.SetNull64(key);
    Assert(map.HasKey64(key), "IntObject 64-bit null exists");
    AssertEq(map.GetInt64(key), 0, "IntObject 64-bit null returns 0");

    // Object
    Json child = Json.CreateObject();
    child.SetInt("x", 5);
    map.SetObject64(key, child);
    child.Close();

    Json got = map.GetObject64(key);
    Assert(view_as<int>(got) != 0, "IntObject 64-bit GetObject");
    AssertEq(got.GetInt("x"), 5, "IntObject 64-bit GetObject value");
    got.Close();

    // Array child
    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(99);
    int key2[2]; key2[0] = 1; key2[1] = 1;
    map.SetObject64(key2, arr);
    arr.Close();

    Json got_arr = map.GetArray64(key2);
    Assert(view_as<int>(got_arr) != 0, "IntObject 64-bit GetArray");
    AssertEq(got_arr.ArrayLength, 1, "IntObject 64-bit GetArray length");
    got_arr.Close();

    map.Close();
}

void Test_IntObject_64bit_KeyValue() {
    IntObject map = IntObject.Create();
    int key[2]; key[0] = 1; key[1] = 2;

    // Set 64-bit key with 64-bit value
    int val[2]; val[0] = 0; val[1] = 100;
    map.SetInt64Value(key, val);

    // Read back via 64-bit accessor
    int got[2];
    map.GetInt64Value(key, got);
    AssertEq(got[0], 0, "IntObject Int64KeyValue low");
    AssertEq(got[1], 100, "IntObject Int64KeyValue high");

    map.Close();
}

void Test_IntObject_MsgPack_Nested() {
    // Object containing IntObject — serialize and parse back
    Json obj = Json.CreateObject();
    IntObject inner = IntObject.Create();
    inner.SetInt(1, 11);
    inner.SetInt(2, 22);
    obj.SetObject("imap", view_as<Json>(inner));
    inner.Close();

    char buf[512];
    int len = async2_MsgPackSerialize(obj, buf, sizeof(buf));
    Assert(len > 0, "IntObject nested MsgPack serialize");

    Json parsed = async2_MsgPackParseBuffer(buf, len);
    Assert(view_as<int>(parsed) != 0, "IntObject nested MsgPack parse");

    // Get the inner map via path
    Json child = async2_JsonPathGet(parsed, "imap");
    Assert(view_as<int>(child) != 0, "IntObject nested MsgPack child exists");
    AssertEq(view_as<int>(child.Type), view_as<int>(JSON_TYPE_INTOBJECT), "IntObject nested type preserved");
    IntObject child_map = view_as<IntObject>(child);
    AssertEq(child_map.GetInt(1), 11, "IntObject nested MsgPack value 1");
    AssertEq(child_map.GetInt(2), 22, "IntObject nested MsgPack value 2");
    child.Close();
    parsed.Close();
    obj.Close();
}

void Test_IntObject_MsgPack_Empty() {
    IntObject map = IntObject.Create();

    char buf[64];
    int len = async2_MsgPackSerialize(view_as<Json>(map), buf, sizeof(buf));
    Assert(len > 0, "IntObject empty MsgPack serialize");

    // Empty intmap serializes as empty msgpack map, which parses as Object
    // (empty map has no keys to inspect, defaults to Object)
    Json parsed = async2_MsgPackParseBuffer(buf, len);
    Assert(view_as<int>(parsed) != 0, "IntObject empty MsgPack parse");
    AssertEq(view_as<int>(parsed.Type), view_as<int>(JSON_TYPE_OBJECT), "IntObject empty MsgPack parses as Object");
    AssertEq(parsed.ObjectSize, 0, "IntObject empty MsgPack size 0");

    parsed.Close();
    map.Close();
}

void Test_IntObject_MemorySize() {
    IntObject map = IntObject.Create();
    int empty_size = async2_JsonMemorySize(view_as<Json>(map));
    Assert(empty_size > 0, "IntObject MemorySize empty > 0");

    map.SetInt(1, 100);
    map.SetString(2, "a fairly long string value");
    int populated_size = async2_JsonMemorySize(view_as<Json>(map));
    Assert(populated_size > empty_size, "IntObject MemorySize populated > empty");
    map.Close();
}

void RunIntMapTests() {
    Test_IntObject_Create();
    Test_IntObject_SetGetInt();
    Test_IntObject_SetGetString();
    Test_IntObject_SetGetFloat();
    Test_IntObject_SetGetBool();
    Test_IntObject_SetNull();
    Test_IntObject_HasKey();
    Test_IntObject_Overwrite();
    Test_IntObject_RemoveKey();
    Test_IntObject_Clear();
    Test_IntObject_SetGetObject();
    Test_IntObject_SetGetArray();
    Test_IntObject_SetObject_Move();
    Test_IntObject_Iterator();
    Test_IntObject_IteratorEmpty();
    Test_IntObject_MergeNew();
    Test_IntObject_MergeReplace();
    Test_IntObject_Equals();
    Test_IntObject_Copy();
    Test_IntObject_JsonSerialize();
    Test_IntObject_MsgPack_RoundTrip();
    Test_IntObject_AsChildOfObject();
    Test_IntObject_AsChildOfArray();
    Test_IntObject_AsChildOfIntObject();
    Test_IntObject_PathGetInt();
    Test_IntObject_PathGetString();
    Test_IntObject_PathGetType();
    Test_IntObject_CrossTypeAccess();
    Test_IntObject_NegativeKeys();
    Test_IntObject_ZeroKey();
    Test_IntObject_LargeKey_64bit();
    Test_IntObject_64bit_Iterator();
    Test_IntObject_64bit_SetGetAll();
    Test_IntObject_64bit_KeyValue();
    Test_IntObject_MsgPack_Nested();
    Test_IntObject_MsgPack_Empty();
    Test_IntObject_MemorySize();
}
