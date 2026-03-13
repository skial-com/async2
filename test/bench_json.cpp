// JSON operation benchmarks: random access, insert, delete, append
// Compile: g++ -std=c++17 -O2 -I../src -o bench_json bench_json.cpp ../src/json_value.cpp

#include "json_value.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>

static constexpr int ITERATIONS = 100000;

using Clock = std::chrono::high_resolution_clock;

struct BenchResult {
    std::string name;
    double ns_per_op;
};

static std::vector<BenchResult> results;

static void report(const char* name, Clock::time_point start, Clock::time_point end, int ops) {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ns_per_op = (double)ns / ops;
    results.push_back({name, ns_per_op});
    printf("  %-45s %10.1f ns/op  (%d ops)\n", name, ns_per_op, ops);
}

// Build an object node with N keys: "key_0" .. "key_{N-1}", values are ints
static std::shared_ptr<JsonNode> make_object(int n) {
    auto obj = JsonNode::MakeObject();
    for (int i = 0; i < n; i++) {
        std::string k = "key_" + std::to_string(i);
        obj->keys.push_back(k);
        obj->obj[k] = JsonNode::MakeInt(i);
    }
    return obj;
}

// Build an array node with N int elements
static std::shared_ptr<JsonNode> make_array(int n) {
    auto arr = JsonNode::MakeArray();
    for (int i = 0; i < n; i++)
        arr->arr.push_back(JsonNode::MakeInt(i));
    return arr;
}

// ---- Object benchmarks ----

static void bench_obj_random_access(int n, std::mt19937& rng) {
    auto obj = make_object(n);

    // Pre-generate random keys
    std::vector<std::string> keys(ITERATIONS);
    std::uniform_int_distribution<int> dist(0, n - 1);
    for (int i = 0; i < ITERATIONS; i++)
        keys[i] = "key_" + std::to_string(dist(rng));

    volatile int64_t sink = 0;
    auto start = Clock::now();
    for (int i = 0; i < ITERATIONS; i++) {
        auto it = obj->obj.find(keys[i]);
        sink = it->second->int_val;
    }
    auto end = Clock::now();
    (void)sink;

    char label[64];
    snprintf(label, sizeof(label), "obj[%d] random access", n);
    report(label, start, end, ITERATIONS);
}

static void bench_obj_random_insert(int n, std::mt19937& rng) {
    int ops = ITERATIONS;

    // Start with a copy each time would be expensive, so we insert new keys
    auto obj = make_object(n);
    std::vector<std::string> new_keys(ops);
    for (int i = 0; i < ops; i++)
        new_keys[i] = "ins_" + std::to_string(i);

    auto start = Clock::now();
    for (int i = 0; i < ops; i++) {
        obj->keys.push_back(new_keys[i]);
        obj->obj[new_keys[i]] = JsonNode::MakeInt(i);
    }
    auto end = Clock::now();

    char label[64];
    snprintf(label, sizeof(label), "obj[%d] insert (from %d keys)", n, n);
    report(label, start, end, ops);
}

static void bench_obj_random_delete(int n, std::mt19937& rng) {
    auto obj = make_object(n);

    // Shuffle key indices for random deletion order
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);

    std::vector<std::string> del_keys(n);
    for (int i = 0; i < n; i++)
        del_keys[i] = "key_" + std::to_string(order[i]);

    auto start = Clock::now();
    for (int i = 0; i < n; i++) {
        obj->obj.erase(del_keys[i]);
        auto it = std::find(obj->keys.begin(), obj->keys.end(), del_keys[i]);
        if (it != obj->keys.end())
            obj->keys.erase(it);
    }
    auto end = Clock::now();

    char label[64];
    snprintf(label, sizeof(label), "obj[%d] random delete", n);
    report(label, start, end, n);
}

static void bench_obj_append(int n, std::mt19937&) {
    auto obj = JsonNode::MakeObject();

    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++)
        keys[i] = "key_" + std::to_string(i);

    auto start = Clock::now();
    for (int i = 0; i < n; i++) {
        obj->keys.push_back(keys[i]);
        obj->obj[keys[i]] = JsonNode::MakeInt(i);
    }
    auto end = Clock::now();

    char label[64];
    snprintf(label, sizeof(label), "obj append %d keys", n);
    report(label, start, end, n);
}

// ---- Array benchmarks ----

