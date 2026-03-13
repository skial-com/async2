#ifndef ASYNC2_DATA_MAP_H
#define ASYNC2_DATA_MAP_H

// Central config for the hash map and hash function used by DataNode.
// Change these two includes and the aliases below to swap implementations.

#define XXH_INLINE_ALL
#include "xxhash.h"
#include "tsl/robin_map.h"

struct xxh3_hash {
    size_t operator()(const std::string& s) const noexcept {
        return (size_t)XXH3_64bits(s.data(), s.size());
    }
    size_t operator()(int64_t v) const noexcept {
        return (size_t)XXH3_64bits(&v, sizeof(v));
    }
};

template<typename K, typename V>
using DataMap = tsl::robin_map<K, V, xxh3_hash>;

#endif
