#ifndef ASYNC2_DATA_HANDLE_H
#define ASYNC2_DATA_HANDLE_H

#include <string>
#include "data_node.h"

enum Async2DataType {
    JSON_TYPE_NONE = 0,
    JSON_TYPE_NULL,
    JSON_TYPE_BOOL,
    JSON_TYPE_NUMBER,
    JSON_TYPE_STRING,
    JSON_TYPE_ARRAY,
    JSON_TYPE_OBJECT,
    JSON_TYPE_BINARY,
    JSON_TYPE_INTOBJECT,
};

class DataHandle {
public:
    DataNode* node;                 // the node this handle accesses (refcount managed externally)

    // Takes ownership — caller's refcount transfers to this handle.
    explicit DataHandle(DataNode* owned);
    ~DataHandle();

    static DataHandle* Parse(const char* data, size_t len);
    static DataHandle* ParseString(const char* str);
    static DataHandle* CreateObject();
    static DataHandle* CreateArray();
    static DataHandle* CreateIntMap();

    Async2DataType GetType() const;
    size_t ObjectSize() const;
    size_t ArrayLength() const;
    bool HasKey(const char* key) const;

    // Object getters
    const char* GetString(const char* key) const;
    int64_t GetInt(const char* key) const;
    double GetFloat(const char* key) const;
    bool GetBool(const char* key) const;
    DataNode* GetObjectNode(const char* key) const;
    DataNode* GetArrayNode(const char* key) const;

    // Array getters
    const char* ArrayGetString(size_t index) const;
    int64_t ArrayGetInt(size_t index) const;
    double ArrayGetFloat(size_t index) const;
    bool ArrayGetBool(size_t index) const;
    DataNode* ArrayGetNode(size_t index) const;

    // IntMap getters
    const char* IntMapGetString(int64_t key) const;
    int64_t IntMapGetInt(int64_t key) const;
    double IntMapGetFloat(int64_t key) const;
    bool IntMapGetBool(int64_t key) const;
    DataNode* IntMapGetObjectNode(int64_t key) const;
    DataNode* IntMapGetArrayNode(int64_t key) const;

    // IntMap setters
    void IntMapSetString(int64_t key, const char* val);
    void IntMapSetInt(int64_t key, int64_t val);
    void IntMapSetFloat(int64_t key, double val);
    void IntMapSetBool(int64_t key, bool val);
    void IntMapSetNull(int64_t key);

    // IntMap mutation
    bool IntMapRemoveKey(int64_t key);
    void IntMapClear();
    void IntMapMerge(const DataNode* other, bool overwrite);
    size_t IntMapSize() const;
    bool IntMapHasKey(int64_t key) const;

    // Object setters
    void SetString(const char* key, const char* val);
    void SetInt(const char* key, int64_t val);
    void SetFloat(const char* key, double val);
    void SetBool(const char* key, bool val);
    void SetNull(const char* key);

    // Object mutation
    bool RemoveKey(const char* key);
    void ObjectClear();
    void ObjectMerge(const DataNode* other, bool overwrite);

    // Array mutation
    bool ArrayRemove(size_t index);
    void ArraySetString(size_t index, const char* val);
    void ArraySetInt(size_t index, int64_t val);
    void ArraySetFloat(size_t index, double val);
    void ArraySetBool(size_t index, bool val);
    void ArraySetNull(size_t index);
    void ArrayClear();
    void ArrayExtend(const DataNode* other);

    // Array append
    void ArrayAppendString(const char* val);
    void ArrayAppendInt(int64_t val);
    void ArrayAppendFloat(double val);
    void ArrayAppendBool(bool val);
    void ArrayAppendNull();

    // Serialize
    size_t Serialize(char* buf, size_t maxlen, bool pretty = false) const;
    std::string SerializeToString(bool pretty = false) const;
};

// Map DataNode::type to the SourcePawn-visible JsonType enum.
inline Async2DataType NodeToType(const DataNode* n) {
    if (!n) return JSON_TYPE_NONE;
    switch (n->type) {
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

// DeepCopy the node tree for handoff to the event thread.
// Always copies to avoid races when a child node is shared via JsonRef/GetObject.
inline DataNode* CopyNodeForSend(DataHandle* dh) {
    return dh->node->DeepCopy();
}

#endif
