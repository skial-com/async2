// Templated BenchNode for benchmarking different map+hash configurations.
//
// BenchNode<Map> mirrors JsonNode but takes the map type as a template parameter,
// allowing the same benchmark code to run with different map/hash combos.

#ifndef BENCH_NODE_H
#define BENCH_NODE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ============================================================
// mumx hash — MUM-based string hash (multiply-unfold-multiply)
// ============================================================

struct mumx_hash {
    static uint64_t mumx(uint64_t a, uint64_t b) {
        __uint128_t r = (__uint128_t)a * b;
        return (uint64_t)r ^ (uint64_t)(r >> 64);
    }

    size_t operator()(const std::string& s) const noexcept {
        static constexpr uint64_t k0 = 0xd6e8feb86659fd93ULL;
        static constexpr uint64_t k1 = 0x14020a57acced8b7ULL;

        uint64_t h = k0;
        const char* p = s.data();
        size_t len = s.size();

        while (len >= 8) {
            uint64_t v;
            memcpy(&v, p, 8);
            h = mumx(h ^ v, k1);
            p += 8;
            len -= 8;
        }

        if (len > 0) {
            uint64_t v = 0;
            memcpy(&v, p, len);
            h = mumx(h ^ v, k1);
        }

        return (size_t)h;
    }
};

// ============================================================
// xxh3 hash — XXH3_64bits wrapper
// ============================================================

#define XXH_INLINE_ALL
#include "xxhash.h"

struct xxh3_hash {
    size_t operator()(const std::string& s) const noexcept {
        return (size_t)XXH3_64bits(s.data(), s.size());
    }
};

// ============================================================
// Map configurations — each bundles a map + hash into Map<K,V>
// ============================================================

#include "flat_hash_map.hpp"
#include "robin_hood.h"
#include "hash_table8.hpp"
#include "tsl/robin_map.h"
#include "ankerl/unordered_dense.h"

// --- ska::flat_hash_map with each hash (isolate hash comparison) ---

template<typename K, typename V>
using SkaRobinMap = ska::flat_hash_map<K, V, robin_hood::hash<K>>;

template<typename K, typename V>
using SkaMumxMap = ska::flat_hash_map<K, V, mumx_hash>;

template<typename K, typename V>
using SkaAnkerlMap = ska::flat_hash_map<K, V, ankerl::unordered_dense::hash<K>>;

template<typename K, typename V>
using SkaXxh3Map = ska::flat_hash_map<K, V, xxh3_hash>;

// --- XXH3 across all maps (isolate map comparison) ---

template<typename K, typename V>
using Emhash8Xxh3Map = emhash8::HashMap<K, V, xxh3_hash>;

template<typename K, typename V>
using TslXxh3Map = tsl::robin_map<K, V, xxh3_hash>;

template<typename K, typename V>
using AnkerlXxh3Map = ankerl::unordered_dense::map<K, V, xxh3_hash>;

// --- Other cross-map configs ---

template<typename K, typename V>
using Emhash8MumxMap = emhash8::HashMap<K, V, mumx_hash>;

template<typename K, typename V>
using TslAnkerlMap = tsl::robin_map<K, V, ankerl::unordered_dense::hash<K>>;

template<typename K, typename V>
using TslMumxMap = tsl::robin_map<K, V, mumx_hash>;

// ============================================================
// BenchNode<Map> — tagged union DOM node, templated on map type
// ============================================================

enum class BenchType { Null, Bool, Int, Float, String, Array, Object };

template<template<typename, typename> class Map>
struct BenchNode {
    using ObjMap = Map<std::string, BenchNode*>;

    BenchType type;

    union {
        bool bool_val;
        int64_t int_val;
        double float_val;
        std::string str_val;
        std::vector<BenchNode*> arr;
        ObjMap obj;
    };

    BenchNode() : type(BenchType::Null), int_val(0) {}
    ~BenchNode() {}

