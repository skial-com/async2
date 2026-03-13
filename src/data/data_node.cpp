#include "data_node.h"
#include "data_node_pool.h"
#include "simdjson.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// ---------- Pool ----------

static FixedPool<sizeof(DataNode), alignof(DataNode)> g_pool;

// ---------- Factory methods ----------

DataNode* DataNode::MakeNull() {
    auto* n = new (g_pool.Alloc()) DataNode();
    return n;
}

DataNode* DataNode::MakeBool(bool v) {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::Bool;
    n->bool_val = v;
    return n;
}

DataNode* DataNode::MakeInt(int64_t v) {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::Int;
    n->int_val = v;
    return n;
}

DataNode* DataNode::MakeFloat(double v) {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::Float;
    n->float_val = v;
    return n;
}

DataNode* DataNode::MakeString(const char* v) {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::String;
    new (&n->str_val) std::string(v);
    return n;
}

DataNode* DataNode::MakeString(const char* v, size_t len) {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::String;
    new (&n->str_val) std::string(v, len);
    return n;
}

DataNode* DataNode::MakeArray() {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::Array;
    new (&n->arr) std::vector<DataNode*>();
    return n;
}

DataNode* DataNode::MakeObject() {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::Object;
    new (&n->obj) DataMap<std::string, DataNode*>();
    return n;
}

DataNode* DataNode::MakeIntMap() {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::IntMap;
    new (&n->intmap) DataMap<int64_t, DataNode*>();
    return n;
}

DataNode* DataNode::MakeBinary(const uint8_t* data, size_t len) {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::Binary;
    new (&n->bin) std::vector<uint8_t>(data, data + len);
    return n;
}

DataNode* DataNode::MakeBinary(std::vector<uint8_t>&& data) {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::Binary;
    new (&n->bin) std::vector<uint8_t>(std::move(data));
    return n;
}

// ---------- Destroy ----------

void DataNode::Destroy(DataNode* node) {
    if (!node) return;

    // If a child handle still references this node, orphan it instead of destroying
    if (node->refcount > 0) {
        node->orphaned = true;
        return;
    }

    switch (node->type) {
        case DataType::String:
            node->str_val.~basic_string();
            break;
        case DataType::Array:
            for (auto* child : node->arr)
                Destroy(child);
            node->arr.~vector();
            break;
        case DataType::Object:
            for (auto& [key, val] : node->obj)
                Destroy(val);
            using ObjMap = DataMap<std::string, DataNode*>;
            node->obj.~ObjMap();
            break;
        case DataType::IntMap:
            for (auto& [key, val] : node->intmap)
                Destroy(val);
            using IntMapType = DataMap<int64_t, DataNode*>;
            node->intmap.~IntMapType();
            break;
        case DataType::Binary:
            node->bin.~vector();
            break;
        default:
            break;
    }

    node->~DataNode();
    g_pool.Free(node);
}

// ---------- DeepCopy ----------

DataNode* DataNode::DeepCopy() const {
    switch (type) {
        case DataType::Null:   return MakeNull();
        case DataType::Bool:   return MakeBool(bool_val);
        case DataType::Int:    return MakeInt(int_val);
        case DataType::Float:  return MakeFloat(float_val);
        case DataType::String: return MakeString(str_val.c_str());
        case DataType::Array: {
            auto* n = MakeArray();
            n->arr.reserve(arr.size());
            for (const auto* elem : arr)
                n->arr.push_back(elem->DeepCopy());
            return n;
        }
        case DataType::Object: {
            auto* n = MakeObject();
            for (const auto& [key, val] : obj)
                n->ObjInsert(key, val->DeepCopy());
            return n;
        }
        case DataType::IntMap: {
            auto* n = MakeIntMap();
            for (const auto& [key, val] : intmap)
                n->IntMapInsert(key, val->DeepCopy());
            return n;
        }
        case DataType::Binary:
            return MakeBinary(bin.data(), bin.size());
    }
    return MakeNull();
}

// ---------- Equals ----------

