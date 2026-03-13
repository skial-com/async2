// Benchmark: compare JSON libraries on parse, access, and parse+convert-to-DataNode
//
// Libraries: custom (DataNode), yyjson, simdjson, jansson
//
// Build via cmake (from test/):
//   mkdir build && cd build && cmake .. && make bench_compare

#include "data_node.h"
#include "yyjson.h"
#include "simdjson.h"
#include "jansson.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

using Clock = std::chrono::high_resolution_clock;

// ============================================================
// Helpers
// ============================================================

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Row {
    std::string benchmark;
    double custom_ns;
    double yyjson_ns;
    double simdjson_ns;
    double jansson_ns;
};

static std::vector<Row> table;

static int auto_ops(size_t data_bytes, int max_ops = 100000) {
    return std::max(1, std::min(max_ops, (int)(500000000 / std::max(data_bytes, (size_t)1))));
}

// ============================================================
// Conversion helpers: library DOM -> DataNode
// ============================================================

static DataNode* yyjson_to_node(yyjson_val* val) {
    if (!val) return DataNode::MakeNull();
    switch (yyjson_get_type(val)) {
        case YYJSON_TYPE_NULL: return DataNode::MakeNull();
        case YYJSON_TYPE_BOOL: return DataNode::MakeBool(yyjson_get_bool(val));
        case YYJSON_TYPE_NUM:
            if (yyjson_is_int(val))  return DataNode::MakeInt(yyjson_get_sint(val));
            if (yyjson_is_uint(val)) return DataNode::MakeInt((int64_t)yyjson_get_uint(val));
            return DataNode::MakeFloat(yyjson_get_real(val));
        case YYJSON_TYPE_STR:
            return DataNode::MakeString(yyjson_get_str(val));
        case YYJSON_TYPE_ARR: {
            auto* node = DataNode::MakeArray();
            yyjson_val* item;
            yyjson_arr_iter iter;
            yyjson_arr_iter_init(val, &iter);
            while ((item = yyjson_arr_iter_next(&iter)))
                node->arr.push_back(yyjson_to_node(item));
            return node;
        }
        case YYJSON_TYPE_OBJ: {
            auto* node = DataNode::MakeObject();
            yyjson_val* key;
            yyjson_obj_iter iter;
            yyjson_obj_iter_init(val, &iter);
            while ((key = yyjson_obj_iter_next(&iter))) {
                yyjson_val* v = yyjson_obj_iter_get_val(key);
                node->ObjInsert(yyjson_get_str(key), yyjson_to_node(v));
            }
            return node;
        }
        default: return DataNode::MakeNull();
    }
}

static DataNode* simdjson_to_node(simdjson::ondemand::value val);

static DataNode* simdjson_to_node_element(simdjson::ondemand::document& doc) {
    auto tp = doc.type();
    if (tp.error()) return DataNode::MakeNull();
    switch (tp.value()) {
        case simdjson::ondemand::json_type::null:
            return DataNode::MakeNull();
        case simdjson::ondemand::json_type::boolean:
            return DataNode::MakeBool(doc.get_bool().value());
        case simdjson::ondemand::json_type::number: {
            auto i = doc.get_int64();
            if (!i.error()) return DataNode::MakeInt(i.value());
            auto u = doc.get_uint64();
            if (!u.error()) return DataNode::MakeInt((int64_t)u.value());
            return DataNode::MakeFloat(doc.get_double().value());
        }
        case simdjson::ondemand::json_type::string:
            return DataNode::MakeString(std::string(doc.get_string().value()).c_str());
        case simdjson::ondemand::json_type::array: {
            auto* node = DataNode::MakeArray();
            for (auto child : doc.get_array())
                node->arr.push_back(simdjson_to_node(child.value()));
            return node;
        }
        case simdjson::ondemand::json_type::object: {
            auto* node = DataNode::MakeObject();
            for (auto field : doc.get_object()) {
                std::string k(field.unescaped_key().value());
                node->ObjInsert(k, simdjson_to_node(field.value()));
            }
            return node;
        }
        default: return DataNode::MakeNull();
    }
}

