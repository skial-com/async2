// Benchmark: compare map implementations with XXH3 hash held constant.
//
// Configurations (all using xxh3_hash):
//   1. emhash8::HashMap
//   2. tsl::robin_map
//   3. ska::flat_hash_map
//   4. ankerl::unordered_dense::map
//
// Compile (from test/):
//   g++ -std=c++17 -O2 \
//     -I../src/json -I../third_party/simdjson/singleheader \
//     -I../third_party/robin-hood-hashing/src/include \
//     -I../third_party/flat_hash_map \
//     -I../third_party/emhash \
//     -I../third_party/robin-map/include \
//     -I../third_party/unordered_dense/include \
//     -I../third_party/xxhash \
//     -o bench_maps bench_maps.cpp \
//     ../third_party/simdjson/singleheader/simdjson.cpp

#include "bench_node.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

// ============================================================
// Helpers
// ============================================================

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static constexpr int NUM_MAPS = 4;
static const char* map_names[NUM_MAPS] = {
    "emhash8+xxh3", "tsl+xxh3", "ska+xxh3", "ankerl+xxh3"
};

struct MapRow {
    std::string benchmark;
    double times[NUM_MAPS]; // ns per op, -1 = N/A
};

static std::vector<MapRow> map_table;

static auto fmt_ns(double ns) -> std::string {
    if (ns < 0) return "N/A";
    if (ns >= 1000000) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.2f ms", ns / 1000000.0); return buf;
    }
    if (ns >= 1000) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.1f us", ns / 1000.0); return buf;
    }
    char buf[32]; snprintf(buf, sizeof(buf), "%.1f ns", ns); return buf;
}

// ============================================================
// Benchmark implementations (templated on map config)
// ============================================================

// -- Object: insert N new keys --
template<template<typename, typename> class Map>
static double bench_obj_insert_impl(int n, int ops) {
    using Node = BenchNode<Map>;
    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++)
        keys[i] = "key_" + std::to_string(i);

    auto s = Clock::now();
    for (int r = 0; r < ops / n; r++) {
        auto* obj = Node::MakeObject();
        for (int i = 0; i < n; i++)
            obj->ObjInsert(keys[i], Node::MakeInt(i));
        Node::Destroy(obj);
    }
    auto e = Clock::now();
    int total = (ops / n) * n;
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
}

// -- Object: random key lookup on N-key object --
template<template<typename, typename> class Map>
static double bench_obj_access_impl(int n, int ops) {
    using Node = BenchNode<Map>;

    // Build the object
    auto* obj = Node::MakeObject();
    for (int i = 0; i < n; i++)
        obj->ObjInsert("key_" + std::to_string(i), Node::MakeInt(i));

    // Pre-generate random keys
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, n - 1);
    std::vector<std::string> keys(ops);
    for (int i = 0; i < ops; i++)
        keys[i] = "key_" + std::to_string(dist(rng));

    volatile int64_t sink = 0;
    auto s = Clock::now();
    for (int i = 0; i < ops; i++) {
        auto* v = obj->ObjFind(keys[i]);
        sink = v->int_val;
    }
    auto e = Clock::now();
    (void)sink;
    Node::Destroy(obj);
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
}

// -- Object: random delete from N-key object --
template<template<typename, typename> class Map>
static double bench_obj_delete_impl(int n, int reps) {
    using Node = BenchNode<Map>;

    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++)
        keys[i] = "key_" + std::to_string(i);

    std::mt19937 rng(42);
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;

    std::vector<std::vector<int>> shuffles(reps);
    for (int r = 0; r < reps; r++) {
        shuffles[r] = order;
        std::shuffle(shuffles[r].begin(), shuffles[r].end(), rng);
    }

    double total_ns = 0;
    for (int r = 0; r < reps; r++) {
        auto* obj = Node::MakeObject();
        for (int i = 0; i < n; i++)
            obj->ObjInsert(keys[i], Node::MakeInt(i));
        auto s = Clock::now();
        for (int i = 0; i < n; i++)
            obj->ObjErase(keys[shuffles[r][i]]);
        auto e = Clock::now();
        total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
        Node::Destroy(obj);
    }
    return total_ns / (reps * n);
}