bool DataNode::Equals(const DataNode* other) const {
    if (!other) return false;
    if (type != other->type) return false;

    switch (type) {
        case DataType::Null:   return true;
        case DataType::Bool:   return bool_val == other->bool_val;
        case DataType::Int:    return int_val == other->int_val;
        case DataType::Float:  return float_val == other->float_val;
        case DataType::String: return str_val == other->str_val;
        case DataType::Array: {
            if (arr.size() != other->arr.size()) return false;
            for (size_t i = 0; i < arr.size(); i++) {
                if (!arr[i]->Equals(other->arr[i])) return false;
            }
            return true;
        }
        case DataType::Object: {
            if (obj.size() != other->obj.size()) return false;
            for (const auto& [key, val] : obj) {
                auto it = other->obj.find(key);
                if (it == other->obj.end()) return false;
                if (!val->Equals(it->second)) return false;
            }
            return true;
        }
        case DataType::IntMap: {
            if (intmap.size() != other->intmap.size()) return false;
            for (const auto& [key, val] : intmap) {
                auto it = other->intmap.find(key);
                if (it == other->intmap.end()) return false;
                if (!val->Equals(it->second)) return false;
            }
            return true;
        }
        case DataType::Binary:
            return bin == other->bin;
    }
    return false;
}

// ---------- Object helpers ----------

DataNode* DataNode::ObjFind(const std::string& key) const {
    if (type != DataType::Object) return nullptr;
    auto it = obj.find(key);
    if (it == obj.end()) return nullptr;
    return it->second;
}

bool DataNode::ObjContains(const std::string& key) const {
    if (type != DataType::Object) return false;
    return obj.find(key) != obj.end();
}

void DataNode::ObjInsert(std::string key, DataNode* val) {
    if (type != DataType::Object) { Destroy(val); return; }
    auto [it, inserted] = obj.try_emplace(std::move(key), val);
    if (!inserted) {
        // Update in-place to preserve iterator validity
        Destroy(it->second);
        it.value() = val;
    }
}

bool DataNode::ObjErase(const std::string& key) {
    if (type != DataType::Object) return false;
    auto it = obj.find(key);
    if (it == obj.end()) return false;

    Destroy(it->second);
    obj.erase(it);
    return true;
}

size_t DataNode::ObjSize() const {
    if (type != DataType::Object) return 0;
    return obj.size();
}

void DataNode::ObjClear() {
    if (type != DataType::Object) return;
    for (auto& [key, val] : obj)
        Destroy(val);
    obj.clear();
}

void DataNode::ObjMerge(const DataNode* other, bool overwrite) {
    if (type != DataType::Object || !other || other->type != DataType::Object) return;
    for (const auto& [key, val] : other->obj) {
        auto it = obj.find(key);
        if (it != obj.end()) {
            if (overwrite) {
                Destroy(it->second);
                it.value() = val->DeepCopy();
            }
        } else {
            obj[key] = val->DeepCopy();
        }
    }
}

// ---------- IntMap helpers ----------

DataNode* DataNode::IntMapFind(int64_t key) const {
    if (type != DataType::IntMap) return nullptr;
    auto it = intmap.find(key);
    if (it == intmap.end()) return nullptr;
    return it->second;
}

bool DataNode::IntMapContains(int64_t key) const {
    if (type != DataType::IntMap) return false;
    return intmap.find(key) != intmap.end();
}

void DataNode::IntMapInsert(int64_t key, DataNode* val) {
    if (type != DataType::IntMap) { Destroy(val); return; }
    auto [it, inserted] = intmap.try_emplace(key, val);
    if (!inserted) {
        Destroy(it->second);
        it.value() = val;
    }
}

bool DataNode::IntMapErase(int64_t key) {
    if (type != DataType::IntMap) return false;
    auto it = intmap.find(key);
    if (it == intmap.end()) return false;

    Destroy(it->second);
    intmap.erase(it);
    return true;
}

size_t DataNode::IntMapSize() const {
    if (type != DataType::IntMap) return 0;
    return intmap.size();
}

void DataNode::IntMapClear() {
    if (type != DataType::IntMap) return;
    for (auto& [key, val] : intmap)
        Destroy(val);
    intmap.clear();
}

