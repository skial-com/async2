// Benchmark: shared_ptr vs unique_ptr overhead in JsonNode
// Compile: g++ -std=c++17 -O2 -I../src -o bench_ptr bench_ptr.cpp ../src/json_value.cpp

#include "json_value.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

using Clock = std::chrono::high_resolution_clock;

// ============================================================
// unique_ptr JsonNode clone (UNode)
// ============================================================

struct UNode {
    JsonType type = JsonType::Null;
    bool bool_val = false;
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string str_val;

    std::vector<std::unique_ptr<UNode>> arr;
    std::unordered_map<std::string, std::unique_ptr<UNode>> obj;
    std::vector<std::string> keys;

    static std::unique_ptr<UNode> MakeNull() {
        return std::make_unique<UNode>();
    }
    static std::unique_ptr<UNode> MakeInt(int64_t v) {
        auto n = std::make_unique<UNode>();
        n->type = JsonType::Int;
        n->int_val = v;
        return n;
    }
    static std::unique_ptr<UNode> MakeString(const char* v) {
        auto n = std::make_unique<UNode>();
        n->type = JsonType::String;
        n->str_val = v;
        return n;
    }
    static std::unique_ptr<UNode> MakeObject() {
        auto n = std::make_unique<UNode>();
        n->type = JsonType::Object;
        return n;
    }
    static std::unique_ptr<UNode> MakeArray() {
        auto n = std::make_unique<UNode>();
        n->type = JsonType::Array;
        return n;
    }
};

// ============================================================
// Helpers
// ============================================================

struct Row {
    std::string benchmark;
    double shared_ns;
    double unique_ns;
    double speedup; // shared/unique
};

static std::vector<Row> table;

static void add_row(const char* name, double shared_ns, double unique_ns) {
    table.push_back({name, shared_ns, unique_ns, shared_ns / unique_ns});
}

// ============================================================
// 1. Raw allocation cost
// ============================================================

static void bench_alloc() {
    constexpr int OPS = 1000000;

    // shared_ptr
    double shared_ns;
    {
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            auto p = std::make_shared<JsonNode>();
            p->type = JsonType::Int;
            p->int_val = i;
        }
        auto e = Clock::now();
        shared_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
    }

    // unique_ptr
    double unique_ns;
    {
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            auto p = std::make_unique<UNode>();
            p->type = JsonType::Int;
            p->int_val = i;
        }
        auto e = Clock::now();
        unique_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
    }

    add_row("alloc + dealloc (1 node)", shared_ns, unique_ns);
}

// ============================================================
// 2. Object insert N keys
// ============================================================

static void bench_obj_insert(int n) {
    int reps = std::max(1, 100000 / n);
    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++)
        keys[i] = "key_" + std::to_string(i);

    double shared_ns;
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto obj = JsonNode::MakeObject();
            for (int i = 0; i < n; i++) {
                obj->keys.push_back(keys[i]);
                obj->obj[keys[i]] = JsonNode::MakeInt(i);
            }
        }
        auto e = Clock::now();
        shared_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    double unique_ns;
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto obj = UNode::MakeObject();
            for (int i = 0; i < n; i++) {
                obj->keys.push_back(keys[i]);
                obj->obj[keys[i]] = UNode::MakeInt(i);
            }
        }
        auto e = Clock::now();
        unique_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    char label[64];
    snprintf(label, sizeof(label), "obj insert %d keys", n);
    add_row(label, shared_ns, unique_ns);
}

// ============================================================
// 3. Array append N elements
// ============================================================

static void bench_arr_append(int n) {
    int reps = std::max(1, 100000 / n);

    double shared_ns;
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto arr = JsonNode::MakeArray();
            for (int i = 0; i < n; i++)
                arr->arr.push_back(JsonNode::MakeInt(i));
        }
        auto e = Clock::now();
        shared_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    double unique_ns;
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto arr = UNode::MakeArray();
            for (int i = 0; i < n; i++)
                arr->arr.push_back(UNode::MakeInt(i));
        }
        auto e = Clock::now();
        unique_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    char label[64];
    snprintf(label, sizeof(label), "arr append %d", n);
    add_row(label, shared_ns, unique_ns);
}

