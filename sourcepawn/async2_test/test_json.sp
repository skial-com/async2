// JSON Tests — object, array, parse, path, iteration, mutation, int64, memory

void Test_CreateObject() {
    Json obj = Json.CreateObject();
    Assert(view_as<int>(obj) != 0, "CreateObject returns non-zero");
    AssertEq(view_as<int>(obj.Type), view_as<int>(JSON_TYPE_OBJECT), "CreateObject type is OBJECT");
    AssertEq(obj.ObjectSize, 0, "CreateObject is empty");
    obj.Close();
}

void Test_CreateArray() {
    Json arr = Json.CreateArray();
    Assert(view_as<int>(arr) != 0, "CreateArray returns non-zero");
    AssertEq(view_as<int>(arr.Type), view_as<int>(JSON_TYPE_ARRAY), "CreateArray type is ARRAY");
    AssertEq(arr.ArrayLength, 0, "CreateArray is empty");
    arr.Close();
}

void Test_SetGetString() {
    Json obj = Json.CreateObject();
    obj.SetString("key", "hello");
    char buf[64];
    obj.GetString("key", buf, sizeof(buf));
    AssertStrEq(buf, "hello", "SetGet string");
    obj.Close();
}

void Test_SetGetInt() {
    Json obj = Json.CreateObject();
    obj.SetInt("num", 42);
    AssertEq(obj.GetInt("num"), 42, "SetGet int 42");

    obj.SetInt("neg", -100);
    AssertEq(obj.GetInt("neg"), -100, "SetGet int -100");

    obj.SetInt("zero", 0);
    AssertEq(obj.GetInt("zero"), 0, "SetGet int 0");
    obj.Close();
}

void Test_SetGetFloat() {
    Json obj = Json.CreateObject();
    obj.SetFloat("pi", 3.14);
    AssertFloatEq(obj.GetFloat("pi"), 3.14, "SetGet float 3.14");

    obj.SetFloat("neg", -1.5);
    AssertFloatEq(obj.GetFloat("neg"), -1.5, "SetGet float -1.5");
    obj.Close();
}

void Test_SetGetBool() {
    Json obj = Json.CreateObject();
    obj.SetBool("t", true);
    obj.SetBool("f", false);
    Assert(obj.GetBool("t") == true, "SetGet bool true");
    Assert(obj.GetBool("f") == false, "SetGet bool false");
    obj.Close();
}

void Test_SetNull() {
    Json obj = Json.CreateObject();
    obj.SetNull("n");
    Assert(obj.HasKey("n"), "SetNull key exists");
    obj.Close();
}

void Test_HasKey() {
    Json obj = Json.CreateObject();
    obj.SetString("exists", "yes");
    Assert(obj.HasKey("exists"), "HasKey returns true for existing key");
    Assert(!obj.HasKey("missing"), "HasKey returns false for missing key");
    obj.Close();
}

void Test_ObjectSize() {
    Json obj = Json.CreateObject();
    AssertEq(obj.ObjectSize, 0, "ObjectSize empty");
    obj.SetString("a", "1");
    AssertEq(obj.ObjectSize, 1, "ObjectSize after 1 insert");
    obj.SetInt("b", 2);
    AssertEq(obj.ObjectSize, 2, "ObjectSize after 2 inserts");
    obj.SetString("a", "overwrite");
    AssertEq(obj.ObjectSize, 2, "ObjectSize unchanged after overwrite");
    obj.Close();
}

void Test_OverwriteKey() {
    Json obj = Json.CreateObject();
    obj.SetInt("x", 1);
    obj.SetInt("x", 2);
    AssertEq(obj.GetInt("x"), 2, "Overwrite key gets new value");
    obj.Close();
}

void Test_ArrayAppendAndGet() {
    Json arr = Json.CreateArray();

    arr.ArrayAppendString("hello");
    arr.ArrayAppendInt(42);
    arr.ArrayAppendFloat(3.14);
    arr.ArrayAppendBool(true);
    arr.ArrayAppendNull();

    AssertEq(arr.ArrayLength, 5, "Array length after 5 appends");

    char buf[64];
    arr.ArrayGetString(0, buf, sizeof(buf));
    AssertStrEq(buf, "hello", "ArrayGet string at 0");
    AssertEq(arr.ArrayGetInt(1), 42, "ArrayGet int at 1");
    AssertFloatEq(arr.ArrayGetFloat(2), 3.14, "ArrayGet float at 2");
    Assert(arr.ArrayGetBool(3) == true, "ArrayGet bool at 3");

    arr.Close();
}

