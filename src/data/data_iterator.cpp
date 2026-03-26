#include "data_iterator.h"

DataIterator* DataIterator::CreateObject(DataNode* node) {
    if (!node || node->type != DataType::Object) return nullptr;
    auto* it = new DataIterator();
    it->type_ = IteratorType::Object;
    it->node_ = node;
    it->version_ = node->version;
    node->Incref();
    new (&it->obj_iter_) decltype(it->obj_iter_)(node->Obj().begin());
    return it;
}

DataIterator* DataIterator::CreateIntMap(DataNode* node) {
    if (!node || node->type != DataType::IntMap) return nullptr;
    auto* it = new DataIterator();
    it->type_ = IteratorType::IntMap;
    it->node_ = node;
    it->version_ = node->version;
    node->Incref();
    new (&it->intmap_iter_) decltype(it->intmap_iter_)(node->Intmap().begin());
    return it;
}

DataIterator::~DataIterator() {
    if (type_ == IteratorType::Object)
        obj_iter_.~ObjIterType();
    else
        intmap_iter_.~IntMapIterType();

    DataNode::Decref(node_);
}

bool DataIterator::Next() {
    if (node_->version != version_)
        return false;  // container mutated — iterator invalidated
    if (type_ == IteratorType::Object) {
        if (obj_iter_ == node_->Obj().end())
            return false;
        obj_key_ = obj_iter_->first.c_str();
        value_ = obj_iter_->second;
        ++obj_iter_;
        return true;
    } else {
        if (intmap_iter_ == node_->Intmap().end())
            return false;
        intmap_key_ = intmap_iter_->first;
        value_ = intmap_iter_->second;
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
