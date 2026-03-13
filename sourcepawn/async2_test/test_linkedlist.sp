// LinkedList Tests — doubly-linked list: push, pop, traverse, move, remove

void Test_LinkedList_CreateClose() {
    LinkedList list = LinkedList.Create();
    Assert(view_as<int>(list) != 0, "LinkedList Create returns non-zero");
    AssertEq(list.Size, 0, "LinkedList Create is empty");
    list.Close();
}

void Test_LinkedList_PushFrontBack() {
    LinkedList list = LinkedList.Create();
    LinkedListNode n1 = list.PushFront(100);
    LinkedListNode n2 = list.PushBack(200);
    LinkedListNode n3 = list.PushFront(50);

    AssertEq(list.Size, 3, "LinkedList size after 3 pushes");
    Assert(n1 != INVALID_LINKED_LIST_NODE, "PushFront returns valid node");
    Assert(n2 != INVALID_LINKED_LIST_NODE, "PushBack returns valid node");
    Assert(n3 != INVALID_LINKED_LIST_NODE, "PushFront2 returns valid node");

    // Order should be: 50, 100, 200
    AssertEq(view_as<int>(list.GetValue(list.First())), 50, "First is 50");
    AssertEq(view_as<int>(list.GetValue(list.Last())), 200, "Last is 200");

    list.Close();
}

void Test_LinkedList_PopFrontBack() {
    LinkedList list = LinkedList.Create();
    list.PushBack(1);
    list.PushBack(2);
    list.PushBack(3);

    AssertEq(view_as<int>(list.PopFront()), 1, "PopFront first");
    AssertEq(view_as<int>(list.PopBack()), 3, "PopBack last");
    AssertEq(list.Size, 1, "Size after 2 pops");
    AssertEq(view_as<int>(list.PopFront()), 2, "PopFront remaining");
    AssertEq(list.Size, 0, "Size after all pops");

    list.Close();
}

void Test_LinkedList_Traversal() {
    LinkedList list = LinkedList.Create();
    list.PushBack(10);
    list.PushBack(20);
    list.PushBack(30);

    // Forward traversal
    LinkedListNode node = list.First();
    AssertEq(view_as<int>(list.GetValue(node)), 10, "Traverse first");
    node = list.Next(node);
    AssertEq(view_as<int>(list.GetValue(node)), 20, "Traverse second");
    node = list.Next(node);
    AssertEq(view_as<int>(list.GetValue(node)), 30, "Traverse third");
    node = list.Next(node);
    Assert(node == INVALID_LINKED_LIST_NODE, "Traverse past end");

    // Backward traversal
    node = list.Last();
    AssertEq(view_as<int>(list.GetValue(node)), 30, "Reverse last");
    node = list.Prev(node);
    AssertEq(view_as<int>(list.GetValue(node)), 20, "Reverse middle");
    node = list.Prev(node);
    AssertEq(view_as<int>(list.GetValue(node)), 10, "Reverse first");
    node = list.Prev(node);
    Assert(node == INVALID_LINKED_LIST_NODE, "Reverse past start");

    list.Close();
}

void Test_LinkedList_Remove() {
    LinkedList list = LinkedList.Create();
    list.PushBack(10);
    LinkedListNode n2 = list.PushBack(20);
    list.PushBack(30);

    int val = list.Remove(n2);
    AssertEq(val, 20, "Remove returns value");
    AssertEq(list.Size, 2, "Size after remove");

    // Check order: 10, 30
    AssertEq(view_as<int>(list.GetValue(list.First())), 10, "First after remove");
    AssertEq(view_as<int>(list.GetValue(list.Last())), 30, "Last after remove");

    list.Close();
}

void Test_LinkedList_MoveToFront() {
    LinkedList list = LinkedList.Create();
    list.PushBack(1);
    list.PushBack(2);
    LinkedListNode n3 = list.PushBack(3);

    list.MoveToFront(n3);

    // Order should be: 3, 1, 2
    AssertEq(view_as<int>(list.GetValue(list.First())), 3, "MoveToFront - first");
    LinkedListNode second = list.Next(list.First());
    AssertEq(view_as<int>(list.GetValue(second)), 1, "MoveToFront - second");
    AssertEq(view_as<int>(list.GetValue(list.Last())), 2, "MoveToFront - last");

    list.Close();
}