void Test_ArrayOfObjects() {
    Json arr = Json.CreateArray();
    Json child = Json.CreateObject();
    child.SetString("name", "test");

    arr.ArrayAppendObject(child);
    AssertEq(arr.ArrayLength, 1, "Array length after AppendObject");

    // Move: child is now null after append
    AssertEq(view_as<int>(child.Type), view_as<int>(JSON_TYPE_NULL), "Child null after ArrayAppendObject");

    Json got = arr.ArrayGetObject(0);
    Assert(view_as<int>(got) != 0, "ArrayGetObject returns non-zero");
    char buf[64];
    got.GetString("name", buf, sizeof(buf));
    AssertStrEq(buf, "test", "ArrayAppendObject moves data");

    got.Close();
    child.Close();
    arr.Close();
}

void Test_SetObject_Move() {
    Json parent = Json.CreateObject();
    Json child = Json.CreateObject();
    child.SetInt("val", 10);

    parent.SetObject("child", child);

    // Child is now null after move
    AssertEq(view_as<int>(child.Type), view_as<int>(JSON_TYPE_NULL), "Child null after SetObject");

    Json got = parent.GetObject("child");
    Assert(view_as<int>(got) != 0, "SetObject child retrievable");
    AssertEq(got.GetInt("val"), 10, "SetObject moves data");

    got.Close();
    child.Close();
    parent.Close();
}

void Test_GetObject_Shallow() {
    Json parent = Json.CreateObject();
    Json child = Json.CreateObject();
    child.SetInt("val", 1);
    parent.SetObject("child", child);
    child.Close();

    // GetObject returns a shallow handle into parent's tree
    Json ref = parent.GetObject("child");
    ref.SetInt("val", 2);

    // Mutation through ref should be visible through parent
    Json ref2 = parent.GetObject("child");
    AssertEq(ref2.GetInt("val"), 2, "GetObject is shallow — mutation visible");

    ref2.Close();
    ref.Close();
    parent.Close();
}

void Test_GetArray_Shallow() {
    Json obj = Json.CreateObject();
    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(1);
    obj.SetObject("arr", arr);
    arr.Close();

    Json ref = obj.GetArray("arr");
    ref.ArrayAppendInt(2);

    Json ref2 = obj.GetArray("arr");
    AssertEq(ref2.ArrayLength, 2, "GetArray is shallow — append visible");

    ref2.Close();
    ref.Close();
    obj.Close();
}

void Test_GetObject_MissingKey() {
    Json obj = Json.CreateObject();
    Json missing = obj.GetObject("nope");
    AssertEq(view_as<int>(missing), 0, "GetObject missing key returns 0");
    obj.Close();
}

void Test_GetArray_MissingKey() {
    Json obj = Json.CreateObject();
    Json missing = obj.GetArray("nope");
    AssertEq(view_as<int>(missing), 0, "GetArray missing key returns 0");
    obj.Close();
}

void Test_ParseString() {
    Json json = Json.ParseString("{\"name\":\"async2\",\"version\":1,\"ok\":true}");
    Assert(view_as<int>(json) != 0, "ParseString returns non-zero");
    AssertEq(view_as<int>(json.Type), view_as<int>(JSON_TYPE_OBJECT), "ParseString type is OBJECT");

    char buf[64];
    json.GetString("name", buf, sizeof(buf));
    AssertStrEq(buf, "async2", "ParseString get string");
    AssertEq(json.GetInt("version"), 1, "ParseString get int");
    Assert(json.GetBool("ok") == true, "ParseString get bool");

    json.Close();
}