static DataNode* simdjson_to_node(simdjson::ondemand::value val) {
    auto tp = val.type();
    if (tp.error()) return DataNode::MakeNull();
    switch (tp.value()) {
        case simdjson::ondemand::json_type::null:
            return DataNode::MakeNull();
        case simdjson::ondemand::json_type::boolean:
            return DataNode::MakeBool(val.get_bool().value());
        case simdjson::ondemand::json_type::number: {
            auto i = val.get_int64();
            if (!i.error()) return DataNode::MakeInt(i.value());
            auto u = val.get_uint64();
            if (!u.error()) return DataNode::MakeInt((int64_t)u.value());
            return DataNode::MakeFloat(val.get_double().value());
        }
        case simdjson::ondemand::json_type::string:
            return DataNode::MakeString(std::string(val.get_string().value()).c_str());
        case simdjson::ondemand::json_type::array: {
            auto* node = DataNode::MakeArray();
            for (auto child : val.get_array())
                node->arr.push_back(simdjson_to_node(child.value()));
            return node;
        }
        case simdjson::ondemand::json_type::object: {
            auto* node = DataNode::MakeObject();
            for (auto field : val.get_object()) {
                std::string k(field.unescaped_key().value());
                node->ObjInsert(k, simdjson_to_node(field.value()));
            }
            return node;
        }
        default: return DataNode::MakeNull();
    }
}

static DataNode* jansson_to_node(json_t* val) {
    if (!val) return DataNode::MakeNull();
    switch (json_typeof(val)) {
        case JSON_NULL:    return DataNode::MakeNull();
        case JSON_TRUE:    return DataNode::MakeBool(true);
        case JSON_FALSE:   return DataNode::MakeBool(false);
        case JSON_INTEGER: return DataNode::MakeInt(json_integer_value(val));
        case JSON_REAL:    return DataNode::MakeFloat(json_real_value(val));
        case JSON_STRING:  return DataNode::MakeString(json_string_value(val));
        case JSON_ARRAY: {
            auto* node = DataNode::MakeArray();
            size_t i;
            json_t* item;
            json_array_foreach(val, i, item)
                node->arr.push_back(jansson_to_node(item));
            return node;
        }
        case JSON_OBJECT: {
            auto* node = DataNode::MakeObject();
            const char* key;
            json_t* item;
            json_object_foreach(val, key, item) {
                node->ObjInsert(key, jansson_to_node(item));
            }
            return node;
        }
        default: return DataNode::MakeNull();
    }
}

// ============================================================
// Benchmarks
// ============================================================

static void bench_parse_only(const std::string& data) {
    int ops = auto_ops(data.size());
    printf("  (%d ops, %zu bytes)\n", ops, data.size());

    // -- custom --
    {
        auto s = Clock::now();
        for (int i = 0; i < ops; i++) {
            auto* r = DataParseJson(data.data(), data.size());
            DataNode::Destroy(r);
        }
        auto e = Clock::now();
        double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
        table.push_back({"parse only", ns, 0, 0, 0});
    }
    // -- yyjson --
    {
        auto s = Clock::now();
        for (int i = 0; i < ops; i++) {
            yyjson_doc* doc = yyjson_read(data.data(), data.size(), 0);
            yyjson_doc_free(doc);
        }
        auto e = Clock::now();
        table.back().yyjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
    }
    // -- simdjson --
    {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded = simdjson::padded_string(data);
        auto s = Clock::now();
        for (int i = 0; i < ops; i++) {
            auto doc = parser.iterate(padded);
            auto tp = doc.type().value();
            (void)tp;
        }
        auto e = Clock::now();
        table.back().simdjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
    }
    // -- jansson --
    {
        auto s = Clock::now();
        for (int i = 0; i < ops; i++) {
            json_error_t err;
            json_t* root = json_loadb(data.data(), data.size(), 0, &err);
            json_decref(root);
        }
        auto e = Clock::now();
        table.back().jansson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
    }
}

static void bench_parse_and_convert(const std::string& data) {
    int ops = auto_ops(data.size());

    // custom column: same as "parse only" (DataParseJson is the conversion)
    table.push_back({"parse + convert to DataNode", table.back().custom_ns, 0, 0, 0});

    // -- yyjson parse + convert --
    {
        auto s = Clock::now();
        for (int i = 0; i < ops; i++) {
            yyjson_doc* doc = yyjson_read(data.data(), data.size(), 0);
            auto* node = yyjson_to_node(yyjson_doc_get_root(doc));
            yyjson_doc_free(doc);
            DataNode::Destroy(node);
        }
        auto e = Clock::now();
        table.back().yyjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
    }
    // -- simdjson parse + convert --
    {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded = simdjson::padded_string(data);
        auto s = Clock::now();
        for (int i = 0; i < ops; i++) {
            auto doc = parser.iterate(padded);
            auto* node = simdjson_to_node_element(doc.value());
            DataNode::Destroy(node);
        }
        auto e = Clock::now();
        table.back().simdjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
    }
    // -- jansson parse + convert --
    {
        auto s = Clock::now();
        for (int i = 0; i < ops; i++) {
            json_error_t err;
            json_t* root = json_loadb(data.data(), data.size(), 0, &err);
            auto* node = jansson_to_node(root);
            json_decref(root);
            DataNode::Destroy(node);
        }
        auto e = Clock::now();
        table.back().jansson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / ops;
    }
}

