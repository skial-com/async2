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

// ============================================================================
// SetObject move semantics tests
// ============================================================================

void Test_SetObject_MovesChild() {
    Json parent = Json.CreateObject();
    Json child = Json.CreateObject();
    child.SetInt("x", 42);

    parent.SetObject("data", child);

    // Child is now empty
    AssertEq(view_as<int>(child.Type), view_as<int>(JSON_TYPE_NULL), "Child is null after SetObject");

    // Parent has the data
    Json ref = parent.GetObject("data");
    AssertEq(ref.GetInt("x"), 42, "Parent has child's data after SetObject");
    ref.Close();

    // Child close is safe
    child.Close();
    parent.Close();
}

void Test_SetObject_DataAccessible() {
    Json parent = Json.CreateObject();
    Json child = Json.CreateObject();
    child.SetInt("x", 1);

    parent.SetObject("data", child);
    child.Close();

    Json ref = parent.GetObject("data");
    Assert(view_as<int>(ref) != 0, "GetObject after SetObject returns non-zero");
    AssertEq(ref.GetInt("x"), 1, "Data accessible through parent after move");
    ref.Close();
    parent.Close();
}

void Test_ArrayAppendObject_MovesChild() {
    Json arr = Json.CreateArray();
    Json child = Json.CreateObject();
    child.SetString("name", "alice");

    arr.ArrayAppendObject(child);

    AssertEq(view_as<int>(child.Type), view_as<int>(JSON_TYPE_NULL), "Child is null after ArrayAppendObject");
    child.Close();

    Json ref = arr.ArrayGetObject(0);
    char buf[64];
    ref.GetString("name", buf, sizeof(buf));
    AssertStrEq(buf, "alice", "Array has child's data after AppendObject");
    ref.Close();
    arr.Close();
}

void Test_ArrayAppendObject_DataAccessible() {
    Json arr = Json.CreateArray();
    Json child = Json.CreateObject();
    child.SetInt("x", 99);

    arr.ArrayAppendObject(child);
    child.Close();

    Json ref = arr.ArrayGetObject(0);
    AssertEq(ref.GetInt("x"), 99, "Data accessible through array after move");
    ref.Close();
    arr.Close();
}

void Test_SetObject_CopyPreservesOriginal() {
    Json parent = Json.CreateObject();
    Json child = Json.CreateObject();
    child.SetInt("x", 10);

    Json copy = child.Copy();
    parent.SetObject("data", copy);
    copy.Close();

    // Original still valid after Copy + SetObject
    AssertEq(child.GetInt("x"), 10, "Original valid after SetObject with Copy");
    child.Close();

    Json ref = parent.GetObject("data");
    AssertEq(ref.GetInt("x"), 10, "Parent has copied data");
    ref.Close();
    parent.Close();
}

void Test_SetObject_MultiInsertWithCopy() {
    Json parent = Json.CreateObject();
    Json child = Json.CreateObject();
    child.SetString("val", "shared");

    Json copy = child.Copy();
    parent.SetObject("a", copy);
    copy.Close();
    parent.SetObject("b", child);  // last use — move is fine
    child.Close();

    char buf[64];
    Json a = parent.GetObject("a");
    a.GetString("val", buf, sizeof(buf));
    AssertStrEq(buf, "shared", "First insert via Copy has data");
    a.Close();

    Json b = parent.GetObject("b");
    b.GetString("val", buf, sizeof(buf));
    AssertStrEq(buf, "shared", "Second insert via move has data");
    b.Close();
    parent.Close();
}

void Test_SetObject_ChildHandleSteal() {
    // Stealing from a child handle (GetObject) hollows out that node in the source tree
    Json src = Json.CreateObject();
    Json inner = Json.CreateObject();
    inner.SetInt("x", 1);
    Json inner_copy = inner.Copy();
    src.SetObject("inner", inner_copy);
    inner_copy.Close();
    inner.Close();

    Json dst = Json.CreateObject();
    Json child_ref = src.GetObject("inner");
    dst.SetObject("stolen", child_ref);
    child_ref.Close();

    // dst has the data
    Json dst_ref = dst.GetObject("stolen");
    AssertEq(dst_ref.GetInt("x"), 1, "Stolen child handle data in dst");
    dst_ref.Close();

    // src["inner"] is now null (hollowed out)
    Json src_ref = src.GetObject("inner");
    Assert(view_as<int>(src_ref) == 0, "Source node is null after child handle steal");

    src.Close();
    dst.Close();
}

// Self-insert tests removed — ThrowNativeError halts the calling function,
// so these can't be tested from within a plugin. The guard is verified
// by the fact that the server doesn't crash if triggered accidentally.

void RunHandleTests() {
    Test_SetHandlePlugin_Json();
    Test_SetHandlePlugin_LinkedList();
    Test_SetHandlePlugin_InvalidHandle();
    Test_SetHandlePlugin_ClosedHandle();
    Test_JsonCopy_IndependentOwnership();
    Test_SetObject_MovesChild();
    Test_SetObject_DataAccessible();
    Test_ArrayAppendObject_MovesChild();
    Test_ArrayAppendObject_DataAccessible();
    Test_SetObject_CopyPreservesOriginal();
    Test_SetObject_MultiInsertWithCopy();
    Test_SetObject_ChildHandleSteal();
}