void Test_ParseString_Array() {
    Json json = Json.ParseString("[1,2,3]");
    Assert(view_as<int>(json) != 0, "ParseString array returns non-zero");
    AssertEq(view_as<int>(json.Type), view_as<int>(JSON_TYPE_ARRAY), "ParseString array type");
    AssertEq(json.ArrayLength, 3, "ParseString array length");
    AssertEq(json.ArrayGetInt(0), 1, "ParseString array[0]");
    AssertEq(json.ArrayGetInt(2), 3, "ParseString array[2]");
    json.Close();
}

void Test_ParseString_Invalid() {
    Json json = Json.ParseString("{invalid json");
    AssertEq(view_as<int>(json), 0, "ParseString invalid returns 0");
}

void Test_ParseString_Empty() {
    Json json = Json.ParseString("");
    AssertEq(view_as<int>(json), 0, "ParseString empty returns 0");
}

void Test_Nested() {
    Json json = Json.ParseString("{\"a\":{\"b\":{\"c\":42}}}");
    Json a = json.GetObject("a");
    Assert(view_as<int>(a) != 0, "Nested level 1");
    Json b = a.GetObject("b");
    Assert(view_as<int>(b) != 0, "Nested level 2");
    AssertEq(b.GetInt("c"), 42, "Nested get deep value");
    b.Close();
    a.Close();
    json.Close();
}

void Test_Equals() {
    Json a = Json.ParseString("{\"x\":1,\"y\":\"hello\"}");
    Json b = Json.ParseString("{\"x\":1,\"y\":\"hello\"}");
    Json c = Json.ParseString("{\"x\":2,\"y\":\"hello\"}");

    Assert(a.Equals(b), "Equals same content");
    Assert(!a.Equals(c), "Equals different content");

    c.Close();
    b.Close();
    a.Close();
}

void Test_Copy_Deep() {
    Json original = Json.CreateObject();
    original.SetInt("val", 1);

    Json copy = original.Copy();
    Assert(view_as<int>(copy) != 0, "Copy returns non-zero");

    // Mutate original — copy should be unaffected
    original.SetInt("val", 999);
    AssertEq(copy.GetInt("val"), 1, "Copy is deep — independent of original");

    copy.Close();
    original.Close();
}

void Test_Serialize() {
    Json obj = Json.CreateObject();
    obj.SetString("key", "val");

    char buf[256];
    bool ok = obj.Serialize(buf, sizeof(buf));
    Assert(ok, "Serialize succeeds");

    // Parse it back to verify round-trip
    Json parsed = Json.ParseString(buf);
    Assert(view_as<int>(parsed) != 0, "Serialize round-trip parseable");

    char val[64];
    parsed.GetString("key", val, sizeof(val));
    AssertStrEq(val, "val", "Serialize round-trip value correct");

    parsed.Close();
    obj.Close();
}

void Test_ObjectIteration() {
    Json obj = Json.CreateObject();
    obj.SetInt("a", 1);
    obj.SetInt("b", 2);

    AssertEq(obj.ObjectSize, 2, "Object iteration size");

    // Verify iterator visits all keys
    int sum = 0;
    char key[64];
    Iterator iter = Iterator.FromObject(obj);
    while (iter.Next(key, sizeof(key))) {
        Assert(StrEqual(key, "a") || StrEqual(key, "b"), "ObjectIterNext valid key");
        sum++;
    }
    iter.Close();
    AssertEq(sum, 2, "Object iteration count");
    obj.Close();
}

void Test_ChildOutlivesParent() {
    Json child;
    {
        Json parent = Json.ParseString("{\"nested\":{\"val\":77}}");
        child = parent.GetObject("nested");
        parent.Close();
    }
    // Child should still be valid after parent is closed (shared ownership)
    AssertEq(child.GetInt("val"), 77, "Child outlives parent");
    child.Close();
}