// Access: random key lookup on an object parsed from JSON
static void bench_obj_access_native(const std::string& data, const char* obj_key, int n) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, n - 1);
    constexpr int OPS = 100000;
    std::vector<std::string> keys(OPS);
    for (int i = 0; i < OPS; i++)
        keys[i] = "key_" + std::to_string(dist(rng));

    char label[64];
    snprintf(label, sizeof(label), "obj[%d] random key access", n);

    // -- custom --
    {
        auto* root = DataParseJson(data.data(), data.size());
        auto* obj = root->ObjFind(obj_key);
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            auto* v = obj->ObjFind(keys[i]);
            sink = v->int_val;
        }
        auto e = Clock::now();
        (void)sink;
        double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
        table.push_back({label, ns, 0, 0, 0});
        DataNode::Destroy(root);
    }
    // -- yyjson --
    {
        yyjson_doc* doc = yyjson_read(data.data(), data.size(), 0);
        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* obj = yyjson_obj_get(root, obj_key);
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            yyjson_val* v = yyjson_obj_getn(obj, keys[i].c_str(), keys[i].size());
            sink = yyjson_get_sint(v);
        }
        auto e = Clock::now();
        (void)sink;
        table.back().yyjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
        yyjson_doc_free(doc);
    }
    // -- simdjson (not applicable for random access — on-demand parser is forward-only) --
    table.back().simdjson_ns = -1;

    // -- jansson --
    {
        json_error_t err;
        json_t* root = json_loadb(data.data(), data.size(), 0, &err);
        json_t* obj = json_object_get(root, obj_key);
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            json_t* v = json_object_get(obj, keys[i].c_str());
            sink = json_integer_value(v);
        }
        auto e = Clock::now();
        (void)sink;
        table.back().jansson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
        json_decref(root);
    }
}

// Access: random index on an array parsed from JSON
static void bench_arr_access_native(const std::string& data, const char* arr_key, int n) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, n - 1);
    constexpr int OPS = 100000;
    std::vector<int> indices(OPS);
    for (int i = 0; i < OPS; i++)
        indices[i] = dist(rng);

    char label[64];
    snprintf(label, sizeof(label), "arr[%d] random index access", n);

    // -- custom --
    {
        auto* root = DataParseJson(data.data(), data.size());
        auto* arr = root->ObjFind(arr_key);
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++)
            sink = arr->arr[indices[i]]->int_val;
        auto e = Clock::now();
        (void)sink;
        double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
        table.push_back({label, ns, 0, 0, 0});
        DataNode::Destroy(root);
    }
    // -- yyjson (array access is O(n) linear scan) --
    {
        yyjson_doc* doc = yyjson_read(data.data(), data.size(), 0);
        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* arr = yyjson_obj_get(root, arr_key);
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            yyjson_val* v = yyjson_arr_get(arr, indices[i]);
            sink = yyjson_get_sint(v);
        }
        auto e = Clock::now();
        (void)sink;
        table.back().yyjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
        yyjson_doc_free(doc);
    }
    // -- simdjson (not applicable - forward-only) --
    table.back().simdjson_ns = -1;

    // -- jansson --
    {
        json_error_t err;
        json_t* root = json_loadb(data.data(), data.size(), 0, &err);
        json_t* arr = json_object_get(root, arr_key);
        volatile int64_t sink = 0;
        auto s = Clock::now();
        for (int i = 0; i < OPS; i++) {
            json_t* v = json_array_get(arr, indices[i]);
            sink = json_integer_value(v);
        }
        auto e = Clock::now();
        (void)sink;
        table.back().jansson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / OPS;
        json_decref(root);
    }
}