// -- Parse (simdjson) + convert to BenchNode --
template<template<typename, typename> class Map>
static double bench_parse_convert_impl(const std::string& data, int ops) {
    simdjson::dom::parser parser;

    auto s = Clock::now();
    for (int i = 0; i < ops; i++) {
        simdjson::padded_string padded(data.data(), data.size());
        auto result = parser.parse(padded);
        auto* node = simdjson_dom_to_bench<Map>(result.value());
        BenchNode<Map>::Destroy(node);
    }
    auto e = Clock::now();
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
}

// -- Object: mixed read/write workload --
template<template<typename, typename> class Map>
static double bench_obj_mixed_impl(int n, int ops) {
    using Node = BenchNode<Map>;

    // Build initial object
    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++)
        keys[i] = "key_" + std::to_string(i);

    // Pre-generate operations: 70% lookup, 15% insert, 15% delete
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int> key_dist(0, n - 1);
    int next_key = n;

    struct Op { int kind; std::string key; }; // kind: 0=find, 1=insert, 2=delete
    std::vector<Op> operations(ops);
    for (int i = 0; i < ops; i++) {
        int r = op_dist(rng);
        if (r < 70) {
            operations[i] = {0, keys[key_dist(rng)]};
        } else if (r < 85) {
            std::string k = "key_" + std::to_string(next_key++);
            operations[i] = {1, k};
        } else {
            operations[i] = {2, keys[key_dist(rng)]};
        }
    }

    auto* obj = Node::MakeObject();
    for (int i = 0; i < n; i++)
        obj->ObjInsert(keys[i], Node::MakeInt(i));

    volatile int64_t sink = 0;
    auto s = Clock::now();
    for (int i = 0; i < ops; i++) {
        switch (operations[i].kind) {
            case 0: {
                auto* v = obj->ObjFind(operations[i].key);
                if (v) sink = v->int_val;
                break;
            }
            case 1:
                obj->ObjInsert(operations[i].key, Node::MakeInt(i));
                break;
            case 2:
                obj->ObjErase(operations[i].key);
                break;
        }
    }
    auto e = Clock::now();
    (void)sink;
    Node::Destroy(obj);
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
}

// ============================================================
// Wrappers — run all map configs for each benchmark
// ============================================================

static void bench_obj_insert(int n) {
    constexpr int OPS = 100000;
    char label[64];
    snprintf(label, sizeof(label), "obj insert %d keys", n);

    MapRow row;
    row.benchmark = label;
    row.times[0] = bench_obj_insert_impl<Emhash8Xxh3Map>(n, OPS);
    row.times[1] = bench_obj_insert_impl<TslXxh3Map>(n, OPS);
    row.times[2] = bench_obj_insert_impl<SkaXxh3Map>(n, OPS);
    row.times[3] = bench_obj_insert_impl<AnkerlXxh3Map>(n, OPS);
    map_table.push_back(row);
}

static void bench_obj_access(int n) {
    constexpr int OPS = 100000;
    char label[64];
    snprintf(label, sizeof(label), "obj[%d] random lookup", n);

    MapRow row;
    row.benchmark = label;
    row.times[0] = bench_obj_access_impl<Emhash8Xxh3Map>(n, OPS);
    row.times[1] = bench_obj_access_impl<TslXxh3Map>(n, OPS);
    row.times[2] = bench_obj_access_impl<SkaXxh3Map>(n, OPS);
    row.times[3] = bench_obj_access_impl<AnkerlXxh3Map>(n, OPS);
    map_table.push_back(row);
}

static void bench_obj_delete(int n) {
    int reps = std::max(1, 10000 / n);
    char label[64];
    snprintf(label, sizeof(label), "obj[%d] random delete", n);

    MapRow row;
    row.benchmark = label;
    row.times[0] = bench_obj_delete_impl<Emhash8Xxh3Map>(n, reps);
    row.times[1] = bench_obj_delete_impl<TslXxh3Map>(n, reps);
    row.times[2] = bench_obj_delete_impl<SkaXxh3Map>(n, reps);
    row.times[3] = bench_obj_delete_impl<AnkerlXxh3Map>(n, reps);
    map_table.push_back(row);
}

