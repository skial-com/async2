# DataHandle CacheConfig Design (Shelved)

This documents an alternative approach to LRU caching that was considered but
shelved in favor of the LinkedList native + `async2_lru.inc` composition approach.

## Concept

Embed cache behavior directly into DataHandle with a CacheConfig struct:

```cpp
struct CacheConfig {
    int max_entries;
    int ttl_seconds;
    EvictionPolicy policy;  // LRU, LFU, FIFO
    // ... stats counters
};
```

DataHandle would gain cache-aware methods: `CacheGet`, `CacheSet`, `CacheEvict`,
with the linked list and eviction tracking built into the C++ layer.

## Why it was shelved

- **Complexity**: 30+ if-guards throughout DataHandle to check if cache mode is active
- **Coupling**: Cache logic deeply intertwined with JSON/IntObject data model
- **Rigidity**: Hard to customize eviction strategies from SourcePawn
- **Child handle invalidation**: Evicting a cached entry that has outstanding shallow
  child handles requires careful refcount management to avoid dangling references

## Chosen approach

General-purpose LinkedList native exposed as a primitive, with `async2_lru.inc`
composing LinkedList + IntObject/Json into reusable LRU+TTL caches entirely in
SourcePawn stock functions. This keeps the C++ layer simple and lets users build
custom cache strategies.
