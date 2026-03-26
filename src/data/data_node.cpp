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
    n->obj_ptr = new DataMap<std::string, DataNode*>();
    return n;
}

DataNode* DataNode::MakeIntMap() {
    auto* n = new (g_pool.Alloc()) DataNode();
    n->type = DataType::IntMap;
    n->intmap_ptr = new DataMap<int64_t, DataNode*>();
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

// ---------- Decref ----------

void DataNode::Decref(DataNode* node) {
    if (!node) return;

    if (node->refcount.fetch_sub(1, std::memory_order_acq_rel) > 1)
        return;  // other references still exist

    // refcount hit 0 — destroy this node and recursively Decref children
    switch (node->type) {
        case DataType::String:
            node->str_val.~basic_string();
            break;
        case DataType::Array:
            for (auto* child : node->arr)
                Decref(child);
            node->arr.~vector();
            break;
        case DataType::Object:
            for (auto& [key, val] : *node->obj_ptr)
                Decref(val);
            delete node->obj_ptr;
            break;
        case DataType::IntMap:
            for (auto& [key, val] : *node->intmap_ptr)
                Decref(val);
            delete node->intmap_ptr;
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
        case DataType::String: return MakeString(Str().c_str());
        case DataType::Array: {
            auto* n = MakeArray();
            n->Arr().reserve(Arr().size());
            for (const auto* elem : Arr())
                n->Arr().push_back(elem->DeepCopy());
            return n;
        }
        case DataType::Object: {
            auto* n = MakeObject();
            for (const auto& [key, val] : Obj())
                n->ObjInsert(key, val->DeepCopy());
            return n;
        }
        case DataType::IntMap: {
            auto* n = MakeIntMap();
            for (const auto& [key, val] : Intmap())
                n->IntMapInsert(key, val->DeepCopy());
            return n;
        }
        case DataType::Binary:
            return MakeBinary(Bin().data(), Bin().size());
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
        case DataType::String: return Str() == other->Str();
        case DataType::Array: {
            if (Arr().size() != other->Arr().size()) return false;
            for (size_t i = 0; i < Arr().size(); i++) {
                if (!Arr()[i]->Equals(other->Arr()[i])) return false;
            }
            return true;
        }
        case DataType::Object: {
            if (Obj().size() != other->Obj().size()) return false;
            for (const auto& [key, val] : Obj()) {
                auto it = other->Obj().find(key);
                if (it == other->Obj().end()) return false;
                if (!val->Equals(it->second)) return false;
            }
            return true;
        }
        case DataType::IntMap: {
            if (Intmap().size() != other->Intmap().size()) return false;
            for (const auto& [key, val] : Intmap()) {
                auto it = other->Intmap().find(key);
                if (it == other->Intmap().end()) return false;
                if (!val->Equals(it->second)) return false;
            }
            return true;
        }
        case DataType::Binary:
            return Bin() == other->Bin();
    }
    return false;
}

// ---------- Object helpers ----------

DataNode* DataNode::ObjFind(const std::string& key) const {
    if (type != DataType::Object) return nullptr;
    auto it = Obj().find(key);
    if (it == Obj().end()) return nullptr;
    return it->second;
}

bool DataNode::ObjContains(const std::string& key) const {
    if (type != DataType::Object) return false;
    return Obj().find(key) != Obj().end();
}

void DataNode::ObjInsert(std::string key, DataNode* val) {
    if (type != DataType::Object) { Decref(val); return; }
    version++;
    auto [it, inserted] = Obj().try_emplace(std::move(key), val);
    if (!inserted) {
        Decref(it->second);
        it.value() = val;
    }
}

bool DataNode::ObjErase(const std::string& key) {
    if (type != DataType::Object) return false;
    auto it = Obj().find(key);
    if (it == Obj().end()) return false;

    version++;
    Decref(it->second);
    Obj().erase(it);
    return true;
}

size_t DataNode::ObjSize() const {
    if (type != DataType::Object) return 0;
    return Obj().size();
}

void DataNode::ObjClear() {
    if (type != DataType::Object) return;
    version++;
    for (auto& [key, val] : Obj())
        Decref(val);
    Obj().clear();
}

void DataNode::ObjMerge(const DataNode* other, bool overwrite) {
    if (type != DataType::Object || !other || other->type != DataType::Object) return;
    for (const auto& [key, val] : other->Obj()) {
        auto it = Obj().find(key);
        if (it != Obj().end()) {
            if (overwrite) {
                Decref(it->second);
                it.value() = val->DeepCopy();
            }
        } else {
            Obj()[key] = val->DeepCopy();
        }
    }
}

// ---------- IntMap helpers ----------

DataNode* DataNode::IntMapFind(int64_t key) const {
    if (type != DataType::IntMap) return nullptr;
    auto it = Intmap().find(key);
    if (it == Intmap().end()) return nullptr;
    return it->second;
}

bool DataNode::IntMapContains(int64_t key) const {
    if (type != DataType::IntMap) return false;
    return Intmap().find(key) != Intmap().end();
}

void DataNode::IntMapInsert(int64_t key, DataNode* val) {
    if (type != DataType::IntMap) { Decref(val); return; }
    version++;
    auto [it, inserted] = Intmap().try_emplace(key, val);
    if (!inserted) {
        Decref(it->second);
        it.value() = val;
    }
}

bool DataNode::IntMapErase(int64_t key) {
    if (type != DataType::IntMap) return false;
    auto it = Intmap().find(key);
    if (it == Intmap().end()) return false;

    version++;
    Decref(it->second);
    Intmap().erase(it);
    return true;
}

size_t DataNode::IntMapSize() const {
    if (type != DataType::IntMap) return 0;
    return Intmap().size();
}

void DataNode::IntMapClear() {
    if (type != DataType::IntMap) return;
    version++;
    for (auto& [key, val] : Intmap())
        Decref(val);
    Intmap().clear();
}

void DataNode::IntMapMerge(const DataNode* other, bool overwrite) {
    if (type != DataType::IntMap || !other || other->type != DataType::IntMap) return;
    for (const auto& [key, val] : other->Intmap()) {
        auto it = Intmap().find(key);
        if (it != Intmap().end()) {
            if (overwrite) {
                Decref(it->second);
                it.value() = val->DeepCopy();
            }
        } else {
            Intmap()[key] = val->DeepCopy();
        }
    }
}

// ---------- Array helpers ----------

bool DataNode::ArrRemove(size_t index) {
    if (type != DataType::Array || index >= Arr().size()) return false;
    Decref(Arr()[index]);
    Arr().erase(Arr().begin() + index);
    return true;
}

void DataNode::ArrSet(size_t index, DataNode* val) {
    if (type != DataType::Array || index >= Arr().size()) { Decref(val); return; }
    Decref(Arr()[index]);
    Arr()[index] = val;
}

void DataNode::ArrClear() {
    if (type != DataType::Array) return;
    for (auto* child : Arr())
        Decref(child);
    Arr().clear();
}

void DataNode::ArrExtend(const DataNode* other) {
    if (type != DataType::Array || !other || other->type != DataType::Array) return;
    Arr().reserve(Arr().size() + other->Arr().size());
    for (const auto* elem : other->Arr())
        Arr().push_back(elem->DeepCopy());
}

// ---------- EstimateBytes ----------

size_t DataNode::EstimateBytes() const {
    size_t bytes = sizeof(DataNode);

    switch (type) {
        case DataType::String:
            bytes += Str().capacity();
            break;
        case DataType::Array:
            bytes += Arr().capacity() * sizeof(DataNode*);
            for (const auto* child : Arr())
                bytes += child->EstimateBytes();
            break;
        case DataType::Object:
            // robin_map: bucket_count entries, each holding key+value+metadata
            bytes += sizeof(DataMap<std::string, DataNode*>);
            bytes += Obj().bucket_count() * (sizeof(std::string) + sizeof(DataNode*) + 1);
            for (const auto& [key, val] : Obj()) {
                bytes += key.capacity();
                bytes += val->EstimateBytes();
            }
            break;
        case DataType::IntMap:
            bytes += sizeof(DataMap<int64_t, DataNode*>);
            bytes += Intmap().bucket_count() * (sizeof(int64_t) + sizeof(DataNode*) + 1);
            for (const auto& [key, val] : Intmap())
                bytes += val->EstimateBytes();
            break;
        case DataType::Binary:
            bytes += Bin().capacity();
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

// Returns nullptr on error. Callers must Decref(node) on nullptr return
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
            if (arr.error()) { DataNode::Decref(node); return nullptr; }
            for (auto child : arr.value()) {
                if (child.error()) { DataNode::Decref(node); return nullptr; }
                auto* child_node = od_to_node(child.value(), depth + 1);
                if (!child_node) { DataNode::Decref(node); return nullptr; }
                node->Arr().push_back(child_node);
            }
            return node;
        }
        case simdjson::ondemand::json_type::object: {
            auto* node = DataNode::MakeObject();
            auto obj_result = val.get_object();
            if (obj_result.error()) { DataNode::Decref(node); return nullptr; }
            for (auto field : obj_result.value()) {
                if (field.error()) { DataNode::Decref(node); return nullptr; }
                auto key = field.unescaped_key();
                if (key.error()) { DataNode::Decref(node); return nullptr; }
                auto* val_node = od_to_node(field.value(), depth + 1);
                if (!val_node) { DataNode::Decref(node); return nullptr; }
                auto kv = key.value();
                node->Obj().emplace(std::string(kv.data(), kv.size()), val_node);
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
            if (arr.error()) { DataNode::Decref(node); return nullptr; }
            for (auto child : arr.value()) {
                if (child.error()) { DataNode::Decref(node); return nullptr; }
                auto* child_node = od_to_node(child.value(), 1);
                if (!child_node) { DataNode::Decref(node); return nullptr; }
                node->Arr().push_back(child_node);
            }
            return node;
        }
        case simdjson::ondemand::json_type::object: {
            auto* node = DataNode::MakeObject();
            auto obj_result = doc.get_object();
            if (obj_result.error()) { DataNode::Decref(node); return nullptr; }
            for (auto field : obj_result.value()) {
                if (field.error()) { DataNode::Decref(node); return nullptr; }
                auto key = field.unescaped_key();
                if (key.error()) { DataNode::Decref(node); return nullptr; }
                auto* val_node = od_to_node(field.value(), 1);
                if (!val_node) { DataNode::Decref(node); return nullptr; }
                auto kv = key.value();
                node->Obj().emplace(std::string(kv.data(), kv.size()), val_node);
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
            serialize_string(node.Str(), out);
            break;
        case DataType::IntMap:
        case DataType::Binary:
            out += "null";
            break;
        default:
            break;
    }
}

static constexpr int kMaxSerializeDepth = 128;

static void serialize_value(const DataNode& node, std::string& out, int depth = 0) {
    if (depth > kMaxSerializeDepth) { out += "null"; return; }
    switch (node.type) {
        case DataType::Array:
            out += '[';
            for (size_t i = 0; i < node.Arr().size(); i++) {
                if (i > 0) out += ',';
                serialize_value(*node.Arr()[i], out, depth + 1);
            }
            out += ']';
            break;
        case DataType::Object: {
            out += '{';
            bool first = true;
            for (const auto& [key, val] : node.Obj()) {
                if (!first) out += ',';
                first = false;
                serialize_string(key, out);
                out += ':';
                serialize_value(*val, out, depth + 1);
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
    if (depth > kMaxSerializeDepth) { out += "null"; return; }
    switch (node.type) {
        case DataType::Array: {
            if (node.Arr().empty()) {
                out += "[]";
            } else {
                out += "[\n";
                for (size_t i = 0; i < node.Arr().size(); i++) {
                    if (i > 0) out += ",\n";
                    out.append((depth + 1) * 4, ' ');
                    serialize_value_pretty(*node.Arr()[i], out, depth + 1);
                }
                out += '\n';
                out.append(depth * 4, ' ');
                out += ']';
            }
            break;
        }
        case DataType::Object: {
            if (node.Obj().empty()) {
                out += "{}";
            } else {
                out += "{\n";
                bool first = true;
                for (const auto& [key, val] : node.Obj()) {
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