void Test_MultipleTypes() {
    Json obj = Json.CreateObject();
    obj.SetString("s", "text");
    obj.SetInt("i", 123);
    obj.SetFloat("f", 2.5);
    obj.SetBool("b", false);
    obj.SetNull("n");

    Json child = Json.CreateObject();
    child.SetInt("inner", 1);
    obj.SetObject("o", child);
    child.Close();

    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(10);
    obj.SetObject("a", arr);
    arr.Close();

    AssertEq(obj.ObjectSize, 7, "Multiple types size");

    char buf[64];
    obj.GetString("s", buf, sizeof(buf));
    AssertStrEq(buf, "text", "Multiple types string");
    AssertEq(obj.GetInt("i"), 123, "Multiple types int");
    AssertFloatEq(obj.GetFloat("f"), 2.5, "Multiple types float");
    Assert(obj.GetBool("b") == false, "Multiple types bool");

    Json o = obj.GetObject("o");
    AssertEq(o.GetInt("inner"), 1, "Multiple types nested object");
    o.Close();

    obj.Close();
}

void Test_PathGetInt() {
    Json json = Json.ParseString("{\"a\":{\"b\":{\"c\":42}}}");
    AssertEq(async2_JsonPathGetInt(json, "a", "b", "c"), 42, "PathGetInt nested");
    Assert(!async2_JsonPathFailed(), "PathGetInt nested no error");
    json.Close();
}

void Test_PathGetString() {
    Json json = Json.ParseString("{\"response\":{\"items\":[{\"name\":\"foo\"}]}}");
    char buf[64];
    async2_JsonPathGetString(json, buf, sizeof(buf), "response", "items", 0, "name");
    AssertStrEq(buf, "foo", "PathGetString deep nested");
    Assert(!async2_JsonPathFailed(), "PathGetString no error");
    json.Close();
}

void Test_PathGetFloat() {
    Json json = Json.ParseString("{\"data\":{\"values\":[1.5,2.5,3.5]}}");
    AssertFloatEq(async2_JsonPathGetFloat(json, "data", "values", 1), 2.5, "PathGetFloat nested array");
    Assert(!async2_JsonPathFailed(), "PathGetFloat no error");
    json.Close();
}

void Test_PathGetBool() {
    Json json = Json.ParseString("{\"config\":{\"enabled\":true}}");
    Assert(async2_JsonPathGetBool(json, "config", "enabled"), "PathGetBool nested");
    Assert(!async2_JsonPathFailed(), "PathGetBool no error");
    json.Close();
}

void Test_PathGet_Handle() {
    Json json = Json.ParseString("{\"a\":{\"b\":{\"x\":1,\"y\":2}}}");
    Json obj = async2_JsonPathGet(json, "a", "b");
    Assert(view_as<int>(obj) != 0, "PathGet returns handle");
    Assert(!async2_JsonPathFailed(), "PathGet no error");
    AssertEq(obj.GetInt("x"), 1, "PathGet handle usable x");
    AssertEq(obj.GetInt("y"), 2, "PathGet handle usable y");
    obj.Close();
    json.Close();
}

void Test_PathGetType() {
    Json json = Json.ParseString("{\"a\":[1,\"hello\",true]}");
    AssertEq(view_as<int>(async2_JsonPathGetType(json, "a")), view_as<int>(JSON_TYPE_ARRAY), "PathGetType array");
    AssertEq(view_as<int>(async2_JsonPathGetType(json, "a", 1)), view_as<int>(JSON_TYPE_STRING), "PathGetType string");
    AssertEq(view_as<int>(async2_JsonPathGetType(json, "a", 2)), view_as<int>(JSON_TYPE_BOOL), "PathGetType bool");
    json.Close();
}

void Test_PathFailed_MissingKey() {
    Json json = Json.ParseString("{\"a\":{\"b\":1}}");
    async2_JsonPathGetInt(json, "a", "missing");
    Assert(async2_JsonPathFailed(), "PathFailed on missing key");

    char err[256];
    async2_JsonPathError(err, sizeof(err));
    Assert(StrContains(err, "missing") != -1, "PathError mentions missing key");
    Assert(StrContains(err, "not found") != -1, "PathError says not found");
    json.Close();
}

void Test_PathFailed_IndexOutOfBounds() {
    Json json = Json.ParseString("{\"items\":[1,2,3]}");
    async2_JsonPathGetInt(json, "items", 10);
    Assert(async2_JsonPathFailed(), "PathFailed on index OOB");

    char err[256];
    async2_JsonPathError(err, sizeof(err));
    Assert(StrContains(err, "[10]") != -1, "PathError mentions index");
    Assert(StrContains(err, "out of bounds") != -1, "PathError says out of bounds");
    json.Close();
}

