// FixedPool churn scenario.
//
// Many threads alternate MakeX/Decref in tight loops. This drives the pool
// hot path (thread-local cache push/pop, no lock) and the batch transfer
// path (thread-local <-> central pool via std::mutex) through every thread.
//
// TSan should be silent. A race here implies a bug in FixedPool's cache
// handoff or in the node constructor/destructor invoked by MakeX/Decref.

#include "scenarios.h"
#include "data_node.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr int kThreads      = 8;
constexpr int kBurst        = 512;  // allocate this many, then free them all

}  // namespace

ScenarioResult RunDataNodePool(int seconds) {
    std::atomic<bool> stop{false};
    std::atomic<long> total_allocs{0};

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            long allocs = 0;
            std::vector<DataNode*> holding;
            holding.reserve(kBurst);

            while (!stop.load(std::memory_order_relaxed)) {
                // Build up a batch of mixed node types so both fast-path
                // alloc and occasional batch refills fire on each thread.
                for (int i = 0; i < kBurst; ++i) {
                    DataNode* n;
                    switch ((t + i) & 3) {
                        case 0: n = DataNode::MakeInt(i); break;
                        case 1: n = DataNode::MakeFloat(i * 0.1); break;
                        case 2: n = DataNode::MakeString("pool"); break;
                        default: n = DataNode::MakeNull(); break;
                    }
                    holding.push_back(n);
                    ++allocs;
                }
                // Release them all — exercises batch return to the central
                // pool once the local cache exceeds kMaxLocal.
                for (DataNode* n : holding) DataNode::Decref(n);
                holding.clear();
            }
            total_allocs.fetch_add(allocs, std::memory_order_relaxed);
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : workers) t.join();

    size_t total = 0, free_blocks = 0, block_size = 0;
    DataPoolStats(total, free_blocks, block_size);
    long leaked = static_cast<long>(total) - static_cast<long>(free_blocks);

    long allocs = total_allocs.load();
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "allocs=%ld pool_total=%zu pool_free=%zu leaked=%ld block=%zu",
                  allocs, total, free_blocks, leaked, block_size);
    ScenarioResult r;
    r.summary = buf;
    r.exit_code = (allocs > 0 && leaked == 0) ? 0 : 1;
    return r;
}