// ============================================================
// Mutation benchmarks (custom, yyjson-mut, jansson — simdjson N/A)
// ============================================================

// -- Object: insert N new keys --
static void bench_obj_insert(int n) {
    constexpr int OPS = 100000;
    char label[64];
    snprintf(label, sizeof(label), "obj insert %d keys", n);

    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++)
        keys[i] = "key_" + std::to_string(i);

    // -- custom --
    {
        auto s = Clock::now();
        for (int r = 0; r < OPS / n; r++) {
            auto* obj = DataNode::MakeObject();
            for (int i = 0; i < n; i++)
                obj->ObjInsert(keys[i], DataNode::MakeInt(i));
            DataNode::Destroy(obj);
        }
        auto e = Clock::now();
        int total = (OPS / n) * n;
        double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
        table.push_back({label, ns, 0, -1, 0});
    }
    // -- yyjson mut --
    {
        auto s = Clock::now();
        for (int r = 0; r < OPS / n; r++) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
            yyjson_mut_val* obj = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, obj);
            for (int i = 0; i < n; i++) {
                yyjson_mut_obj_add_int(doc, obj, keys[i].c_str(), i);
            }
            yyjson_mut_doc_free(doc);
        }
        auto e = Clock::now();
        int total = (OPS / n) * n;
        table.back().yyjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
    }
    // -- jansson --
    {
        auto s = Clock::now();
        for (int r = 0; r < OPS / n; r++) {
            json_t* obj = json_object();
            for (int i = 0; i < n; i++) {
                json_object_set_new(obj, keys[i].c_str(), json_integer(i));
            }
            json_decref(obj);
        }
        auto e = Clock::now();
        int total = (OPS / n) * n;
        table.back().jansson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
    }
}

// -- Object: random delete from N keys --
static void bench_obj_delete(int n) {
    char label[64];
    snprintf(label, sizeof(label), "obj[%d] random delete", n);

    std::mt19937 rng(42);
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;

    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++)
        keys[i] = "key_" + std::to_string(i);

    int reps = std::max(1, 10000 / n);

    // Pre-generate all shuffle orders
    std::vector<std::vector<int>> shuffles(reps);
    for (int r = 0; r < reps; r++) {
        shuffles[r] = order;
        std::shuffle(shuffles[r].begin(), shuffles[r].end(), rng);
    }

    // -- custom --
    {
        double total_ns = 0;
        for (int r = 0; r < reps; r++) {
            auto* obj = DataNode::MakeObject();
            for (int i = 0; i < n; i++)
                obj->ObjInsert(keys[i], DataNode::MakeInt(i));
            auto s = Clock::now();
            for (int i = 0; i < n; i++)
                obj->ObjErase(keys[shuffles[r][i]]);
            auto e = Clock::now();
            total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            DataNode::Destroy(obj);
        }
        int total = reps * n;
        table.push_back({label, total_ns / total, 0, -1, 0});
    }
    // -- yyjson mut --
    {
        double total_ns = 0;
        for (int r = 0; r < reps; r++) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
            yyjson_mut_val* obj = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, obj);
            for (int i = 0; i < n; i++)
                yyjson_mut_obj_add_int(doc, obj, keys[i].c_str(), i);
            auto s = Clock::now();
            for (int i = 0; i < n; i++)
                yyjson_mut_obj_remove_keyn(obj, keys[shuffles[r][i]].c_str(), keys[shuffles[r][i]].size());
            auto e = Clock::now();
            total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            yyjson_mut_doc_free(doc);
        }
        int total = reps * n;
        table.back().yyjson_ns = total_ns / total;
    }
    // -- jansson --
    {
        double total_ns = 0;
        for (int r = 0; r < reps; r++) {
            json_t* obj = json_object();
            for (int i = 0; i < n; i++)
                json_object_set_new(obj, keys[i].c_str(), json_integer(i));
            auto s = Clock::now();
            for (int i = 0; i < n; i++)
                json_object_del(obj, keys[shuffles[r][i]].c_str());
            auto e = Clock::now();
            total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            json_decref(obj);
        }
        int total = reps * n;
        table.back().jansson_ns = total_ns / total;
    }
}