    BenchNode(const BenchNode&) = delete;
    BenchNode& operator=(const BenchNode&) = delete;

    // --- Factory methods ---

    static BenchNode* MakeNull() {
        return new BenchNode();
    }

    static BenchNode* MakeBool(bool v) {
        auto* n = new BenchNode();
        n->type = BenchType::Bool;
        n->bool_val = v;
        return n;
    }

    static BenchNode* MakeInt(int64_t v) {
        auto* n = new BenchNode();
        n->type = BenchType::Int;
        n->int_val = v;
        return n;
    }

    static BenchNode* MakeFloat(double v) {
        auto* n = new BenchNode();
        n->type = BenchType::Float;
        n->float_val = v;
        return n;
    }

    static BenchNode* MakeString(const char* v) {
        auto* n = new BenchNode();
        n->type = BenchType::String;
        new (&n->str_val) std::string(v);
        return n;
    }

    static BenchNode* MakeArray() {
        auto* n = new BenchNode();
        n->type = BenchType::Array;
        new (&n->arr) std::vector<BenchNode*>();
        return n;
    }

    static BenchNode* MakeObject() {
        auto* n = new BenchNode();
        n->type = BenchType::Object;
        new (&n->obj) ObjMap();
        return n;
    }

    // --- Destroy ---

    static void Destroy(BenchNode* node) {
        if (!node) return;
        switch (node->type) {
            case BenchType::String:
                node->str_val.~basic_string();
                break;
            case BenchType::Array:
                for (auto* child : node->arr)
                    Destroy(child);
                node->arr.~vector();
                break;
            case BenchType::Object:
                for (auto& [key, val] : node->obj)
                    Destroy(val);
                node->obj.~ObjMap();
                break;
            default:
                break;
        }
        delete node;
    }

    // --- Object helpers ---

    BenchNode* ObjFind(const std::string& key) const {
        auto it = obj.find(key);
        return it != obj.end() ? it->second : nullptr;
    }

    void ObjInsert(const std::string& key, BenchNode* val) {
        auto it = obj.find(key);
        if (it != obj.end()) {
            Destroy(it->second);
            obj.erase(it);
        }
        obj[key] = val;
    }

    bool ObjErase(const std::string& key) {
        auto it = obj.find(key);
        if (it == obj.end()) return false;
        Destroy(it->second);
        obj.erase(it);
        return true;
    }
};

// ============================================================
// simdjson DOM -> BenchNode converter (templated)
// ============================================================

#include "simdjson.h"

template<template<typename, typename> class Map>
static BenchNode<Map>* simdjson_dom_to_bench(simdjson::dom::element elem) {
    using Node = BenchNode<Map>;
    switch (elem.type()) {
        case simdjson::dom::element_type::NULL_VALUE:
            return Node::MakeNull();
        case simdjson::dom::element_type::BOOL:
            return Node::MakeBool(elem.get_bool().value());
        case simdjson::dom::element_type::INT64:
            return Node::MakeInt(elem.get_int64().value());
        case simdjson::dom::element_type::UINT64:
            return Node::MakeInt(static_cast<int64_t>(elem.get_uint64().value()));
        case simdjson::dom::element_type::DOUBLE:
            return Node::MakeFloat(elem.get_double().value());
        case simdjson::dom::element_type::STRING:
            return Node::MakeString(std::string(elem.get_string().value()).c_str());
        case simdjson::dom::element_type::ARRAY: {
            auto* node = Node::MakeArray();
            auto arr = elem.get_array().value();
            for (auto child : arr)
                node->arr.push_back(simdjson_dom_to_bench<Map>(child));
            return node;
        }
        case simdjson::dom::element_type::OBJECT: {
            auto* node = Node::MakeObject();
            auto obj = elem.get_object().value();
            for (auto field : obj)
                node->ObjInsert(std::string(field.key), simdjson_dom_to_bench<Map>(field.value));
            return node;
        }
    }
    return Node::MakeNull();
}

#endif
