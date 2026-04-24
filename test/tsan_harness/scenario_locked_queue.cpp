// LockedQueue scenario.
//
// Exercises the exact protocol used by event_loop.cpp:
//
//   Producer:  q.Lock(); q.Push(item); q.Unlock();
//   Consumer:  if (q.HasItems()) { q.Lock(); while (!q.Empty()) q.Pop(); q.Unlock(); }
//
// The interesting race surface is the unlocked `has_items_.load()` poll on
// the consumer side against the locked Push/Pop on both sides. TSan should
// see zero races on the queue itself; any report here points at a bug in
// LockedQueue.

#include "scenarios.h"
#include "locked_queue.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

struct Item {
    int producer;
    long seq;
};

constexpr int kProducers = 4;

}  // namespace

ScenarioResult RunLockedQueue(int seconds) {
    LockedQueue<Item> q;
    std::atomic<long> total_pushed{0};
    std::atomic<long> total_popped{0};

    // Two-stage shutdown: `stop_producers` halts the producers; the consumer
    // keeps draining until `stop_consumer` is set *after* producers are joined.
    // This makes the pushed/popped counts comparable without races.
    std::atomic<bool> stop_producers{false};
    std::atomic<bool> stop_consumer{false};

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p]() {
            long seq = 0;
            while (!stop_producers.load(std::memory_order_relaxed)) {
                q.Lock();
                q.Push({p, seq});
                q.Unlock();
                ++seq;
                // occasional yield so HasItems() polling has a chance to miss
                if ((seq & 0xff) == 0) std::this_thread::yield();
            }
            total_pushed.fetch_add(seq, std::memory_order_relaxed);
        });
    }

    std::thread consumer([&]() {
        long popped = 0;
        auto drain = [&]() {
            q.Lock();
            while (!q.Empty()) {
                (void)q.Pop();
                ++popped;
            }
            q.Unlock();
        };
        while (!stop_consumer.load(std::memory_order_relaxed)) {
            if (q.HasItems()) drain();
            else std::this_thread::yield();
        }
        // final drain after producers have definitely stopped
        drain();
        total_popped.store(popped, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    stop_producers.store(true, std::memory_order_relaxed);
    for (auto& t : producers) t.join();
    stop_consumer.store(true, std::memory_order_relaxed);
    consumer.join();

    long pushed = total_pushed.load();
    long popped = total_popped.load();

    char buf[128];
    std::snprintf(buf, sizeof(buf), "pushed=%ld popped=%ld", pushed, popped);
    ScenarioResult r;
    r.summary = buf;
    r.exit_code = (pushed == popped) ? 0 : 1;
    return r;
}
