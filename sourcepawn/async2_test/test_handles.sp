// Handle ownership tests — SetHandlePlugin
// Note: Cross-plugin behavior can't be tested within a single plugin.
// These tests verify the API works on same-plugin handles.

void Test_SetHandlePlugin_Json() {
    Json obj = Json.CreateObject();
    obj.SetInt("x", 42);

    int result = async2_SetHandlePlugin(view_as<int>(obj));
    AssertEq(result, 1, "SetHandlePlugin on Json returns 1");

    // Handle still works after transfer to same plugin
    AssertEq(obj.GetInt("x"), 42, "Json still readable after SetHandlePlugin");

    obj.Close();
}

void Test_SetHandlePlugin_LinkedList() {
    LinkedList list = LinkedList.Create();
    list.PushBack(10);

    int result = async2_SetHandlePlugin(view_as<int>(list));
    AssertEq(result, 1, "SetHandlePlugin on LinkedList returns 1");

    // Handle still works
    AssertEq(list.Size, 1, "LinkedList still readable after SetHandlePlugin");

    list.Close();
}

void Test_SetHandlePlugin_InvalidHandle() {
    int result = async2_SetHandlePlugin(99999);
    AssertEq(result, 0, "SetHandlePlugin on invalid handle returns 0");
}

void Test_SetHandlePlugin_ClosedHandle() {
    Json obj = Json.CreateObject();
    obj.Close();

    int result = async2_SetHandlePlugin(view_as<int>(obj));
    AssertEq(result, 0, "SetHandlePlugin on closed handle returns 0");
}

void Test_JsonCopy_IndependentOwnership() {
    Json src = Json.CreateObject();
    src.SetString("key", "hello");

    Json copy = src.Copy();
    Assert(view_as<int>(copy) != 0, "JsonCopy returns non-zero");

    // Copy is independent
    copy.SetString("key", "world");
    char buf[64];
    src.GetString("key", buf, sizeof(buf));
    AssertStrEq(buf, "hello", "Source unchanged after copy mutation");

    copy.GetString("key", buf, sizeof(buf));
    AssertStrEq(buf, "world", "Copy has mutated value");

    src.Close();

    // Copy survives source close
    copy.GetString("key", buf, sizeof(buf));
    AssertStrEq(buf, "world", "Copy still works after source close");

    copy.Close();
}

void RunHandleTests() {
    Test_SetHandlePlugin_Json();
    Test_SetHandlePlugin_LinkedList();
    Test_SetHandlePlugin_InvalidHandle();
    Test_SetHandlePlugin_ClosedHandle();
    Test_JsonCopy_IndependentOwnership();
}
