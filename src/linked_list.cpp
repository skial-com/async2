#include "linked_list.h"

LinkedList::LinkedList() {
    sentinel_.prev = &sentinel_;
    sentinel_.next = &sentinel_;
    sentinel_.id = 0;
    sentinel_.value = 0;
}

LinkedList::~LinkedList() {
    Clear();
}

LinkedList::Node* LinkedList::FindNode(uint32_t id) const {
    auto it = nodes_.find(id);
    return (it != nodes_.end()) ? it->second : nullptr;
}

void LinkedList::InsertBefore(Node* pos, Node* node) {
    node->prev = pos->prev;
    node->next = pos;
    pos->prev->next = node;
    pos->prev = node;
}

void LinkedList::Unlink(Node* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void LinkedList::RecycleId(uint32_t id) {
    free_ids_.push(id);
}

uint32_t LinkedList::NextId() {
    if (!free_ids_.empty()) {
        uint32_t id = free_ids_.front();
        free_ids_.pop();
        return id;
    }
    uint32_t id = next_id_++;
    if (id == 0) id = next_id_++;  // skip 0 (sentinel/invalid)
    return id;
}

uint32_t LinkedList::PushFront(int value) {
    Node* node = new Node();
    node->value = value;
    node->id = NextId();
    InsertBefore(sentinel_.next, node);
    nodes_[node->id] = node;
    return node->id;
}

uint32_t LinkedList::PushBack(int value) {
    Node* node = new Node();
    node->value = value;
    node->id = NextId();
    InsertBefore(&sentinel_, node);
    nodes_[node->id] = node;
    return node->id;
}

int LinkedList::PopFront() {
    if (nodes_.empty()) return 0;
    Node* node = sentinel_.next;
    int value = node->value;
    Unlink(node);
    nodes_.erase(node->id);
    RecycleId(node->id);
    delete node;
    return value;
}

int LinkedList::PopBack() {
    if (nodes_.empty()) return 0;
    Node* node = sentinel_.prev;
    int value = node->value;
    Unlink(node);
    nodes_.erase(node->id);
    RecycleId(node->id);
    delete node;
    return value;
}

void LinkedList::Remove(uint32_t node_id, int* out_value) {
    Node* node = FindNode(node_id);
    if (!node) {
        if (out_value) *out_value = 0;
        return;
    }
    if (out_value) *out_value = node->value;
    Unlink(node);
    nodes_.erase(node_id);
    RecycleId(node_id);
    delete node;
}

void LinkedList::MoveToFront(uint32_t node_id) {
    Node* node = FindNode(node_id);
    if (!node || node == sentinel_.next) return;
    Unlink(node);
    InsertBefore(sentinel_.next, node);
}

void LinkedList::MoveToBack(uint32_t node_id) {
    Node* node = FindNode(node_id);
    if (!node || node == sentinel_.prev) return;
    Unlink(node);
    InsertBefore(&sentinel_, node);
}

int LinkedList::GetValue(uint32_t node_id) const {
    Node* node = FindNode(node_id);
    return node ? node->value : 0;
}

void LinkedList::SetValue(uint32_t node_id, int value) {
    Node* node = FindNode(node_id);
    if (node) node->value = value;
}

uint32_t LinkedList::First() const {
    return (sentinel_.next != &sentinel_) ? sentinel_.next->id : 0;
}

uint32_t LinkedList::Last() const {
    return (sentinel_.prev != &sentinel_) ? sentinel_.prev->id : 0;
}

uint32_t LinkedList::Next(uint32_t node_id) const {
    Node* node = FindNode(node_id);
    if (!node || node->next == &sentinel_) return 0;
    return node->next->id;
}

uint32_t LinkedList::Prev(uint32_t node_id) const {
    Node* node = FindNode(node_id);
    if (!node || node->prev == &sentinel_) return 0;
    return node->prev->id;
}

int LinkedList::Size() const {
    return static_cast<int>(nodes_.size());
}

void LinkedList::Clear() {
    Node* node = sentinel_.next;
    while (node != &sentinel_) {
        Node* next = node->next;
        delete node;
        node = next;
    }
    sentinel_.next = &sentinel_;
    sentinel_.prev = &sentinel_;
    nodes_.clear();
    std::queue<uint32_t>().swap(free_ids_);
}