static void bench_arr_random_access(int n, std::mt19937& rng) {
    auto arr = make_array(n);

    std::vector<int> indices(ITERATIONS);
    std::uniform_int_distribution<int> dist(0, n - 1);
    for (int i = 0; i < ITERATIONS; i++)
        indices[i] = dist(rng);

    volatile int64_t sink = 0;
    auto start = Clock::now();
    for (int i = 0; i < ITERATIONS; i++) {
        sink = arr->arr[indices[i]]->int_val;
    }
    auto end = Clock::now();
    (void)sink;

    char label[64];
    snprintf(label, sizeof(label), "arr[%d] random access", n);
    report(label, start, end, ITERATIONS);
}

static void bench_arr_random_insert(int n, std::mt19937& rng) {
    // Insert at random positions into an array starting at size n
    int ops = std::min(ITERATIONS, n); // cap to keep runtime sane for large n
    auto arr = make_array(n);

    std::vector<int> positions(ops);
    for (int i = 0; i < ops; i++) {
        std::uniform_int_distribution<int> dist(0, (int)arr->arr.size());
        positions[i] = dist(rng);
    }

    auto start = Clock::now();
    for (int i = 0; i < ops; i++) {
        int pos = positions[i] % (int)(arr->arr.size() + 1);
        arr->arr.insert(arr->arr.begin() + pos, JsonNode::MakeInt(i));
    }
    auto end = Clock::now();

    char label[64];
    snprintf(label, sizeof(label), "arr[%d] random insert", n);
    report(label, start, end, ops);
}

static void bench_arr_random_delete(int n, std::mt19937& rng) {
    auto arr = make_array(n);

    auto start = Clock::now();
    while (!arr->arr.empty()) {
        std::uniform_int_distribution<int> dist(0, (int)arr->arr.size() - 1);
        int idx = dist(rng);
        arr->arr.erase(arr->arr.begin() + idx);
    }
    auto end = Clock::now();

    char label[64];
    snprintf(label, sizeof(label), "arr[%d] random delete", n);
    report(label, start, end, n);
}

static void bench_arr_append(int n, std::mt19937&) {
    auto arr = JsonNode::MakeArray();

    auto start = Clock::now();
    for (int i = 0; i < n; i++)
        arr->arr.push_back(JsonNode::MakeInt(i));
    auto end = Clock::now();

    char label[64];
    snprintf(label, sizeof(label), "arr append %d elements", n);
    report(label, start, end, n);
}

// ---- Parse benchmark ----

static void bench_parse(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        printf("  SKIP: could not open %s\n", path.c_str());
        return;
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    printf("  File: %s (%zu bytes)\n", path.c_str(), data.size());

    int ops = std::max(1, std::min(ITERATIONS, (int)(500000000 / std::max(data.size(), (size_t)1))));

    auto start = Clock::now();
    for (int i = 0; i < ops; i++) {
        auto node = JsonParse(data.data(), data.size());
        if (!node) {
            printf("  ERROR: parse failed\n");
            return;
        }
    }
    auto end = Clock::now();

    report("parse bench.json", start, end, ops);
}

static void bench_serialize(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto node = JsonParse(data.data(), data.size());
    if (!node) return;

    int ops = std::max(1, std::min(ITERATIONS, (int)(500000000 / std::max(data.size(), (size_t)1))));

    auto start = Clock::now();
    for (int i = 0; i < ops; i++) {
        std::string s = JsonSerialize(*node);
    }
    auto end = Clock::now();

    report("serialize bench.json", start, end, ops);
}

int main(int argc, char** argv) {
    std::string data_file = "../data/bench.json";
    if (argc > 1) data_file = argv[1];

    std::mt19937 rng(42); // fixed seed for reproducibility

    printf("JSON Benchmark\n");
    printf("==============\n\n");

    // Parse/serialize benchmark
    printf("[Parse & Serialize]\n");
    bench_parse(data_file);
    bench_serialize(data_file);

    // Object benchmarks
    for (int n : {10, 50, 1000}) {
        printf("\n[Object n=%d]\n", n);
        bench_obj_random_access(n, rng);
        bench_obj_random_insert(n, rng);
        bench_obj_random_delete(n, rng);
        bench_obj_append(n, rng);
    }

    // Array benchmarks
    for (int n : {10, 50, 1000}) {
        printf("\n[Array n=%d]\n", n);
        bench_arr_random_access(n, rng);
        bench_arr_random_insert(n, rng);
        bench_arr_random_delete(n, rng);
        bench_arr_append(n, rng);
    }

    // Summary table
    printf("\n\nSummary\n");
    printf("%-47s %12s\n", "Benchmark", "ns/op");
    printf("%-47s %12s\n", "-----------------------------------------------", "------------");
    for (auto& r : results)
        printf("%-47s %12.1f\n", r.name.c_str(), r.ns_per_op);

    return 0;
}
