#include "data_handle.h"
#include <cstring>

DataHandle::DataHandle(DataNode* owned)
    : root(std::shared_ptr<DataNode>(owned, DataNode::Destroy)), node(owned) {}

DataHandle::DataHandle(std::shared_ptr<DataNode> root, DataNode* node)
    : root(std::move(root)), node(node) {}

DataHandle::~DataHandle() {
    if (owns_refcount_ && node) {
        node->refcount--;
        if (node->refcount == 0 && node->orphaned) {
            DataNode::Destroy(node);
        }
    }
}

DataHandle* DataHandle::Parse(const char* data, size_t len) {
    auto* parsed = DataParseJson(data, len);
    if (!parsed)
        return nullptr;
    return new DataHandle(parsed);
}

DataHandle* DataHandle::ParseString(const char* str) {
    return Parse(str, strlen(str));
}

DataHandle* DataHandle::CreateObject() {
    return new DataHandle(DataNode::MakeObject());
}

DataHandle* DataHandle::CreateArray() {
    return new DataHandle(DataNode::MakeArray());
}

DataHandle* DataHandle::CreateIntMap() {
    return new DataHandle(DataNode::MakeIntMap());
}

Async2DataType DataHandle::GetType() const {
    if (!node) return JSON_TYPE_NONE;
    switch (node->type) {
        case DataType::Null:   return JSON_TYPE_NULL;
        case DataType::Bool:   return JSON_TYPE_BOOL;
        case DataType::Int:
        case DataType::Float:  return JSON_TYPE_NUMBER;
        case DataType::String: return JSON_TYPE_STRING;
        case DataType::Array:  return JSON_TYPE_ARRAY;
        case DataType::Object: return JSON_TYPE_OBJECT;
        case DataType::IntMap: return JSON_TYPE_INTOBJECT;
        case DataType::Binary: return JSON_TYPE_BINARY;
    }
    return JSON_TYPE_NONE;
}

size_t DataHandle::ObjectSize() const {
    if (!node) return 0;
    return node->ObjSize();
}

size_t DataHandle::ArrayLength() const {
    if (!node || node->type != DataType::Array) return 0;
    return node->arr.size();
}

bool DataHandle::HasKey(const char* key) const {
    if (!node) return false;
    return node->ObjContains(key);
}

const char* DataHandle::GetString(const char* key) const {
    if (!node) return "";
    const DataNode* v = node->ObjFind(key);
    if (!v || v->type != DataType::String) return "";
    return v->str_val.c_str();
}

int64_t DataHandle::GetInt(const char* key) const {
    if (!node) return 0;
    const DataNode* v = node->ObjFind(key);
    if (!v) return 0;
    if (v->type == DataType::Int) return v->int_val;
    if (v->type == DataType::Float) return static_cast<int64_t>(v->float_val);
    return 0;
}

double DataHandle::GetFloat(const char* key) const {
    if (!node) return 0.0;
    const DataNode* v = node->ObjFind(key);
    if (!v) return 0.0;
    if (v->type == DataType::Float) return v->float_val;
    if (v->type == DataType::Int) return static_cast<double>(v->int_val);
    return 0.0;
}

bool DataHandle::GetBool(const char* key) const {
    if (!node) return false;
    const DataNode* v = node->ObjFind(key);
    if (!v || v->type != DataType::Bool) return false;
    return v->bool_val;
}

DataNode* DataHandle::GetObjectNode(const char* key) const {
    if (!node) return nullptr;
    DataNode* child = node->ObjFind(key);
    if (!child || child->type != DataType::Object) return nullptr;
    return child;
}

DataNode* DataHandle::GetArrayNode(const char* key) const {
    if (!node) return nullptr;
    DataNode* child = node->ObjFind(key);
    if (!child || child->type != DataType::Array) return nullptr;
    return child;
}

// Array getters
const char* DataHandle::ArrayGetString(size_t index) const {
    if (!node || node->type != DataType::Array || index >= node->arr.size()) return "";
    const DataNode* v = node->arr[index];
    if (v->type != DataType::String) return "";
    return v->str_val.c_str();
}