static void bench_parse_convert(const std::string& data) {
    int ops = std::max(1, std::min(100000, (int)(500000000 / std::max(data.size(), (size_t)1))));

    MapRow row;
    row.benchmark = "parse + convert";
    row.times[0] = bench_parse_convert_impl<Emhash8Xxh3Map>(data, ops);
    row.times[1] = bench_parse_convert_impl<TslXxh3Map>(data, ops);
    row.times[2] = bench_parse_convert_impl<SkaXxh3Map>(data, ops);
    row.times[3] = bench_parse_convert_impl<AnkerlXxh3Map>(data, ops);
    map_table.push_back(row);
}

static void bench_obj_mixed(int n) {
    constexpr int OPS = 100000;
    char label[64];
    snprintf(label, sizeof(label), "obj[%d] mixed r/w", n);

    MapRow row;
    row.benchmark = label;
    row.times[0] = bench_obj_mixed_impl<Emhash8Xxh3Map>(n, OPS);
    row.times[1] = bench_obj_mixed_impl<TslXxh3Map>(n, OPS);
    row.times[2] = bench_obj_mixed_impl<SkaXxh3Map>(n, OPS);
    row.times[3] = bench_obj_mixed_impl<AnkerlXxh3Map>(n, OPS);
    map_table.push_back(row);
}

// ============================================================
// Main
// ============================================================

static void print_table() {
    // Compute column widths
    int name_w = 25;
    int col_w = 14;

    printf("\n");
    printf("%-*s", name_w, "Benchmark");
    for (int i = 0; i < NUM_MAPS; i++)
        printf(" %*s", col_w, map_names[i]);
    printf("\n");

    printf("%-*s", name_w, "-------------------------");
    for (int i = 0; i < NUM_MAPS; i++)
        printf(" %*s", col_w, "--------------");
    printf("\n");

    for (auto& r : map_table) {
        printf("%-*s", name_w, r.benchmark.c_str());
        for (int i = 0; i < NUM_MAPS; i++)
            printf(" %*s", col_w, fmt_ns(r.times[i]).c_str());
        printf("\n");
    }

    // Standard deviation per column
    printf("%-*s", name_w, "-------------------------");
    for (int i = 0; i < NUM_MAPS; i++)
        printf(" %*s", col_w, "--------------");
    printf("\n");

    // Exclude parse+convert from summary stats
    std::vector<MapRow*> stat_rows;
    for (auto& r : map_table)
        if (r.benchmark != "parse + convert")
            stat_rows.push_back(&r);

    size_t n = stat_rows.size();
    printf("%-*s", name_w, "stddev");
    for (int i = 0; i < NUM_MAPS; i++) {
        double sum = 0, sum2 = 0;
        for (auto* r : stat_rows) {
            sum += r->times[i];
            sum2 += r->times[i] * r->times[i];
        }
        double mean = sum / n;
        double sd = sqrt(sum2 / n - mean * mean);
        printf(" %*s", col_w, fmt_ns(sd).c_str());
    }
    printf("\n");

    printf("%-*s", name_w, "mean");
    for (int i = 0; i < NUM_MAPS; i++) {
        double sum = 0;
        for (auto* r : stat_rows)
            sum += r->times[i];
        printf(" %*s", col_w, fmt_ns(sum / n).c_str());
    }
    printf("\n");
}

int main(int argc, char** argv) {
    std::string data_file = "../data/bench.json";
    if (argc > 1) data_file = argv[1];

    std::string data = read_file(data_file);
    if (data.empty()) {
        printf("ERROR: could not read %s\n", data_file.c_str());
        return 1;
    }

    printf("Map+Hash Configuration Benchmark\n");
    printf("=================================\n");
    printf("Configs:\n");
    for (int i = 0; i < NUM_MAPS; i++)
        printf("  %d. %s\n", i + 1, map_names[i]);
    printf("\nFile: %s (%zu bytes)\n\n", data_file.c_str(), data.size());

    printf("[1] Object insert\n");
    for (int n : {10, 50, 1000})
        bench_obj_insert(n);

    printf("[2] Object random key lookup\n");
    for (int n : {10, 50, 1000})
        bench_obj_access(n);

    printf("[3] Object random delete\n");
    for (int n : {10, 50, 1000})
        bench_obj_delete(n);

    printf("[4] Object mixed read/write (70/15/15)\n");
    for (int n : {10, 50, 1000})
        bench_obj_mixed(n);

    printf("[5] Parse (simdjson) + convert to DOM\n");
    bench_parse_convert(data);

    print_table();

    return 0;
}