// ============================================================
// 4. Object random access
// ============================================================

static void bench_obj_access(int n) {
    constexpr int OPS = 100000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, n - 1);
    std::vector<std::string> lookup_keys(OPS);
    for (int i = 0; i < OPS; i++)
        lookup_keys[i] = "key_" + std::to_string(dist(rng));

    // Build shared_ptr version
    auto s_obj = JsonNode::MakeObject();
    for (int i = 0; i < n; i++) {
        std::string k = "key_" + std::to_string(i);
        s_obj->keys.push_back(k);
        s_obj->obj[k] = JsonNode::MakeInt(i);
    }

    // Build unique_ptr version
    auto u_obj = UNode::MakeObject();
    for (int i = 0; i < n; i++) {
        std::string k = "key_" + std::to_string(i);
        u_obj->keys.push_back(k);
        u_obj->obj[k] = UNode::MakeInt(i);
    }

    double shared_ns;
    {
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            auto it = s_obj->obj.find(lookup_keys[i]);
            sink = it->second->int_val;
        }
        auto e = Clock::now();
        (void)sink;
        shared_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
    }

    double unique_ns;
    {
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            auto it = u_obj->obj.find(lookup_keys[i]);
            sink = it->second->int_val;
        }
        auto e = Clock::now();
        (void)sink;
        unique_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
    }

    char label[64];
    snprintf(label, sizeof(label), "obj[%d] random access", n);
    add_row(label, shared_ns, unique_ns);
}

// ============================================================
// 5. Array random access
// ============================================================

static void bench_arr_access(int n) {
    constexpr int OPS = 100000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, n - 1);
    std::vector<int> indices(OPS);
    for (int i = 0; i < OPS; i++)
        indices[i] = dist(rng);

    auto s_arr = JsonNode::MakeArray();
    for (int i = 0; i < n; i++)
        s_arr->arr.push_back(JsonNode::MakeInt(i));

    auto u_arr = UNode::MakeArray();
    for (int i = 0; i < n; i++)
        u_arr->arr.push_back(UNode::MakeInt(i));

    double shared_ns;
    {
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++)
            sink = s_arr->arr[indices[i]]->int_val;
        auto e = Clock::now();
        (void)sink;
        shared_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
    }

    double unique_ns;
    {
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++)
            sink = u_arr->arr[indices[i]]->int_val;
        auto e = Clock::now();
        (void)sink;
        unique_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
    }

    char label[64];
    snprintf(label, sizeof(label), "arr[%d] random access", n);
    add_row(label, shared_ns, unique_ns);
}

// ============================================================
// 6. Object random delete
// ============================================================

static void bench_obj_delete(int n) {
    int reps = std::max(1, 10000 / n);
    std::mt19937 rng(42);
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;
    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++)
        keys[i] = "key_" + std::to_string(i);

    double shared_ns;
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto obj = JsonNode::MakeObject();
            for (int i = 0; i < n; i++) {
                obj->keys.push_back(keys[i]);
                obj->obj[keys[i]] = JsonNode::MakeInt(i);
            }
            auto shuf = order;
            std::shuffle(shuf.begin(), shuf.end(), rng);
            for (int i = 0; i < n; i++) {
                auto& k = keys[shuf[i]];
                obj->obj.erase(k);
                auto it = std::find(obj->keys.begin(), obj->keys.end(), k);
                if (it != obj->keys.end()) obj->keys.erase(it);
            }
        }
        auto e = Clock::now();
        shared_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    double unique_ns;
    {
        std::mt19937 rng2(42);
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto obj = UNode::MakeObject();
            for (int i = 0; i < n; i++) {
                obj->keys.push_back(keys[i]);
                obj->obj[keys[i]] = UNode::MakeInt(i);
            }
            auto shuf = order;
            std::shuffle(shuf.begin(), shuf.end(), rng2);
            for (int i = 0; i < n; i++) {
                auto& k = keys[shuf[i]];
                obj->obj.erase(k);
                auto it = std::find(obj->keys.begin(), obj->keys.end(), k);
                if (it != obj->keys.end()) obj->keys.erase(it);
            }
        }
        auto e = Clock::now();
        unique_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    char label[64];
    snprintf(label, sizeof(label), "obj[%d] random delete", n);
    add_row(label, shared_ns, unique_ns);
}