int64_t DataHandle::ArrayGetInt(size_t index) const {
    if (!node || node->type != DataType::Array || index >= node->arr.size()) return 0;
    const DataNode* v = node->arr[index];
    if (v->type == DataType::Int) return v->int_val;
    if (v->type == DataType::Float) return static_cast<int64_t>(v->float_val);
    return 0;
}

double DataHandle::ArrayGetFloat(size_t index) const {
    if (!node || node->type != DataType::Array || index >= node->arr.size()) return 0.0;
    const DataNode* v = node->arr[index];
    if (v->type == DataType::Float) return v->float_val;
    if (v->type == DataType::Int) return static_cast<double>(v->int_val);
    return 0.0;
}

bool DataHandle::ArrayGetBool(size_t index) const {
    if (!node || node->type != DataType::Array || index >= node->arr.size()) return false;
    const DataNode* v = node->arr[index];
    if (v->type != DataType::Bool) return false;
    return v->bool_val;
}

DataNode* DataHandle::ArrayGetNode(size_t index) const {
    if (!node || node->type != DataType::Array || index >= node->arr.size()) return nullptr;
    return node->arr[index];
}

// IntMap getters — IntMapFind returns nullptr when type != IntMap,
// so no outer type check needed (matches Object getter style).
const char* DataHandle::IntMapGetString(int64_t key) const {
    if (!node) return "";
    const DataNode* v = node->IntMapFind(key);
    if (!v || v->type != DataType::String) return "";
    return v->str_val.c_str();
}

int64_t DataHandle::IntMapGetInt(int64_t key) const {
    if (!node) return 0;
    const DataNode* v = node->IntMapFind(key);
    if (!v) return 0;
    if (v->type == DataType::Int) return v->int_val;
    if (v->type == DataType::Float) return static_cast<int64_t>(v->float_val);
    return 0;
}

double DataHandle::IntMapGetFloat(int64_t key) const {
    if (!node) return 0.0;
    const DataNode* v = node->IntMapFind(key);
    if (!v) return 0.0;
    if (v->type == DataType::Float) return v->float_val;
    if (v->type == DataType::Int) return static_cast<double>(v->int_val);
    return 0.0;
}

bool DataHandle::IntMapGetBool(int64_t key) const {
    if (!node) return false;
    const DataNode* v = node->IntMapFind(key);
    if (!v || v->type != DataType::Bool) return false;
    return v->bool_val;
}

DataNode* DataHandle::IntMapGetObjectNode(int64_t key) const {
    if (!node) return nullptr;
    DataNode* child = node->IntMapFind(key);
    if (!child || child->type != DataType::Object) return nullptr;
    return child;
}

DataNode* DataHandle::IntMapGetArrayNode(int64_t key) const {
    if (!node) return nullptr;
    DataNode* child = node->IntMapFind(key);
    if (!child || child->type != DataType::Array) return nullptr;
    return child;
}

// IntMap setters
void DataHandle::IntMapSetString(int64_t key, const char* val) {
    if (!node || node->type != DataType::IntMap) return;
    node->IntMapInsert(key, DataNode::MakeString(val));
}

void DataHandle::IntMapSetInt(int64_t key, int64_t val) {
    if (!node || node->type != DataType::IntMap) return;
    node->IntMapInsert(key, DataNode::MakeInt(val));
}

void DataHandle::IntMapSetFloat(int64_t key, double val) {
    if (!node || node->type != DataType::IntMap) return;
    node->IntMapInsert(key, DataNode::MakeFloat(val));
}

void DataHandle::IntMapSetBool(int64_t key, bool val) {
    if (!node || node->type != DataType::IntMap) return;
    node->IntMapInsert(key, DataNode::MakeBool(val));
}

void DataHandle::IntMapSetNull(int64_t key) {
    if (!node || node->type != DataType::IntMap) return;
    node->IntMapInsert(key, DataNode::MakeNull());
}

// IntMap mutation
bool DataHandle::IntMapRemoveKey(int64_t key) {
    if (!node) return false;
    return node->IntMapErase(key);
}

void DataHandle::IntMapClear() {
    if (!node) return;
    node->IntMapClear();
}

void DataHandle::IntMapMerge(const DataNode* other, bool overwrite) {
    if (!node || !other) return;
    node->IntMapMerge(other, overwrite);
}