void Test_PathFailed_TraverseLeaf() {
    Json json = Json.ParseString("{\"a\":42}");
    async2_JsonPathGetInt(json, "a", "b");
    Assert(async2_JsonPathFailed(), "PathFailed on traversing into leaf");

    char err[256];
    async2_JsonPathError(err, sizeof(err));
    Assert(StrContains(err, "cannot index") != -1, "PathError says cannot index");
    json.Close();
}

void Test_PathFailed_Clears() {
    Json json = Json.ParseString("{\"a\":{\"b\":1}}");

    // First: fail
    async2_JsonPathGetInt(json, "a", "missing");
    Assert(async2_JsonPathFailed(), "PathFailed set after failure");

    // Then: succeed — error should be cleared
    async2_JsonPathGetInt(json, "a", "b");
    Assert(!async2_JsonPathFailed(), "PathFailed cleared after success");
    json.Close();
}

void Test_PathGetString_Default() {
    Json json = Json.ParseString("{\"a\":42}");
    char buf[64];
    buf[0] = 'X';
    async2_JsonPathGetString(json, buf, sizeof(buf), "a", "nope");
    AssertStrEq(buf, "", "PathGetString returns empty on failure");
    json.Close();
}

void Test_PathGet_Empty() {
    // Zero path elements — returns the root node itself
    Json json = Json.ParseString("{\"a\":1}");
    Json same = async2_JsonPathGet(json);
    Assert(view_as<int>(same) != 0, "PathGet empty path returns handle");
    AssertEq(same.GetInt("a"), 1, "PathGet empty path is root");
    Assert(!async2_JsonPathFailed(), "PathGet empty path no error");
    same.Close();
    json.Close();
}

// Object mutation tests

void Test_RemoveKey() {
    Json obj = Json.CreateObject();
    obj.SetInt("a", 1);
    obj.SetInt("b", 2);

    Assert(obj.RemoveKey("a"), "RemoveKey returns true for existing key");
    Assert(!obj.HasKey("a"), "RemoveKey removes key");
    AssertEq(obj.ObjectSize, 1, "RemoveKey decreases size");
    Assert(!obj.RemoveKey("nonexistent"), "RemoveKey returns false for missing key");
    obj.Close();
}

void Test_ObjectClear() {
    Json obj = Json.CreateObject();
    obj.SetInt("a", 1);
    obj.SetInt("b", 2);
    obj.SetInt("c", 3);
    obj.ObjectClear();
    AssertEq(obj.ObjectSize, 0, "ObjectClear empties object");

    // Can still use after clear
    obj.SetString("new", "val");
    AssertEq(obj.ObjectSize, 1, "ObjectClear object still usable");
    obj.Close();
}

void Test_ObjectMergeNew() {
    Json a = Json.CreateObject();
    a.SetInt("x", 1);
    a.SetInt("y", 2);

    Json b = Json.CreateObject();
    b.SetInt("y", 99);
    b.SetInt("z", 3);

    a.ObjectMergeNew(b);
    AssertEq(a.GetInt("x"), 1, "MergeNew keeps x");
    AssertEq(a.GetInt("y"), 2, "MergeNew keeps existing y");
    AssertEq(a.GetInt("z"), 3, "MergeNew adds z");
    AssertEq(a.ObjectSize, 3, "MergeNew correct size");
    b.Close();
    a.Close();
}

void Test_ObjectMergeReplace() {
    Json a = Json.CreateObject();
    a.SetInt("x", 1);
    a.SetInt("y", 2);

    Json b = Json.CreateObject();
    b.SetInt("y", 99);
    b.SetInt("z", 3);

    a.ObjectMergeReplace(b);
    AssertEq(a.GetInt("x"), 1, "MergeReplace keeps x");
    AssertEq(a.GetInt("y"), 99, "MergeReplace overwrites y");
    AssertEq(a.GetInt("z"), 3, "MergeReplace adds z");
    AssertEq(a.ObjectSize, 3, "MergeReplace correct size");
    b.Close();
    a.Close();
}

