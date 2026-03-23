#if defined _async2_linkedlist_included
    #endinput
#endif
#define _async2_linkedlist_included

// ============================================================================
// LinkedList Natives
// ============================================================================
// A general-purpose doubly-linked list with O(1) node access by ID.
// Useful for building LRU caches, ordered queues, and other data structures
// that need efficient insertion, removal, and reordering.
//
// Node IDs are stable — they don't change when other nodes are added/removed.
// ID 0 (INVALID_LINKED_LIST_NODE) means invalid/end-of-list.

enum LinkedListNode {
    INVALID_LINKED_LIST_NODE = 0
};

// Creates a new empty linked list. Must be closed with async2_LinkedListClose.
native LinkedList async2_LinkedListCreate();

// Closes a linked list and frees all nodes.
native int async2_LinkedListClose(LinkedList handle);

// Returns the number of nodes in the list.
native int async2_LinkedListSize(LinkedList handle);

// Removes all nodes from the list.
native int async2_LinkedListClear(LinkedList handle);

// Inserts a value at the front. Returns the new node's ID.
native LinkedListNode async2_LinkedListPushFront(LinkedList handle, any value);

// Inserts a value at the back. Returns the new node's ID.
native LinkedListNode async2_LinkedListPushBack(LinkedList handle, any value);

// Removes and returns the value at the front. Returns 0 if empty.
native any async2_LinkedListPopFront(LinkedList handle);

// Removes and returns the value at the back. Returns 0 if empty.
native any async2_LinkedListPopBack(LinkedList handle);

// Removes a node by ID. Returns its value.
native any async2_LinkedListRemove(LinkedList handle, LinkedListNode node);

// Moves a node to the front of the list.
native int async2_LinkedListMoveToFront(LinkedList handle, LinkedListNode node);

// Moves a node to the back of the list.
native int async2_LinkedListMoveToBack(LinkedList handle, LinkedListNode node);

// Gets the value stored in a node.
native any async2_LinkedListGetValue(LinkedList handle, LinkedListNode node);

// Sets the value stored in a node.
native int async2_LinkedListSetValue(LinkedList handle, LinkedListNode node, any value);

// Returns the first node's ID, or INVALID_LINKED_LIST_NODE if empty.
native LinkedListNode async2_LinkedListFirst(LinkedList handle);

// Returns the last node's ID, or INVALID_LINKED_LIST_NODE if empty.
native LinkedListNode async2_LinkedListLast(LinkedList handle);

// Returns the next node's ID, or INVALID_LINKED_LIST_NODE at end.
native LinkedListNode async2_LinkedListNext(LinkedList handle, LinkedListNode node);

// Returns the previous node's ID, or INVALID_LINKED_LIST_NODE at start.
native LinkedListNode async2_LinkedListPrev(LinkedList handle, LinkedListNode node);

// Creates an independent deep copy of the linked list.
// The new list is owned by the calling plugin. Must be closed with async2_LinkedListClose.
// Node IDs in the copy are independent from the original.
native LinkedList async2_LinkedListCopy(LinkedList handle);

methodmap LinkedList < Handle {
    public static LinkedList Create() {
        return view_as<LinkedList>(async2_LinkedListCreate());
    }

    public void Close() {
        async2_LinkedListClose(this);
    }

    property int Size {
        public get() { return async2_LinkedListSize(this); }
    }

    public void Clear() {
        async2_LinkedListClear(this);
    }

    public LinkedListNode PushFront(any value) {
        return async2_LinkedListPushFront(this, value);
    }

    public LinkedListNode PushBack(any value) {
        return async2_LinkedListPushBack(this, value);
    }

    public any PopFront() {
        return async2_LinkedListPopFront(this);
    }

    public any PopBack() {
        return async2_LinkedListPopBack(this);
    }

    public any Remove(LinkedListNode node) {
        return async2_LinkedListRemove(this, node);
    }

    public void MoveToFront(LinkedListNode node) {
        async2_LinkedListMoveToFront(this, node);
    }

    public void MoveToBack(LinkedListNode node) {
        async2_LinkedListMoveToBack(this, node);
    }

    public any GetValue(LinkedListNode node) {
        return async2_LinkedListGetValue(this, node);
    }

    public void SetValue(LinkedListNode node, any value) {
        async2_LinkedListSetValue(this, node, value);
    }

    public LinkedListNode First() {
        return async2_LinkedListFirst(this);
    }

    public LinkedListNode Last() {
        return async2_LinkedListLast(this);
    }

    public LinkedListNode Next(LinkedListNode node) {
        return async2_LinkedListNext(this, node);
    }

    public LinkedListNode Prev(LinkedListNode node) {
        return async2_LinkedListPrev(this, node);
    }

    public LinkedList Copy() {
        return view_as<LinkedList>(async2_LinkedListCopy(this));
    }
}