size_t DataHandle::IntMapSize() const {
    if (!node) return 0;
    return node->IntMapSize();
}

bool DataHandle::IntMapHasKey(int64_t key) const {
    if (!node) return false;
    return node->IntMapContains(key);
}

// Object mutation
bool DataHandle::RemoveKey(const char* key) {
    if (!node) return false;
    return node->ObjErase(key);
}

void DataHandle::ObjectClear() {
    if (!node) return;
    node->ObjClear();
}

void DataHandle::ObjectMerge(const DataNode* other, bool overwrite) {
    if (!node || !other) return;
    node->ObjMerge(other, overwrite);
}

// Array mutation
bool DataHandle::ArrayRemove(size_t index) {
    if (!node) return false;
    return node->ArrRemove(index);
}

void DataHandle::ArraySetString(size_t index, const char* val) {
    if (!node) return;
    node->ArrSet(index, DataNode::MakeString(val));
}

void DataHandle::ArraySetInt(size_t index, int64_t val) {
    if (!node) return;
    node->ArrSet(index, DataNode::MakeInt(val));
}

void DataHandle::ArraySetFloat(size_t index, double val) {
    if (!node) return;
    node->ArrSet(index, DataNode::MakeFloat(val));
}

void DataHandle::ArraySetBool(size_t index, bool val) {
    if (!node) return;
    node->ArrSet(index, DataNode::MakeBool(val));
}

void DataHandle::ArraySetNull(size_t index) {
    if (!node) return;
    node->ArrSet(index, DataNode::MakeNull());
}

void DataHandle::ArrayClear() {
    if (!node) return;
    node->ArrClear();
}

void DataHandle::ArrayExtend(const DataNode* other) {
    if (!node || !other) return;
    node->ArrExtend(other);
}

// Object setters
void DataHandle::SetString(const char* key, const char* val) {
    if (!node || node->type != DataType::Object) return;
    node->ObjInsert(key, DataNode::MakeString(val));
}

void DataHandle::SetInt(const char* key, int64_t val) {
    if (!node || node->type != DataType::Object) return;
    node->ObjInsert(key, DataNode::MakeInt(val));
}

void DataHandle::SetFloat(const char* key, double val) {
    if (!node || node->type != DataType::Object) return;
    node->ObjInsert(key, DataNode::MakeFloat(val));
}

void DataHandle::SetBool(const char* key, bool val) {
    if (!node || node->type != DataType::Object) return;
    node->ObjInsert(key, DataNode::MakeBool(val));
}

void DataHandle::SetNull(const char* key) {
    if (!node || node->type != DataType::Object) return;
    node->ObjInsert(key, DataNode::MakeNull());
}

// Array append
void DataHandle::ArrayAppendString(const char* val) {
    if (!node || node->type != DataType::Array) return;
    node->arr.push_back(DataNode::MakeString(val));
}

void DataHandle::ArrayAppendInt(int64_t val) {
    if (!node || node->type != DataType::Array) return;
    node->arr.push_back(DataNode::MakeInt(val));
}

void DataHandle::ArrayAppendFloat(double val) {
    if (!node || node->type != DataType::Array) return;
    node->arr.push_back(DataNode::MakeFloat(val));
}

void DataHandle::ArrayAppendBool(bool val) {
    if (!node || node->type != DataType::Array) return;
    node->arr.push_back(DataNode::MakeBool(val));
}

void DataHandle::ArrayAppendNull() {
    if (!node || node->type != DataType::Array) return;
    node->arr.push_back(DataNode::MakeNull());
}

// Serialize
bool DataHandle::Serialize(char* buf, size_t maxlen, bool pretty) const {
    if (!node || maxlen == 0)
        return false;

    std::string s = DataSerializeJson(*node, pretty);
    size_t copy_len = s.size() < maxlen - 1 ? s.size() : maxlen - 1;
    memcpy(buf, s.data(), copy_len);
    buf[copy_len] = '\0';
    return true;
}

std::string DataHandle::SerializeToString(bool pretty) const {
    if (!node) return "";
    return DataSerializeJson(*node, pretty);
}