void Test_LinkedList_MoveToBack() {
    LinkedList list = LinkedList.Create();
    LinkedListNode n1 = list.PushBack(1);
    list.PushBack(2);
    list.PushBack(3);

    list.MoveToBack(n1);

    // Order should be: 2, 3, 1
    AssertEq(view_as<int>(list.GetValue(list.First())), 2, "MoveToBack - first");
    AssertEq(view_as<int>(list.GetValue(list.Last())), 1, "MoveToBack - last");

    list.Close();
}

void Test_LinkedList_SetValue() {
    LinkedList list = LinkedList.Create();
    LinkedListNode n = list.PushBack(42);

    list.SetValue(n, 99);
    AssertEq(view_as<int>(list.GetValue(n)), 99, "SetValue updates");

    list.Close();
}

void Test_LinkedList_Clear() {
    LinkedList list = LinkedList.Create();
    list.PushBack(1);
    list.PushBack(2);
    list.PushBack(3);

    list.Clear();
    AssertEq(list.Size, 0, "Size after clear");
    Assert(list.First() == INVALID_LINKED_LIST_NODE, "First after clear");
    Assert(list.Last() == INVALID_LINKED_LIST_NODE, "Last after clear");

    // Can still add after clear
    list.PushBack(42);
    AssertEq(list.Size, 1, "Size after push after clear");
    AssertEq(view_as<int>(list.GetValue(list.First())), 42, "Value after push after clear");

    list.Close();
}

void Test_LinkedList_Empty() {
    LinkedList list = LinkedList.Create();

    Assert(list.First() == INVALID_LINKED_LIST_NODE, "Empty First");
    Assert(list.Last() == INVALID_LINKED_LIST_NODE, "Empty Last");
    AssertEq(view_as<int>(list.PopFront()), 0, "Empty PopFront");
    AssertEq(view_as<int>(list.PopBack()), 0, "Empty PopBack");

    list.Close();
}

void Test_LinkedList_SingleElement() {
    LinkedList list = LinkedList.Create();
    LinkedListNode n = list.PushBack(42);

    Assert(list.First() == n, "Single First == node");
    Assert(list.Last() == n, "Single Last == node");
    Assert(list.Next(n) == INVALID_LINKED_LIST_NODE, "Single Next == invalid");
    Assert(list.Prev(n) == INVALID_LINKED_LIST_NODE, "Single Prev == invalid");

    // MoveToFront on single element should be no-op
    list.MoveToFront(n);
    AssertEq(view_as<int>(list.GetValue(list.First())), 42, "MoveToFront single");
    AssertEq(list.Size, 1, "Size unchanged after MoveToFront single");

    list.Close();
}

void Test_LinkedList_StableIds() {
    LinkedList list = LinkedList.Create();
    LinkedListNode n1 = list.PushBack(10);
    LinkedListNode n2 = list.PushBack(20);
    LinkedListNode n3 = list.PushBack(30);

    // Remove middle, IDs of remaining nodes should still work
    list.Remove(n2);
    AssertEq(view_as<int>(list.GetValue(n1)), 10, "n1 still valid after n2 remove");
    AssertEq(view_as<int>(list.GetValue(n3)), 30, "n3 still valid after n2 remove");

    // n1 and n3 should now be adjacent
    Assert(list.Next(n1) == n3, "n1->next == n3 after n2 remove");
    Assert(list.Prev(n3) == n1, "n3->prev == n1 after n2 remove");

    list.Close();
}

void RunLinkedListTests() {
    Test_LinkedList_CreateClose();
    Test_LinkedList_PushFrontBack();
    Test_LinkedList_PopFrontBack();
    Test_LinkedList_Traversal();
    Test_LinkedList_Remove();
    Test_LinkedList_MoveToFront();
    Test_LinkedList_MoveToBack();
    Test_LinkedList_SetValue();
    Test_LinkedList_Clear();
    Test_LinkedList_Empty();
    Test_LinkedList_SingleElement();
    Test_LinkedList_StableIds();
}
