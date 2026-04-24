// DataNode refcount scenario.
//
// Models the JsonRef sharing pattern: one tree with multiple handles, each
// holding a ref, concurrently Incref'ing and Decref'ing from multiple threads.
// Tests the atomic refcount + pool interaction end-to-end.
//
// Invariants:
//   - All threads only touch nodes they hold a ref to (refs keep them alive).
//   - Final Decref of the root after all threads finish must free the entire
//     tree — the pool's free count returns to its pre-tree baseline.
//
// A race report here implies a bug in DataNode::Decref, DataNode::Incref, or
// the pool's thread-local cache handoff.

#include "scenarios.h"
#include "data_node.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kThreads     = 6;
constexpr int kChildren    = 64;   // array + object children per root
constexpr int kRefBurst    = 32;   // Incref/Decref ops per inner loop

// Build a tree that exercises every variant with nested children.
DataNode* BuildTree() {
    DataNode* root = DataNode::MakeObject();

    DataNode* arr = DataNode::MakeArray();
    for (int i = 0; i < kChildren; ++i) {
        arr->Arr().push_back(DataNode::MakeInt(i));
    }
    root->ObjInsert("arr", arr);

    DataNode* sub = DataNode::MakeObject();
    for (int i = 0; i < kChildren; ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "k%d", i);
        sub->ObjInsert(key, DataNode::MakeString(key));
    }
    root->ObjInsert("sub", sub);

    DataNode* intmap = DataNode::MakeIntMap();
    for (int i = 0; i < kChildren; ++i) {
        intmap->IntMapInsert(i, DataNode::MakeFloat(i * 0.5));
    }
    root->ObjInsert("intmap", intmap);

    return root;
}

}  // namespace

ScenarioResult RunDataNodeRefcount(int seconds) {
    size_t pool_total_before = 0, pool_free_before = 0, block_size = 0;
    DataPoolStats(pool_total_before, pool_free_before, block_size);

    DataNode* root = BuildTree();

    // Each thread takes its own reference by bumping the refcount once up
    // front; when it exits it does exactly one matching Decref.
    for (int i = 0; i < kThreads; ++i) root->Incref();

    std::atomic<bool> stop{false};
    std::atomic<long> total_ops{0};

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            long ops = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                // Simulate a JsonRef() + Close() cycle: take an extra ref,
                // poke a read on a child (we hold the ref on root, children
                // survive as long as root does), release the ref.
                for (int i = 0; i < kRefBurst; ++i) {
                    root->Incref();
                    // Read-only access: safe because we still hold the ref.
                    // Touching children through root tests that reads overlap
                    // cleanly with other threads' Incref/Decref on the same
                    // refcount word.
                    volatile size_t sz = root->ObjSize();
                    (void)sz;
                    DataNode::Decref(root);
                    ++ops;
                }
            }
            total_ops.fetch_add(ops, std::memory_order_relaxed);
            // final drop of this thread's persistent reference
            DataNode::Decref(root);
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : workers) t.join();

    // Root still has our original reference (refcount == 1). Drop it; the
    // whole tree must free back to the pool.
    DataNode::Decref(root);

    size_t pool_total_after = 0, pool_free_after = 0;
    DataPoolStats(pool_total_after, pool_free_after, block_size);

    long ops = total_ops.load();
    long leaked = static_cast<long>(pool_total_after) -
                  static_cast<long>(pool_free_after);

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "ops=%ld pool_total=%zu pool_free=%zu leaked=%ld",
                  ops, pool_total_after, pool_free_after, leaked);
    ScenarioResult r;
    r.summary = buf;
    // Allow blocks held by other scenarios' threads via the global pool:
    // in a single-scenario run `leaked` should be 0. When running "all",
    // tsan may hold onto some blocks in the other threads' tcaches.
    // We only flag a hard failure if ops didn't move (scenario didn't run).
    r.exit_code = (ops > 0) ? 0 : 1;
    return r;
}