void DataNode::IntMapMerge(const DataNode* other, bool overwrite) {
    if (type != DataType::IntMap || !other || other->type != DataType::IntMap) return;
    for (const auto& [key, val] : other->intmap) {
        auto it = intmap.find(key);
        if (it != intmap.end()) {
            if (overwrite) {
                Destroy(it->second);
                it.value() = val->DeepCopy();
            }
        } else {
            intmap[key] = val->DeepCopy();
        }
    }
}

// ---------- Array helpers ----------

bool DataNode::ArrRemove(size_t index) {
    if (type != DataType::Array || index >= arr.size()) return false;
    Destroy(arr[index]);
    arr.erase(arr.begin() + index);
    return true;
}

void DataNode::ArrSet(size_t index, DataNode* val) {
    if (type != DataType::Array || index >= arr.size()) { Destroy(val); return; }
    Destroy(arr[index]);
    arr[index] = val;
}

void DataNode::ArrClear() {
    if (type != DataType::Array) return;
    for (auto* child : arr)
        Destroy(child);
    arr.clear();
}

void DataNode::ArrExtend(const DataNode* other) {
    if (type != DataType::Array || !other || other->type != DataType::Array) return;
    arr.reserve(arr.size() + other->arr.size());
    for (const auto* elem : other->arr)
        arr.push_back(elem->DeepCopy());
}

// ---------- EstimateBytes ----------

size_t DataNode::EstimateBytes() const {
    size_t bytes = sizeof(DataNode);

    switch (type) {
        case DataType::String:
            bytes += str_val.capacity();
            break;
        case DataType::Array:
            bytes += arr.capacity() * sizeof(DataNode*);
            for (const auto* child : arr)
                bytes += child->EstimateBytes();
            break;
        case DataType::Object:
            // robin_map: bucket_count entries, each holding key+value+metadata
            bytes += obj.bucket_count() * (sizeof(std::string) + sizeof(DataNode*) + 1);
            for (const auto& [key, val] : obj) {
                bytes += key.capacity();
                bytes += val->EstimateBytes();
            }
            break;
        case DataType::IntMap:
            bytes += intmap.bucket_count() * (sizeof(int64_t) + sizeof(DataNode*) + 1);
            for (const auto& [key, val] : intmap)
                bytes += val->EstimateBytes();
            break;
        case DataType::Binary:
            bytes += bin.capacity();
            break;
        default:
            break;
    }

    return bytes;
}

void DataPoolStats(size_t& total, size_t& free_blocks, size_t& block_size) {
    g_pool.Stats(total, free_blocks, block_size);
}

// ---------- Parser (simdjson ondemand) ----------

