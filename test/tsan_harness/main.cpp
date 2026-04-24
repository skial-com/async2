// async2 TSan harness.
//
// Exercises the cross-thread primitives of async2 (LockedQueue, DataNode
// refcount, FixedPool) under ThreadSanitizer, without the rest of srcds.
// This complements the in-process tsan_launch.sh path, which is blocked by
// steamclient.so's longjmp usage triggering a TSan CHECK.
//
// Usage:
//   async2_tsan_harness                       # run all scenarios, 5s each
//   async2_tsan_harness <name>                # run one scenario
//   async2_tsan_harness <name> <seconds>      # custom duration
//
// TSan race reports go to stderr inline; the harness itself prints a
// one-line summary per scenario and exits non-zero if any invariant fails.

#include "scenarios.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct Entry {
    const char* name;
    ScenarioResult (*fn)(int seconds);
};

static const Entry kScenarios[] = {
    {"locked_queue",      &RunLockedQueue},
    {"datanode_refcount", &RunDataNodeRefcount},
    {"datanode_pool",     &RunDataNodePool},
};

static void PrintUsage(const char* argv0) {
    std::fprintf(stderr, "usage: %s [scenario] [seconds]\n", argv0);
    std::fprintf(stderr, "scenarios:\n");
    for (const auto& e : kScenarios)
        std::fprintf(stderr, "  %s\n", e.name);
    std::fprintf(stderr, "  all (default)\n");
}

int main(int argc, char** argv) {
    const char* name = (argc >= 2) ? argv[1] : "all";
    int seconds      = (argc >= 3) ? std::atoi(argv[2]) : 5;
    if (seconds < 1) seconds = 1;

    if (std::strcmp(name, "-h") == 0 || std::strcmp(name, "--help") == 0) {
        PrintUsage(argv[0]);
        return 0;
    }

    std::vector<const Entry*> to_run;
    if (std::strcmp(name, "all") == 0) {
        for (const auto& e : kScenarios) to_run.push_back(&e);
    } else {
        for (const auto& e : kScenarios) {
            if (std::strcmp(e.name, name) == 0) { to_run.push_back(&e); break; }
        }
        if (to_run.empty()) {
            std::fprintf(stderr, "unknown scenario: %s\n", name);
            PrintUsage(argv[0]);
            return 2;
        }
    }

    int overall = 0;
    for (const auto* e : to_run) {
        std::fprintf(stderr, "=== %s (%ds) ===\n", e->name, seconds);
        std::fflush(stderr);
        ScenarioResult r = e->fn(seconds);
        std::fprintf(stderr, "%s: %s%s\n",
                     e->name,
                     r.exit_code == 0 ? "OK" : "FAIL",
                     r.summary.empty() ? "" : (" — " + r.summary).c_str());
        std::fflush(stderr);
        if (r.exit_code != 0) overall = r.exit_code;
    }

    return overall;
}
