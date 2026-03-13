#ifndef ASYNC2_LINKED_LIST_H
#define ASYNC2_LINKED_LIST_H

#include <queue>
#include <cstdint>
#include <tsl/robin_map.h>

class LinkedList {
public:
    struct Node {
        int value;
        Node* prev;
        Node* next;
        uint32_t id;
    };

    LinkedList();
    ~LinkedList();

    uint32_t PushFront(int value);
    uint32_t PushBack(int value);
    int PopFront();
    int PopBack();
    void Remove(uint32_t node_id, int* out_value);
    void MoveToFront(uint32_t node_id);
    void MoveToBack(uint32_t node_id);
    int GetValue(uint32_t node_id) const;
    void SetValue(uint32_t node_id, int value);
    uint32_t First() const;
    uint32_t Last() const;
    uint32_t Next(uint32_t node_id) const;
    uint32_t Prev(uint32_t node_id) const;
    int Size() const;
    void Clear();

private:
    Node sentinel_;
    uint32_t next_id_ = 1;
    std::queue<uint32_t> free_ids_;
    tsl::robin_map<uint32_t, Node*> nodes_;

    Node* FindNode(uint32_t id) const;
    void InsertBefore(Node* pos, Node* node);
    void Unlink(Node* node);
    void RecycleId(uint32_t id);
    uint32_t NextId();
};

#endif