namespace {

static constexpr int kMaxParseDepth = 1024;

// Returns nullptr on error. Callers must Destroy(node) on nullptr return
// to clean up the partially-built tree.
static DataNode* od_to_node(simdjson::ondemand::value val, int depth = 0) {
    if (depth > kMaxParseDepth) return nullptr;
    auto tp = val.type();
    if (tp.error()) return nullptr;

    switch (tp.value()) {
        case simdjson::ondemand::json_type::null: {
            auto r = val.is_null();
            if (r.error()) return nullptr;
            return DataNode::MakeNull();
        }
        case simdjson::ondemand::json_type::boolean: {
            auto r = val.get_bool();
            if (r.error()) return nullptr;
            return DataNode::MakeBool(r.value());
        }
        case simdjson::ondemand::json_type::number: {
            auto i = val.get_int64();
            if (!i.error()) return DataNode::MakeInt(i.value());
            auto u = val.get_uint64();
            if (!u.error()) return DataNode::MakeInt(static_cast<int64_t>(u.value()));
            auto d = val.get_double();
            if (d.error()) return nullptr;
            return DataNode::MakeFloat(d.value());
        }
        case simdjson::ondemand::json_type::string: {
            auto sv = val.get_string();
            if (sv.error()) return nullptr;
            return DataNode::MakeString(sv.value().data(), sv.value().size());
        }
        case simdjson::ondemand::json_type::array: {
            auto* node = DataNode::MakeArray();
            auto arr = val.get_array();
            if (arr.error()) { DataNode::Destroy(node); return nullptr; }
            for (auto child : arr.value()) {
                if (child.error()) { DataNode::Destroy(node); return nullptr; }
                auto* child_node = od_to_node(child.value(), depth + 1);
                if (!child_node) { DataNode::Destroy(node); return nullptr; }
                node->arr.push_back(child_node);
            }
            return node;
        }
        case simdjson::ondemand::json_type::object: {
            auto* node = DataNode::MakeObject();
            auto obj_result = val.get_object();
            if (obj_result.error()) { DataNode::Destroy(node); return nullptr; }
            for (auto field : obj_result.value()) {
                if (field.error()) { DataNode::Destroy(node); return nullptr; }
                auto key = field.unescaped_key();
                if (key.error()) { DataNode::Destroy(node); return nullptr; }
                auto* val_node = od_to_node(field.value(), depth + 1);
                if (!val_node) { DataNode::Destroy(node); return nullptr; }
                auto kv = key.value();
                node->obj.emplace(std::string(kv.data(), kv.size()), val_node);
            }
            return node;
        }
    }
    return nullptr;
}

static DataNode* od_to_node_doc(simdjson::ondemand::document& doc) {
    auto tp = doc.type();
    if (tp.error()) return nullptr;

    switch (tp.value()) {
        case simdjson::ondemand::json_type::null: {
            auto r = doc.is_null();
            if (r.error() || !r.value()) return nullptr;
            return DataNode::MakeNull();
        }
        case simdjson::ondemand::json_type::boolean: {
            auto r = doc.get_bool();
            if (r.error()) return nullptr;
            return DataNode::MakeBool(r.value());
        }
        case simdjson::ondemand::json_type::number: {
            auto i = doc.get_int64();
            if (!i.error()) return DataNode::MakeInt(i.value());
            auto u = doc.get_uint64();
            if (!u.error()) return DataNode::MakeInt(static_cast<int64_t>(u.value()));
            auto d = doc.get_double();
            if (d.error()) return nullptr;
            return DataNode::MakeFloat(d.value());
        }
        case simdjson::ondemand::json_type::string: {
            auto sv = doc.get_string();
            if (sv.error()) return nullptr;
            return DataNode::MakeString(sv.value().data(), sv.value().size());
        }
        case simdjson::ondemand::json_type::array: {
            auto* node = DataNode::MakeArray();
            auto arr = doc.get_array();
            if (arr.error()) { DataNode::Destroy(node); return nullptr; }
            for (auto child : arr.value()) {
                if (child.error()) { DataNode::Destroy(node); return nullptr; }
                auto* child_node = od_to_node(child.value(), 1);
                if (!child_node) { DataNode::Destroy(node); return nullptr; }
                node->arr.push_back(child_node);
            }
            return node;
        }
        case simdjson::ondemand::json_type::object: {
            auto* node = DataNode::MakeObject();
            auto obj_result = doc.get_object();
            if (obj_result.error()) { DataNode::Destroy(node); return nullptr; }
            for (auto field : obj_result.value()) {
                if (field.error()) { DataNode::Destroy(node); return nullptr; }
                auto key = field.unescaped_key();
                if (key.error()) { DataNode::Destroy(node); return nullptr; }
                auto* val_node = od_to_node(field.value(), 1);
                if (!val_node) { DataNode::Destroy(node); return nullptr; }
                auto kv = key.value();
                node->obj.emplace(std::string(kv.data(), kv.size()), val_node);
            }
            return node;
        }
    }
    return nullptr;
}

} // anonymous namespace

// thread_local raw pointer — avoids __cxa_thread_atexit destructor registration
// which prevents dlclose from unmapping the .so. Each thread that calls
// DataParseJson gets its own parser. Call DataParserCleanup() on each thread
// before unload to free the memory.
static thread_local simdjson::ondemand::parser* tl_json_parser = nullptr;