// -- Array: append N elements --
static void bench_arr_append(int n) {
    constexpr int OPS = 100000;
    char label[64];
    snprintf(label, sizeof(label), "arr append %d", n);

    // -- custom --
    {
        auto s = Clock::now();
        for (int r = 0; r < OPS / n; r++) {
            auto* arr = DataNode::MakeArray();
            for (int i = 0; i < n; i++)
                arr->arr.push_back(DataNode::MakeInt(i));
            DataNode::Destroy(arr);
        }
        auto e = Clock::now();
        int total = (OPS / n) * n;
        double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
        table.push_back({label, ns, 0, -1, 0});
    }
    // -- yyjson mut --
    {
        auto s = Clock::now();
        for (int r = 0; r < OPS / n; r++) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
            yyjson_mut_val* arr = yyjson_mut_arr(doc);
            yyjson_mut_doc_set_root(doc, arr);
            for (int i = 0; i < n; i++)
                yyjson_mut_arr_add_int(doc, arr, i);
            yyjson_mut_doc_free(doc);
        }
        auto e = Clock::now();
        int total = (OPS / n) * n;
        table.back().yyjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
    }
    // -- jansson --
    {
        auto s = Clock::now();
        for (int r = 0; r < OPS / n; r++) {
            json_t* arr = json_array();
            for (int i = 0; i < n; i++)
                json_array_append_new(arr, json_integer(i));
            json_decref(arr);
        }
        auto e = Clock::now();
        int total = (OPS / n) * n;
        table.back().jansson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
    }
}

// -- Array: random insert into array of size N --
static void bench_arr_random_insert(int n) {
    char label[64];
    snprintf(label, sizeof(label), "arr[%d] random insert", n);
    int reps = std::max(1, 10000 / n);
    std::mt19937 rng(42);

    std::vector<int> positions(n);
    for (int i = 0; i < n; i++)
        positions[i] = std::uniform_int_distribution<int>(0, i)(rng);

    // -- custom --
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            auto* arr = DataNode::MakeArray();
            for (int i = 0; i < n; i++)
                arr->arr.insert(arr->arr.begin() + positions[i], DataNode::MakeInt(i));
            DataNode::Destroy(arr);
        }
        auto e = Clock::now();
        int total = reps * n;
        double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
        table.push_back({label, ns, 0, -1, 0});
    }
    // -- yyjson mut --
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
            yyjson_mut_val* arr = yyjson_mut_arr(doc);
            yyjson_mut_doc_set_root(doc, arr);
            for (int i = 0; i < n; i++)
                yyjson_mut_arr_insert(arr, yyjson_mut_int(doc, i), positions[i]);
            yyjson_mut_doc_free(doc);
        }
        auto e = Clock::now();
        int total = reps * n;
        table.back().yyjson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
    }
    // -- jansson --
    {
        auto s = Clock::now();
        for (int r = 0; r < reps; r++) {
            json_t* arr = json_array();
            for (int i = 0; i < n; i++)
                json_array_insert_new(arr, positions[i], json_integer(i));
            json_decref(arr);
        }
        auto e = Clock::now();
        int total = reps * n;
        table.back().jansson_ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count() / total;
    }
}

// -- Array: random delete from array of size N --
static void bench_arr_random_delete(int n) {
    char label[64];
    snprintf(label, sizeof(label), "arr[%d] random delete", n);
    int reps = std::max(1, 10000 / n);
    std::mt19937 rng(42);

    // Pre-generate random indices for each rep
    std::vector<std::vector<int>> indices(reps);
    for (int r = 0; r < reps; r++) {
        indices[r].resize(n);
        for (int remaining = n; remaining > 0; remaining--)
            indices[r][n - remaining] = std::uniform_int_distribution<int>(0, remaining - 1)(rng);
    }

    // -- custom --
    {
        double total_ns = 0;
        for (int r = 0; r < reps; r++) {
            auto* arr = DataNode::MakeArray();
            for (int i = 0; i < n; i++)
                arr->arr.push_back(DataNode::MakeInt(i));
            auto s = Clock::now();
            for (int i = 0; i < n; i++) {
                int idx = indices[r][i];
                DataNode::Destroy(arr->arr[idx]);
                arr->arr.erase(arr->arr.begin() + idx);
            }
            auto e = Clock::now();
            total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            DataNode::Destroy(arr);
        }
        int total = reps * n;
        table.push_back({label, total_ns / total, 0, -1, 0});
    }
    // -- yyjson mut --
    {
        double total_ns = 0;
        for (int r = 0; r < reps; r++) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
            yyjson_mut_val* arr = yyjson_mut_arr(doc);
            yyjson_mut_doc_set_root(doc, arr);
            for (int i = 0; i < n; i++)
                yyjson_mut_arr_add_int(doc, arr, i);
            auto s = Clock::now();
            for (int i = 0; i < n; i++)
                yyjson_mut_arr_remove(arr, indices[r][i]);
            auto e = Clock::now();
            total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            yyjson_mut_doc_free(doc);
        }
        int total = reps * n;
        table.back().yyjson_ns = total_ns / total;
    }
    // -- jansson --
    {
        double total_ns = 0;
        for (int r = 0; r < reps; r++) {
            json_t* arr = json_array();
            for (int i = 0; i < n; i++)
                json_array_append_new(arr, json_integer(i));
            auto s = Clock::now();
            for (int i = 0; i < n; i++)
                json_array_remove(arr, indices[r][i]);
            auto e = Clock::now();
            total_ns += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            json_decref(arr);
        }
        int total = reps * n;
        table.back().jansson_ns = total_ns / total;
    }
}