void Test_ArrayRemove() {
    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(10);
    arr.ArrayAppendInt(20);
    arr.ArrayAppendInt(30);

    Assert(arr.ArrayRemove(1), "ArrayRemove returns true");
    AssertEq(arr.ArrayLength, 2, "ArrayRemove length decreased");
    AssertEq(arr.ArrayGetInt(0), 10, "ArrayRemove first intact");
    AssertEq(arr.ArrayGetInt(1), 30, "ArrayRemove shifted");
    Assert(!arr.ArrayRemove(5), "ArrayRemove OOB returns false");
    arr.Close();
}

void Test_ArraySet() {
    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(1);
    arr.ArrayAppendInt(2);
    arr.ArrayAppendInt(3);

    arr.ArraySetString(0, "hello");
    arr.ArraySetInt(1, 42);
    arr.ArraySetBool(2, true);

    char buf[64];
    arr.ArrayGetString(0, buf, sizeof(buf));
    AssertStrEq(buf, "hello", "ArraySet string");
    AssertEq(arr.ArrayGetInt(1), 42, "ArraySet int");
    Assert(arr.ArrayGetBool(2), "ArraySet bool");
    AssertEq(arr.ArrayLength, 3, "ArraySet length unchanged");
    arr.Close();
}

void Test_ArrayClear() {
    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(1);
    arr.ArrayAppendInt(2);
    arr.ArrayAppendInt(3);
    arr.ArrayClear();
    AssertEq(arr.ArrayLength, 0, "ArrayClear empties array");

    // Can still use after clear
    arr.ArrayAppendString("new");
    AssertEq(arr.ArrayLength, 1, "ArrayClear array still usable");
    arr.Close();
}

void Test_ArrayExtend() {
    Json a = Json.CreateArray();
    a.ArrayAppendInt(1);
    a.ArrayAppendInt(2);

    Json b = Json.CreateArray();
    b.ArrayAppendInt(3);
    b.ArrayAppendInt(4);

    a.ArrayExtend(b);
    AssertEq(a.ArrayLength, 4, "ArrayExtend length");
    AssertEq(a.ArrayGetInt(0), 1, "ArrayExtend [0]");
    AssertEq(a.ArrayGetInt(2), 3, "ArrayExtend [2]");
    AssertEq(a.ArrayGetInt(3), 4, "ArrayExtend [3]");

    // Deep copy — mutating source shouldn't affect target
    b.ArrayClear();
    AssertEq(a.ArrayLength, 4, "ArrayExtend is deep copy");

    b.Close();
    a.Close();
}

// Int64 Tests

void Test_Int64_SetGet() {
    Json obj = Json.CreateObject();

    // Small value that fits in 32 bits
    int small[2];
    small[0] = 42;
    small[1] = 0;
    obj.SetInt64("small", small);

    int got[2];
    obj.GetInt64("small", got);
    AssertEq(got[0], 42, "Int64 SetGet small low");
    AssertEq(got[1], 0, "Int64 SetGet small high");

    // Negative value
    int neg[2];
    neg[0] = -1;  // 0xFFFFFFFF
    neg[1] = -1;  // 0xFFFFFFFF = -1 as int64
    obj.SetInt64("neg", neg);
    obj.GetInt64("neg", got);
    AssertEq(got[0], -1, "Int64 SetGet neg low");
    AssertEq(got[1], -1, "Int64 SetGet neg high");

    // Large value: 0x1_00000000 (4294967296)
    int large[2];
    large[0] = 0;
    large[1] = 1;
    obj.SetInt64("large", large);
    obj.GetInt64("large", got);
    AssertEq(got[0], 0, "Int64 SetGet large low");
    AssertEq(got[1], 1, "Int64 SetGet large high");

    // Zero
    int zero[2];
    zero[0] = 0;
    zero[1] = 0;
    obj.SetInt64("zero", zero);
    obj.GetInt64("zero", got);
    AssertEq(got[0], 0, "Int64 SetGet zero low");
    AssertEq(got[1], 0, "Int64 SetGet zero high");

    obj.Close();
}