// ============================================================
// 7. Array random delete
// ============================================================

static void bench_arr_delete(int n) {
    int reps = std::max(1, 10000 / n);

    double shared_ns;
    {
        std::mt19937 rng(42);
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto arr = JsonNode::MakeArray();
            for (int i = 0; i < n; i++)
                arr->arr.push_back(JsonNode::MakeInt(i));
            for (int remaining = n; remaining > 0; remaining--) {
                int idx = std::uniform_int_distribution<int>(0, remaining - 1)(rng);
                arr->arr.erase(arr->arr.begin() + idx);
            }
        }
        auto e = Clock::now();
        shared_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    double unique_ns;
    {
        std::mt19937 rng(42);
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto arr = UNode::MakeArray();
            for (int i = 0; i < n; i++)
                arr->arr.push_back(UNode::MakeInt(i));
            for (int remaining = n; remaining > 0; remaining--) {
                int idx = std::uniform_int_distribution<int>(0, remaining - 1)(rng);
                arr->arr.erase(arr->arr.begin() + idx);
            }
        }
        auto e = Clock::now();
        unique_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    char label[64];
    snprintf(label, sizeof(label), "arr[%d] random delete", n);
    add_row(label, shared_ns, unique_ns);
}

// ============================================================
// 8. Teardown: destroy a large tree
// ============================================================

static void bench_teardown(int n) {
    int reps = std::max(1, 10000 / n);

    double shared_ns;
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto obj = JsonNode::MakeObject();
            for (int i = 0; i < n; i++) {
                std::string k = "key_" + std::to_string(i);
                obj->keys.push_back(k);
                auto inner = JsonNode::MakeArray();
                inner->arr.push_back(JsonNode::MakeInt(i));
                inner->arr.push_back(JsonNode::MakeString("val"));
                obj->obj[k] = std::move(inner);
            }
            // destruction happens here
        }
        auto e = Clock::now();
        shared_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    double unique_ns;
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto obj = UNode::MakeObject();
            for (int i = 0; i < n; i++) {
                std::string k = "key_" + std::to_string(i);
                obj->keys.push_back(k);
                auto inner = UNode::MakeArray();
                inner->arr.push_back(UNode::MakeInt(i));
                inner->arr.push_back(UNode::MakeString("val"));
                obj->obj[k] = std::move(inner);
            }
        }
        auto e = Clock::now();
        unique_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / (reps * n);
    }

    char label[64];
    snprintf(label, sizeof(label), "build+destroy tree (%d nodes)", n * 3);
    add_row(label, shared_ns, unique_ns);
}

// ============================================================
// Main
// ============================================================

int main() {
    printf("shared_ptr vs unique_ptr Benchmark\n");
    printf("==================================\n\n");

    bench_alloc();

    for (int n : {10, 50, 1000}) bench_obj_insert(n);
    for (int n : {10, 50, 1000}) bench_arr_append(n);
    for (int n : {10, 50, 1000}) bench_obj_access(n);
    for (int n : {10, 50, 1000}) bench_arr_access(n);
    for (int n : {10, 50, 1000}) bench_obj_delete(n);
    for (int n : {10, 50, 1000}) bench_arr_delete(n);
    for (int n : {10, 50, 1000}) bench_teardown(n);

    printf("\n%-35s %12s %12s %8s\n", "Benchmark", "shared_ptr", "unique_ptr", "speedup");
    printf("%-35s %12s %12s %8s\n",
           "-----------------------------------", "------------", "------------", "--------");
    for (auto& r : table) {
        auto fmt = [](double ns) -> std::string {
            if (ns >= 1000) {
                char buf[32]; snprintf(buf, sizeof(buf), "%.1f us", ns / 1000.0); return buf;
            }
            char buf[32]; snprintf(buf, sizeof(buf), "%.1f ns", ns); return buf;
        };
        printf("%-35s %12s %12s %7.2fx\n",
               r.benchmark.c_str(),
               fmt(r.shared_ns).c_str(),
               fmt(r.unique_ns).c_str(),
               r.speedup);
    }

    return 0;
}
