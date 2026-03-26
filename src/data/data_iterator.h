#ifndef ASYNC2_DATA_ITERATOR_H
#define ASYNC2_DATA_ITERATOR_H

#include "data_node.h"

enum class IteratorType { Object, IntMap };

class DataIterator {
public:
    using ObjIterType = DataMap<std::string, DataNode*>::const_iterator;
    using IntMapIterType = DataMap<int64_t, DataNode*>::const_iterator;

    static DataIterator* CreateObject(DataNode* node);
    static DataIterator* CreateIntMap(DataNode* node);
    ~DataIterator();

    bool Next();
    const char* ObjectKey() const;
    int64_t IntMapKey() const;
    DataNode* Value() const { return value_; }
    IteratorType Type() const { return type_; }

private:
    DataIterator() : type_(IteratorType::Object), node_(nullptr), version_(0) {}

    IteratorType type_;
    DataNode* node_;                  // container being iterated (refcount incremented)
    uint32_t version_;                // snapshot of node_->version at last Next()

    union {
        ObjIterType obj_iter_;
        IntMapIterType intmap_iter_;
    };
    const char* obj_key_ = nullptr;
    int64_t intmap_key_ = 0;
    DataNode* value_ = nullptr;
};

#endif