void Test_Int64_Array() {
    Json arr = Json.CreateArray();

    // Append
    int val[2];
    val[0] = 100;
    val[1] = 0;
    arr.ArrayAppendInt64(val);

    val[0] = 0;
    val[1] = 2;  // 0x2_00000000
    arr.ArrayAppendInt64(val);

    AssertEq(arr.ArrayLength, 2, "Int64 array length");

    // Get
    int got[2];
    arr.ArrayGetInt64(0, got);
    AssertEq(got[0], 100, "Int64 ArrayGet [0] low");
    AssertEq(got[1], 0, "Int64 ArrayGet [0] high");

    arr.ArrayGetInt64(1, got);
    AssertEq(got[0], 0, "Int64 ArrayGet [1] low");
    AssertEq(got[1], 2, "Int64 ArrayGet [1] high");

    // Set
    val[0] = 999;
    val[1] = 0;
    arr.ArraySetInt64(0, val);
    arr.ArrayGetInt64(0, got);
    AssertEq(got[0], 999, "Int64 ArraySet low");
    AssertEq(got[1], 0, "Int64 ArraySet high");

    arr.Close();
}

void Test_Int64_PathGet() {
    // Parse JSON with a large integer
    Json json = Json.ParseString("{\"data\":{\"big\":999999999}}");
    int got[2];
    async2_JsonPathGetInt64(json, got, "data", "big");
    Assert(!async2_JsonPathFailed(), "Int64 PathGet no error");
    AssertEq(got[0], 999999999, "Int64 PathGet low");
    AssertEq(got[1], 0, "Int64 PathGet high");

    // Test PathGet failure
    async2_JsonPathGetInt64(json, got, "data", "missing");
    Assert(async2_JsonPathFailed(), "Int64 PathGet fails on missing key");

    json.Close();
}

// JSON Memory Tests

void Test_JsonMemorySize() {
    Json obj = Json.CreateObject();
    obj.SetString("key", "value");
    obj.SetInt("num", 42);

    int size = async2_JsonMemorySize(obj);
    Assert(size > 0, "JsonMemorySize > 0 for non-empty object");

    // Empty object should still have some size
    Json empty = Json.CreateObject();
    int empty_size = async2_JsonMemorySize(empty);
    Assert(empty_size > 0, "JsonMemorySize > 0 for empty object");
    Assert(size > empty_size, "JsonMemorySize populated > empty");

    empty.Close();
    obj.Close();
}

void RunJsonTests() {
    Test_CreateObject();
    Test_CreateArray();
    Test_SetGetString();
    Test_SetGetInt();
    Test_SetGetFloat();
    Test_SetGetBool();
    Test_SetNull();
    Test_HasKey();
    Test_ObjectSize();
    Test_OverwriteKey();
    Test_ArrayAppendAndGet();
    Test_ArrayOfObjects();
    Test_SetObject_Move();
    Test_GetObject_Shallow();
    Test_GetArray_Shallow();
    Test_GetObject_MissingKey();
    Test_GetArray_MissingKey();
    Test_ParseString();
    Test_ParseString_Array();
    Test_ParseString_Invalid();
    Test_ParseString_Empty();
    Test_Nested();
    Test_Equals();
    Test_Copy_Deep();
    Test_Serialize();
    Test_ObjectIteration();
    Test_ChildOutlivesParent();
    Test_MultipleTypes();
    Test_PathGetInt();
    Test_PathGetString();
    Test_PathGetFloat();
    Test_PathGetBool();
    Test_PathGet_Handle();
    Test_PathGetType();
    Test_PathFailed_MissingKey();
    Test_PathFailed_IndexOutOfBounds();
    Test_PathFailed_TraverseLeaf();
    Test_PathFailed_Clears();
    Test_PathGetString_Default();
    Test_PathGet_Empty();
    Test_RemoveKey();
    Test_ObjectClear();
    Test_ObjectMergeNew();
    Test_ObjectMergeReplace();
    Test_ArrayRemove();
    Test_ArraySet();
    Test_ArrayClear();
    Test_ArrayExtend();
    Test_Int64_SetGet();
    Test_Int64_Array();
    Test_Int64_PathGet();
    Test_JsonMemorySize();
}
