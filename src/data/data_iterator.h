#ifndef ASYNC2_DATA_ITERATOR_H
#define ASYNC2_DATA_ITERATOR_H

#include <memory>
#include "data_node.h"

enum class IteratorType { Object, IntMap };

class DataIterator {
public:
    using ObjIterType = DataMap<std::string, DataNode*>::const_iterator;
    using IntMapIterType = DataMap<int64_t, DataNode*>::const_iterator;

    static DataIterator* CreateObject(std::shared_ptr<DataNode> root, DataNode* node);
    static DataIterator* CreateIntMap(std::shared_ptr<DataNode> root, DataNode* node);
    ~DataIterator();

    bool Next();
    const char* ObjectKey() const;
    int64_t IntMapKey() const;
    IteratorType Type() const { return type_; }

private:
    DataIterator() : type_(IteratorType::Object), node_(nullptr) {}

    IteratorType type_;
    std::shared_ptr<DataNode> root_;  // keeps tree alive
    DataNode* node_;                  // container being iterated

    union {
        ObjIterType obj_iter_;
        IntMapIterType intmap_iter_;
    };
    const char* obj_key_ = nullptr;
    int64_t intmap_key_ = 0;
};

#endif
