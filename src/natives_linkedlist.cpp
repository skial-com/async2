#include "smsdk_ext.h"
#include "natives.h"

// async2_LinkedListCreate()
static cell_t Native_LinkedListCreate(IPluginContext* pContext, const cell_t* params) {
    LinkedList* list = new LinkedList();
    int handle = g_handle_manager.CreateHandle(list, HANDLE_LINKED_LIST);
    if (handle == 0) {
        delete list;
        return 0;
    }
    return handle;
}

// async2_LinkedListClose(LinkedList handle)
static cell_t Native_LinkedListClose(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    g_handle_manager.FreeHandle(params[1]);
    return 0;
}

// async2_LinkedListSize(LinkedList handle)
static cell_t Native_LinkedListSize(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return list->Size();
}

// async2_LinkedListClear(LinkedList handle)
static cell_t Native_LinkedListClear(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    list->Clear();
    return 0;
}

// async2_LinkedListPushFront(LinkedList handle, any value)
static cell_t Native_LinkedListPushFront(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return static_cast<cell_t>(list->PushFront(params[2]));
}

// async2_LinkedListPushBack(LinkedList handle, any value)
static cell_t Native_LinkedListPushBack(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return static_cast<cell_t>(list->PushBack(params[2]));
}

// async2_LinkedListPopFront(LinkedList handle)
static cell_t Native_LinkedListPopFront(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return list->PopFront();
}

// async2_LinkedListPopBack(LinkedList handle)
static cell_t Native_LinkedListPopBack(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return list->PopBack();
}

// async2_LinkedListRemove(LinkedList handle, LinkedListNode node)
static cell_t Native_LinkedListRemove(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    int value;
    list->Remove(static_cast<uint32_t>(params[2]), &value);
    return value;
}

// async2_LinkedListMoveToFront(LinkedList handle, LinkedListNode node)
static cell_t Native_LinkedListMoveToFront(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    list->MoveToFront(static_cast<uint32_t>(params[2]));
    return 0;
}

// async2_LinkedListMoveToBack(LinkedList handle, LinkedListNode node)
static cell_t Native_LinkedListMoveToBack(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    list->MoveToBack(static_cast<uint32_t>(params[2]));
    return 0;
}

// async2_LinkedListGetValue(LinkedList handle, LinkedListNode node)
static cell_t Native_LinkedListGetValue(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return list->GetValue(static_cast<uint32_t>(params[2]));
}

// async2_LinkedListSetValue(LinkedList handle, LinkedListNode node, any value)
static cell_t Native_LinkedListSetValue(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    list->SetValue(static_cast<uint32_t>(params[2]), params[3]);
    return 0;
}

// async2_LinkedListFirst(LinkedList handle)
static cell_t Native_LinkedListFirst(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return static_cast<cell_t>(list->First());
}

// async2_LinkedListLast(LinkedList handle)
static cell_t Native_LinkedListLast(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return static_cast<cell_t>(list->Last());
}

// async2_LinkedListNext(LinkedList handle, LinkedListNode node)
static cell_t Native_LinkedListNext(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return static_cast<cell_t>(list->Next(static_cast<uint32_t>(params[2])));
}

// async2_LinkedListPrev(LinkedList handle, LinkedListNode node)
static cell_t Native_LinkedListPrev(IPluginContext* pContext, const cell_t* params) {
    GET_LINKED_LIST()
    return static_cast<cell_t>(list->Prev(static_cast<uint32_t>(params[2])));
}

sp_nativeinfo_t g_LinkedListNatives[] = {
    {"async2_LinkedListCreate",      Native_LinkedListCreate},
    {"async2_LinkedListClose",       Native_LinkedListClose},
    {"async2_LinkedListSize",        Native_LinkedListSize},
    {"async2_LinkedListClear",       Native_LinkedListClear},
    {"async2_LinkedListPushFront",   Native_LinkedListPushFront},
    {"async2_LinkedListPushBack",    Native_LinkedListPushBack},
    {"async2_LinkedListPopFront",    Native_LinkedListPopFront},
    {"async2_LinkedListPopBack",     Native_LinkedListPopBack},
    {"async2_LinkedListRemove",      Native_LinkedListRemove},
    {"async2_LinkedListMoveToFront", Native_LinkedListMoveToFront},
    {"async2_LinkedListMoveToBack",  Native_LinkedListMoveToBack},
    {"async2_LinkedListGetValue",    Native_LinkedListGetValue},
    {"async2_LinkedListSetValue",    Native_LinkedListSetValue},
    {"async2_LinkedListFirst",       Native_LinkedListFirst},
    {"async2_LinkedListLast",        Native_LinkedListLast},
    {"async2_LinkedListNext",        Native_LinkedListNext},
    {"async2_LinkedListPrev",        Native_LinkedListPrev},
    {nullptr,                        nullptr},
};
