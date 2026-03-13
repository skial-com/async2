// MsgPack Tests (synchronous)

void Test_MsgPack_RoundTrip() {
    // Create a JSON object, serialize to msgpack, parse back, verify
    Json obj = Json.CreateObject();
    obj.SetString("name", "test");
    obj.SetInt("count", 42);
    obj.SetBool("active", true);

    char buf[256];
    int len = async2_MsgPackSerialize(obj, buf, sizeof(buf));
    Assert(len > 0, "MsgPack serialize returns bytes");

    Json parsed = async2_MsgPackParseBuffer(buf, len);
    Assert(view_as<int>(parsed) != 0, "MsgPack parse returns handle");

    char name[64];
    parsed.GetString("name", name, sizeof(name));
    AssertStrEq(name, "test", "MsgPack roundtrip string");
    AssertEq(parsed.GetInt("count"), 42, "MsgPack roundtrip int");
    Assert(parsed.GetBool("active"), "MsgPack roundtrip bool");

    parsed.Close();
    obj.Close();
}

void Test_MsgPack_Array() {
    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(1);
    arr.ArrayAppendString("two");
    arr.ArrayAppendBool(true);

    char buf[256];
    int len = async2_MsgPackSerialize(arr, buf, sizeof(buf));
    Assert(len > 0, "MsgPack array serialize");

    Json parsed = async2_MsgPackParseBuffer(buf, len);
    AssertEq(parsed.ArrayLength, 3, "MsgPack array length");
    AssertEq(parsed.ArrayGetInt(0), 1, "MsgPack array[0]");

    char val[64];
    parsed.ArrayGetString(1, val, sizeof(val));
    AssertStrEq(val, "two", "MsgPack array[1]");

    Assert(parsed.ArrayGetBool(2), "MsgPack array[2]");

    parsed.Close();
    arr.Close();
}

void Test_MsgPack_Nested() {
    Json obj = Json.CreateObject();
    Json inner = Json.CreateArray();
    inner.ArrayAppendInt(10);
    inner.ArrayAppendInt(20);
    obj.SetObject("items", inner);
    inner.Close();

    char buf[256];
    int len = async2_MsgPackSerialize(obj, buf, sizeof(buf));

    Json parsed = async2_MsgPackParseBuffer(buf, len);
    Json items = parsed.GetArray("items");
    Assert(view_as<int>(items) != 0, "MsgPack nested array exists");
    AssertEq(items.ArrayLength, 2, "MsgPack nested array length");
    AssertEq(items.ArrayGetInt(0), 10, "MsgPack nested array[0]");

    items.Close();
    parsed.Close();
    obj.Close();
}

void Test_MsgPack_Empty() {
    // Empty object
    Json obj = Json.CreateObject();
    char buf[64];
    int len = async2_MsgPackSerialize(obj, buf, sizeof(buf));
    Assert(len > 0, "MsgPack empty object serialize");
    Json parsed = async2_MsgPackParseBuffer(buf, len);
    AssertEq(parsed.ObjectSize, 0, "MsgPack empty object size");
    parsed.Close();
    obj.Close();
}

void RunMsgPackTests() {
    Test_MsgPack_RoundTrip();
    Test_MsgPack_Array();
    Test_MsgPack_Nested();
    Test_MsgPack_Empty();
}
