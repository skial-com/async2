#include "data_iterator.h"

DataIterator* DataIterator::CreateObject(std::shared_ptr<DataNode> root, DataNode* node) {
    if (!node || node->type != DataType::Object) return nullptr;
    if (node->refcount >= DataNode::kMaxRefcount) return nullptr;
    auto* it = new DataIterator();
    it->type_ = IteratorType::Object;
    it->root_ = std::move(root);
    it->node_ = node;
    node->refcount++;
    new (&it->obj_iter_) decltype(it->obj_iter_)(node->obj.begin());
    return it;
}

DataIterator* DataIterator::CreateIntMap(std::shared_ptr<DataNode> root, DataNode* node) {
    if (!node || node->type != DataType::IntMap) return nullptr;
    if (node->refcount >= DataNode::kMaxRefcount) return nullptr;
    auto* it = new DataIterator();
    it->type_ = IteratorType::IntMap;
    it->root_ = std::move(root);
    it->node_ = node;
    node->refcount++;
    new (&it->intmap_iter_) decltype(it->intmap_iter_)(node->intmap.begin());
    return it;
}

DataIterator::~DataIterator() {
    if (type_ == IteratorType::Object)
        obj_iter_.~ObjIterType();
    else
        intmap_iter_.~IntMapIterType();

    node_->refcount--;
    if (node_->refcount == 0 && node_->orphaned) {
        DataNode::Destroy(node_);
    }
}

bool DataIterator::Next() {
    if (type_ == IteratorType::Object) {
        if (obj_iter_ == node_->obj.end())
            return false;
        obj_key_ = obj_iter_->first.c_str();
        ++obj_iter_;
        return true;
    } else {
        if (intmap_iter_ == node_->intmap.end())
            return false;
        intmap_key_ = intmap_iter_->first;
        ++intmap_iter_;
        return true;
    }
}

const char* DataIterator::ObjectKey() const {
    return obj_key_ ? obj_key_ : "";
}

int64_t DataIterator::IntMapKey() const {
    return intmap_key_;
}