DataNode* DataParseJson(const char* data, size_t len, std::string* error_out) {
    if (!data || len == 0)
        return nullptr;

    if (!tl_json_parser)
        tl_json_parser = new simdjson::ondemand::parser();

    simdjson::padded_string padded(data, len);
    auto doc = tl_json_parser->iterate(padded);
    if (doc.error()) {
        if (error_out)
            *error_out = simdjson::error_message(doc.error());
        return nullptr;
    }

    auto& docref = doc.value();

    return od_to_node_doc(docref);
}

void DataParserCleanup() {
    delete tl_json_parser;
    tl_json_parser = nullptr;
}

// ---------- Serializer ----------

namespace {

static void serialize_string(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
}

// Serialize leaf types (shared by compact and pretty paths)
static void serialize_leaf(const DataNode& node, std::string& out) {
    switch (node.type) {
        case DataType::Null:
            out += "null";
            break;
        case DataType::Bool:
            out += node.bool_val ? "true" : "false";
            break;
        case DataType::Int: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(node.int_val));
            out += buf;
            break;
        }
        case DataType::Float: {
            double val = node.float_val;
            if (std::isnan(val) || std::isinf(val)) {
                out += "null";
            } else {
                char buf[32];
                for (int prec = 1; prec <= 17; prec++) {
                    snprintf(buf, sizeof(buf), "%.*g", prec, val);
                    char* end;
                    double rt = strtod(buf, &end);
                    if (rt == val && std::signbit(rt) == std::signbit(val))
                        break;
                }
                char* ep = buf;
                while (*ep && *ep != 'e' && *ep != 'E') ep++;
                bool has_dot = false, has_exp = (*ep != '\0');
                for (const char* p = buf; p < ep; p++)
                    if (*p == '.') has_dot = true;
                if (has_exp && ep[1] == '+') {
                    memmove(ep + 1, ep + 2, strlen(ep + 2) + 1);
                }
                out += buf;
                if (!has_dot && !has_exp)
                    out += ".0";
            }
            break;
        }
        case DataType::String:
            serialize_string(node.str_val, out);
            break;
        case DataType::IntMap:
        case DataType::Binary:
            out += "null";
            break;
        default:
            break;
    }
}

static void serialize_value(const DataNode& node, std::string& out) {
    switch (node.type) {
        case DataType::Array:
            out += '[';
            for (size_t i = 0; i < node.arr.size(); i++) {
                if (i > 0) out += ',';
                serialize_value(*node.arr[i], out);
            }
            out += ']';
            break;
        case DataType::Object: {
            out += '{';
            bool first = true;
            for (const auto& [key, val] : node.obj) {
                if (!first) out += ',';
                first = false;
                serialize_string(key, out);
                out += ':';
                serialize_value(*val, out);
            }
            out += '}';
            break;
        }
        default:
            serialize_leaf(node, out);
            break;
    }
}

static void serialize_value_pretty(const DataNode& node, std::string& out, int depth) {
    switch (node.type) {
        case DataType::Array: {
            if (node.arr.empty()) {
                out += "[]";
            } else {
                out += "[\n";
                for (size_t i = 0; i < node.arr.size(); i++) {
                    if (i > 0) out += ",\n";
                    out.append((depth + 1) * 4, ' ');
                    serialize_value_pretty(*node.arr[i], out, depth + 1);
                }
                out += '\n';
                out.append(depth * 4, ' ');
                out += ']';
            }
            break;
        }
        case DataType::Object: {
            if (node.obj.empty()) {
                out += "{}";
            } else {
                out += "{\n";
                bool first = true;
                for (const auto& [key, val] : node.obj) {
                    if (!first) out += ",\n";
                    first = false;
                    out.append((depth + 1) * 4, ' ');
                    serialize_string(key, out);
                    out += ": ";
                    serialize_value_pretty(*val, out, depth + 1);
                }
                out += '\n';
                out.append(depth * 4, ' ');
                out += '}';
            }
            break;
        }
        default:
            serialize_leaf(node, out);
            break;
    }
}

} // anonymous namespace

std::string DataSerializeJson(const DataNode& node, bool pretty) {
    std::string result;
    if (pretty)
        serialize_value_pretty(node, result, 0);
    else
        serialize_value(node, result);
    return result;
}
