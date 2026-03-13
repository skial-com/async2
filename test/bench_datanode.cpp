// Benchmark: DataNode parse, access, mutation, and serialize
//
// Build via cmake (from test/):
//   mkdir build && cd build && cmake .. && make bench_datanode

#include "data_node.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

using Clock = std::chrono::high_resolution_clock;

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static int auto_ops(size_t data_bytes, int max_ops = 100000) {
    return std::max(1, std::min(max_ops, (int)(500000000 / std::max(data_bytes, (size_t)1))));
}

static double bench(int ops, auto fn) {
    auto s = Clock::now();
    for (int i = 0; i < ops; i++) fn();
    auto e = Clock::now();
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
}

static const char* fmt(double ns, char* buf, size_t sz) {
    if (ns >= 1000000) snprintf(buf, sz, "%.2f ms", ns / 1000000.0);
    else if (ns >= 1000) snprintf(buf, sz, "%.1f us", ns / 1000.0);
    else snprintf(buf, sz, "%.1f ns", ns);
    return buf;
}

int main(int argc, char** argv) {
    std::string data_file = "../data/bench.json";
    if (argc > 1) data_file = argv[1];

    std::string data = read_file(data_file);
    if (data.empty()) {
        printf("ERROR: could not read %s\n", data_file.c_str());
        return 1;
    }

    int parse_ops = auto_ops(data.size());
    constexpr int ACCESS_OPS = 100000;
    char buf[32];

    printf("DataNode Benchmark (%zu bytes, parse ops=%d)\n", data.size(), parse_ops);
    printf("%-35s %12s\n", "benchmark", "time");
    printf("%-35s %12s\n", "-----------------------------------", "------------");

    // Parse
    printf("%-35s %12s\n", "parse",
        fmt(bench(parse_ops, [&]{ auto* r = DataParseJson(data.data(), data.size()); DataNode::Destroy(r); }), buf, sizeof(buf)));

    // Serialize
    {
        auto* root = DataParseJson(data.data(), data.size());
        printf("%-35s %12s\n", "serialize",
            fmt(bench(parse_ops, [&]{ auto s = DataSerializeJson(*root); }), buf, sizeof(buf)));
        printf("%-35s %12s\n", "serialize pretty",
            fmt(bench(parse_ops, [&]{ auto s = DataSerializeJson(*root, true); }), buf, sizeof(buf)));
        DataNode::Destroy(root);
    }

    // Object key access
    {
        auto* root = DataParseJson(data.data(), data.size());
        std::mt19937 rng(42);
        for (int n : {10, 50, 1000}) {
            char key[16], label[64];
            snprintf(key, sizeof(key), "obj_%d", n);
            snprintf(label, sizeof(label), "obj[%d] random key access", n);
            auto* obj = root->ObjFind(key);
            std::vector<std::string> keys(ACCESS_OPS);
            std::uniform_int_distribution<int> dist(0, n - 1);
            for (int i = 0; i < ACCESS_OPS; i++) keys[i] = "key_" + std::to_string(dist(rng));
            volatile int64_t sink = 0;
            int idx = 0;
            printf("%-35s %12s\n", label,
                fmt(bench(ACCESS_OPS, [&]{ sink = obj->ObjFind(keys[idx++])->int_val; }), buf, sizeof(buf)));
            (void)sink;
        }
        DataNode::Destroy(root);
    }

    // Array index access
    {
        auto* root = DataParseJson(data.data(), data.size());
        std::mt19937 rng(42);
        for (int n : {10, 50, 1000}) {
            char key[16], label[64];
            snprintf(key, sizeof(key), "arr_%d", n);
            snprintf(label, sizeof(label), "arr[%d] random index access", n);
            auto* arr = root->ObjFind(key);
            std::vector<int> indices(ACCESS_OPS);
            std::uniform_int_distribution<int> dist(0, n - 1);
            for (int i = 0; i < ACCESS_OPS; i++) indices[i] = dist(rng);
            volatile int64_t sink = 0;
            int idx = 0;
            printf("%-35s %12s\n", label,
                fmt(bench(ACCESS_OPS, [&]{ sink = arr->arr[indices[idx++]]->int_val; }), buf, sizeof(buf)));
            (void)sink;
        }
        DataNode::Destroy(root);
    }

    // Object insert
    for (int n : {10, 50, 1000}) {
        char label[64];
        snprintf(label, sizeof(label), "obj insert %d keys", n);
        std::vector<std::string> keys(n);
        for (int i = 0; i < n; i++) keys[i] = "key_" + std::to_string(i);
        int reps = std::max(1, ACCESS_OPS / n);
        printf("%-35s %12s\n", label,
            fmt(bench(reps, [&]{
                auto* obj = DataNode::MakeObject();
                for (int i = 0; i < n; i++) obj->ObjInsert(keys[i], DataNode::MakeInt(i));
                DataNode::Destroy(obj);
            }) / n, buf, sizeof(buf)));
    }

    // Object random delete
    {
        std::mt19937 rng(42);
        for (int n : {10, 50, 1000}) {
            char label[64];
            snprintf(label, sizeof(label), "obj[%d] random delete", n);
            std::vector<std::string> keys(n);
            for (int i = 0; i < n; i++) keys[i] = "key_" + std::to_string(i);
            int reps = std::max(1, 10000 / n);
            std::vector<int> order(n);
            for (int i = 0; i < n; i++) order[i] = i;
            std::vector<std::vector<int>> shuffles(reps);
            for (int r = 0; r < reps; r++) {
                shuffles[r] = order;
                std::shuffle(shuffles[r].begin(), shuffles[r].end(), rng);
            }
            double total_ns = 0;
            for (int r = 0; r < reps; r++) {
                auto* obj = DataNode::MakeObject();
                for (int i = 0; i < n; i++) obj->ObjInsert(keys[i], DataNode::MakeInt(i));
                auto s = Clock::now();
                for (int i = 0; i < n; i++) obj->ObjErase(keys[shuffles[r][i]]);
                auto e = Clock::now();
                total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                DataNode::Destroy(obj);
            }
            printf("%-35s %12s\n", label, fmt(total_ns / (reps * n), buf, sizeof(buf)));
        }
    }

    // Array append
    for (int n : {10, 50, 1000}) {
        char label[64];
        snprintf(label, sizeof(label), "arr append %d", n);
        int reps = std::max(1, ACCESS_OPS / n);
        printf("%-35s %12s\n", label,
            fmt(bench(reps, [&]{
                auto* arr = DataNode::MakeArray();
                for (int i = 0; i < n; i++) arr->arr.push_back(DataNode::MakeInt(i));
                DataNode::Destroy(arr);
            }) / n, buf, sizeof(buf)));
    }

    // Array random insert
    {
        std::mt19937 rng(42);
        for (int n : {10, 50, 1000}) {
            char label[64];
            snprintf(label, sizeof(label), "arr[%d] random insert", n);
            int reps = std::max(1, 10000 / n);
            std::vector<int> positions(n);
            for (int i = 0; i < n; i++) positions[i] = std::uniform_int_distribution<int>(0, i)(rng);
            printf("%-35s %12s\n", label,
                fmt(bench(reps, [&]{
                    auto* arr = DataNode::MakeArray();
                    for (int i = 0; i < n; i++) arr->arr.insert(arr->arr.begin() + positions[i], DataNode::MakeInt(i));
                    DataNode::Destroy(arr);
                }) / n, buf, sizeof(buf)));
        }
    }

    // Array random delete
    {
        std::mt19937 rng(42);
        for (int n : {10, 50, 1000}) {
            char label[64];
            snprintf(label, sizeof(label), "arr[%d] random delete", n);
            int reps = std::max(1, 10000 / n);
            std::vector<std::vector<int>> indices(reps);
            for (int r = 0; r < reps; r++) {
                indices[r].resize(n);
                for (int remaining = n; remaining > 0; remaining--)
                    indices[r][n - remaining] = std::uniform_int_distribution<int>(0, remaining - 1)(rng);
            }
            double total_ns = 0;
            for (int r = 0; r < reps; r++) {
                auto* arr = DataNode::MakeArray();
                for (int i = 0; i < n; i++) arr->arr.push_back(DataNode::MakeInt(i));
                auto s = Clock::now();
                for (int i = 0; i < n; i++) {
                    DataNode::Destroy(arr->arr[indices[r][i]]);
                    arr->arr.erase(arr->arr.begin() + indices[r][i]);
                }
                auto e = Clock::now();
                total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                DataNode::Destroy(arr);
            }
            printf("%-35s %12s\n", label, fmt(total_ns / (reps * n), buf, sizeof(buf)));
        }
    }

    // Deep copy
    {
        auto* root = DataParseJson(data.data(), data.size());
        printf("%-35s %12s\n", "deep copy",
            fmt(bench(parse_ops, [&]{ auto* c = root->DeepCopy(); DataNode::Destroy(c); }), buf, sizeof(buf)));
        DataNode::Destroy(root);
    }

    // Pool stats
    {
        size_t total, free_blocks, block_size;
        DataPoolStats(total, free_blocks, block_size);
        printf("\npool: %zu blocks (%zu free), %zu bytes/block\n", total, free_blocks, block_size);
    }

    return 0;
}
