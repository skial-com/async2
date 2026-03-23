// Unit tests for LinkedList::DeepCopy
//
// HandleManager tests require SourceMod SDK headers (curl, etc.) and are
// tested via the SourcePawn test plugin instead (test_handles.sp).

#include "linked_list.h"
#include <cstdio>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_passed++; } \
    else { g_failed++; fprintf(stderr, "FAIL: %s\n", msg); } \
} while(0)

static void Test_DeepCopy_Empty() {
    LinkedList src;
    LinkedList* copy = src.DeepCopy();
    CHECK(copy != nullptr, "DeepCopy of empty list returns non-null");
    CHECK(copy->Size() == 0, "DeepCopy of empty list is empty");
    CHECK(copy->First() == 0, "DeepCopy of empty list First() == 0");
    CHECK(copy->Last() == 0, "DeepCopy of empty list Last() == 0");
    delete copy;
}

static void Test_DeepCopy_Values() {
    LinkedList src;
    src.PushBack(10);
    src.PushBack(20);
    src.PushBack(30);

    LinkedList* copy = src.DeepCopy();
    CHECK(copy->Size() == 3, "Copy has 3 elements");

    CHECK(copy->GetValue(copy->First()) == 10, "Copy first is 10");
    auto second = copy->Next(copy->First());
    CHECK(copy->GetValue(second) == 20, "Copy second is 20");
    CHECK(copy->GetValue(copy->Last()) == 30, "Copy last is 30");

    delete copy;
}

static void Test_DeepCopy_Independence() {
    LinkedList src;
    src.PushBack(1);
    src.PushBack(2);

    LinkedList* copy = src.DeepCopy();

    // Modify copy — source unaffected
    copy->PushBack(3);
    CHECK(copy->Size() == 3, "Copy has 3 after append");
    CHECK(src.Size() == 2, "Source still has 2 after copy append");

    // Modify source — copy unaffected
    src.PopFront();
    CHECK(src.Size() == 1, "Source has 1 after pop");
    CHECK(copy->Size() == 3, "Copy still has 3 after source pop");

    delete copy;
}

static void Test_DeepCopy_PreservesOrder() {
    LinkedList src;
    src.PushBack(100);
    src.PushBack(200);
    src.PushBack(300);
    src.PushBack(400);
    src.PushBack(500);

    LinkedList* copy = src.DeepCopy();

    // Forward traversal matches
    auto sn = src.First();
    auto cn = copy->First();
    int count = 0;
    while (sn != 0 && cn != 0) {
        CHECK(src.GetValue(sn) == copy->GetValue(cn), "Forward values match");
        sn = src.Next(sn);
        cn = copy->Next(cn);
        count++;
    }
    CHECK(count == 5, "Traversed 5 elements");
    CHECK(sn == 0 && cn == 0, "Both ended at same time");

    // Backward traversal matches
    sn = src.Last();
    cn = copy->Last();
    count = 0;
    while (sn != 0 && cn != 0) {
        CHECK(src.GetValue(sn) == copy->GetValue(cn), "Backward values match");
        sn = src.Prev(sn);
        cn = copy->Prev(cn);
        count++;
    }
    CHECK(count == 5, "Backward traversed 5 elements");

    delete copy;
}

static void Test_DeepCopy_SingleElement() {
    LinkedList src;
    src.PushBack(42);

    LinkedList* copy = src.DeepCopy();
    CHECK(copy->Size() == 1, "Copy has 1 element");
    CHECK(copy->GetValue(copy->First()) == 42, "Copy value is 42");
    CHECK(copy->First() == copy->Last(), "First == Last for single element");
    CHECK(copy->Next(copy->First()) == 0, "Next of only element is 0");
    CHECK(copy->Prev(copy->First()) == 0, "Prev of only element is 0");

    delete copy;
}

int main() {
    Test_DeepCopy_Empty();
    Test_DeepCopy_Values();
    Test_DeepCopy_Independence();
    Test_DeepCopy_PreservesOrder();
    Test_DeepCopy_SingleElement();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