// ============================================================
// Main
// ============================================================

static void print_table() {
    printf("\n");
    printf("%-30s %12s %12s %12s %12s\n", "Benchmark", "custom", "yyjson", "simdjson", "jansson");
    printf("%-30s %12s %12s %12s %12s\n",
           "------------------------------", "------------", "------------", "------------", "------------");
    for (auto& r : table) {
        auto fmt = [](double ns) -> std::string {
            if (ns < 0) return "N/A";
            if (ns >= 1000000) {
                char buf[32]; snprintf(buf, sizeof(buf), "%.2f ms", ns / 1000000.0); return buf;
            }
            if (ns >= 1000) {
                char buf[32]; snprintf(buf, sizeof(buf), "%.1f us", ns / 1000.0); return buf;
            }
            char buf[32]; snprintf(buf, sizeof(buf), "%.1f ns", ns); return buf;
        };
        printf("%-30s %12s %12s %12s %12s\n",
               r.benchmark.c_str(),
               fmt(r.custom_ns).c_str(),
               fmt(r.yyjson_ns).c_str(),
               fmt(r.simdjson_ns).c_str(),
               fmt(r.jansson_ns).c_str());
    }
}

int main(int argc, char** argv) {
    std::string data_file = "../data/bench.json";
    bool parse_only_custom = false;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--parse-only-custom")
            parse_only_custom = true;
        else
            data_file = argv[i];
    }

    std::string data = read_file(data_file);
    if (data.empty()) {
        printf("ERROR: could not read %s\n", data_file.c_str());
        return 1;
    }

    if (parse_only_custom) {
        int ops = 10000;
        printf("DataNode parse-only: %d ops, %zu bytes\n", ops, data.size());
        for (int i = 0; i < ops; i++) {
            auto* r = DataParseJson(data.data(), data.size());
            DataNode::Destroy(r);
        }
        return 0;
    }

    printf("JSON Library Comparison Benchmark\n");
    printf("=================================\n");
    printf("File: %s (%zu bytes)\n\n", data_file.c_str(), data.size());

    printf("[1] Parse only (native DOM)\n");
    bench_parse_only(data);

    printf("[2] Parse + convert to DataNode\n");
    bench_parse_and_convert(data);

    printf("[3] Object random key access (native DOM)\n");
    for (int n : {10, 50, 1000}) {
        char key[16];
        snprintf(key, sizeof(key), "obj_%d", n);
        bench_obj_access_native(data, key, n);
    }

    printf("[4] Array random index access (native DOM)\n");
    for (int n : {10, 50, 1000}) {
        char key[16];
        snprintf(key, sizeof(key), "arr_%d", n);
        bench_arr_access_native(data, key, n);
    }

    printf("[5] Object insert (native DOM)\n");
    for (int n : {10, 50, 1000})
        bench_obj_insert(n);

    printf("[6] Object random delete (native DOM)\n");
    for (int n : {10, 50, 1000})
        bench_obj_delete(n);

    printf("[7] Array append (native DOM)\n");
    for (int n : {10, 50, 1000})
        bench_arr_append(n);

    printf("[8] Array random insert (native DOM)\n");
    for (int n : {10, 50, 1000})
        bench_arr_random_insert(n);

    printf("[9] Array random delete (native DOM)\n");
    for (int n : {10, 50, 1000})
        bench_arr_random_delete(n);

    print_table();

    return 0;
}
