#ifndef ASYNC2_DATA_NODE_POOL_H
#define ASYNC2_DATA_NODE_POOL_H

#include <cstddef>
#include <new>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>

// Fixed-size block pool allocator with free-list recycling.
// Thread-safe via thread-local free list caches with batch transfer
// to a shared central pool. Fast path (alloc/free) is lock-free;
// the mutex is only taken for batch refills/returns (~1 per 128 ops).
template<size_t BlockSize, size_t BlockAlign = alignof(std::max_align_t)>
class FixedPool {
    static_assert(BlockSize >= sizeof(void*), "Block must fit a pointer");

    struct FreeBlock { FreeBlock* next; };

    // Thread-local cache: plain pointers, no atomics needed.
    // Uses raw pointer TLS to avoid __cxa_thread_atexit (dlclose compat).
    struct ThreadCache {
        FreeBlock* free_list = nullptr;
        size_t count = 0;
    };

    static constexpr size_t kBatchSize = 128;  // blocks to grab from central pool
    static constexpr size_t kMaxLocal = 256;   // return half when exceeded
    static constexpr size_t kReturnCount = 128; // blocks to return (kMaxLocal / 2)

    std::mutex lock_;
    FreeBlock* free_list_ = nullptr;
    std::vector<void*> chunks_;
    size_t next_chunk_count_ = 256;
    size_t total_allocated_ = 0;  // blocks ever given out (grows on Grow)
    size_t free_count_ = 0;       // blocks currently in central free list

    // Registry of all thread caches for accurate stats and cleanup.
    // Protected by lock_.
    std::vector<ThreadCache*> caches_;

    static constexpr size_t kAlignedSize =
        (BlockSize + BlockAlign - 1) & ~(BlockAlign - 1);

    // Get or create the thread-local cache for this pool.
    // Each pool instance needs its own TLS cache, but since there's only
    // one global pool, we use a single static thread_local.
    ThreadCache* GetCache() {
        // thread_local raw pointer — no destructor, dlclose safe.
        // On thread exit, blocks stay in the cache (leaked to OS on thread
        // destruction, but game/event threads live for the extension lifetime).
        static thread_local ThreadCache* tl_cache = nullptr;
        if (!tl_cache) {
            tl_cache = new ThreadCache();
            std::lock_guard<std::mutex> guard(lock_);
            caches_.push_back(tl_cache);
        }
        return tl_cache;
    }

public:
    FixedPool() = default;

    ~FixedPool() {
        // Free all thread caches
        for (auto* cache : caches_)
            delete cache;
        // Free all chunks
        for (void* chunk : chunks_)
            ::operator delete(chunk);
    }

    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;

    void* Alloc() {
        auto* cache = GetCache();

        // Fast path: pop from thread-local free list (no lock)
        if (cache->free_list) {
            void* ptr = cache->free_list;
            cache->free_list = cache->free_list->next;
            cache->count--;
            return ptr;
        }

        // Slow path: refill from central pool
        std::lock_guard<std::mutex> guard(lock_);
        if (!free_list_) Grow();

        // Transfer up to kBatchSize blocks to local cache
        size_t transferred = 0;
        while (free_list_ && transferred < kBatchSize) {
            FreeBlock* block = free_list_;
            free_list_ = free_list_->next;
            free_count_--;
            block->next = cache->free_list;
            cache->free_list = block;
            cache->count++;
            transferred++;
        }

        // Pop one for the caller
        void* ptr = cache->free_list;
        cache->free_list = cache->free_list->next;
        cache->count--;
        return ptr;
    }

    void Free(void* ptr) {
        auto* cache = GetCache();
        auto* block = static_cast<FreeBlock*>(ptr);

        // Fast path: push to thread-local free list (no lock)
        block->next = cache->free_list;
        cache->free_list = block;
        cache->count++;

        // If local cache is too large, return a batch to central pool
        if (cache->count > kMaxLocal) {
            std::lock_guard<std::mutex> guard(lock_);
            for (size_t i = 0; i < kReturnCount; i++) {
                FreeBlock* b = cache->free_list;
                cache->free_list = cache->free_list->next;
                cache->count--;
                b->next = free_list_;
                free_list_ = b;
                free_count_++;
            }
        }
    }

    // Returns total blocks allocated from OS, blocks currently free, and block size.
    void Stats(size_t& total, size_t& free_blocks, size_t& block_size) {
        std::lock_guard<std::mutex> guard(lock_);
        free_blocks = free_count_;
        for (auto* cache : caches_)
            free_blocks += cache->count;
        total = total_allocated_;
        block_size = kAlignedSize;
    }

private:
    void Grow() {
        size_t count = next_chunk_count_;
        char* chunk = static_cast<char*>(::operator new(kAlignedSize * count));
        chunks_.push_back(chunk);
        for (size_t i = 0; i < count; i++) {
            auto* block = reinterpret_cast<FreeBlock*>(chunk + i * kAlignedSize);
            block->next = free_list_;
            free_list_ = block;
        }
        total_allocated_ += count;
        free_count_ += count;
        next_chunk_count_ = std::min(next_chunk_count_ * 2, (size_t)65536);
    }
};

#endif
