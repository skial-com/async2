#ifndef ASYNC2_DATA_NODE_H
#define ASYNC2_DATA_NODE_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "data_map.h"

enum class DataType {
    Null,
    Bool,
    Int,
    Float,
    String,
    Array,
    Object,
    IntMap,
    Binary
};

struct DataNode {
    DataType type : 8;
    uint32_t version : 24;           // mutation counter for iterator invalidation detection
    std::atomic<uint32_t> refcount;  // per-node lifetime refcount (atomic for cross-thread Decref)

    // Tagged union: only the member matching `type` is active.
    union {
        bool bool_val;
        int64_t int_val;
        double float_val;
        std::string str_val;
        std::vector<DataNode*> arr;
        DataMap<std::string, DataNode*> obj;
        DataMap<int64_t, DataNode*> intmap;
        std::vector<uint8_t> bin;
    };

    DataNode() : type(DataType::Null), version(0), refcount(1), int_val(0) {}
    ~DataNode() {} // no-op — caller must use Decref()

    DataNode(const DataNode&) = delete;
    DataNode& operator=(const DataNode&) = delete;

    // Factory methods (allocate from pool)
    static DataNode* MakeNull();
    static DataNode* MakeBool(bool v);
    static DataNode* MakeInt(int64_t v);
    static DataNode* MakeFloat(double v);
    static DataNode* MakeString(const char* v);
    static DataNode* MakeString(const char* v, size_t len);
    static DataNode* MakeArray();
    static DataNode* MakeObject();
    static DataNode* MakeIntMap();
    static DataNode* MakeBinary(const uint8_t* data, size_t len);
    static DataNode* MakeBinary(std::vector<uint8_t>&& data);

    // Increment reference count (caller takes a new reference).
    void Incref() { refcount.fetch_add(1, std::memory_order_relaxed); }

    // Decrement reference count. If it reaches zero, recursively Decref children and free.
    static void Decref(DataNode* node);

    DataNode* DeepCopy() const;
    bool Equals(const DataNode* other) const;
    size_t EstimateBytes() const;

    // Object helpers — valid only when type == Object
    DataNode* ObjFind(const std::string& key) const;
    bool ObjContains(const std::string& key) const;
    void ObjInsert(std::string key, DataNode* val); // takes ownership
    bool ObjErase(const std::string& key);
    size_t ObjSize() const;
    void ObjClear();
    void ObjMerge(const DataNode* other, bool overwrite);

    // IntMap helpers — valid only when type == IntMap
    DataNode* IntMapFind(int64_t key) const;
    bool IntMapContains(int64_t key) const;
    void IntMapInsert(int64_t key, DataNode* val); // takes ownership
    bool IntMapErase(int64_t key);
    size_t IntMapSize() const;
    void IntMapClear();
    void IntMapMerge(const DataNode* other, bool overwrite);

    // Array helpers — valid only when type == Array
    bool ArrRemove(size_t index);
    void ArrSet(size_t index, DataNode* val); // takes ownership, destroys old
    void ArrClear();
    void ArrExtend(const DataNode* other); // deep copies elements from other
};

DataNode* DataParseJson(const char* data, size_t len, std::string* error_out = nullptr);
void DataParserCleanup(); // call on each thread that used DataParseJson before unload
std::string DataSerializeJson(const DataNode& node, bool pretty = false);
void DataPoolStats(size_t& total, size_t& free_blocks, size_t& block_size);

#endif
