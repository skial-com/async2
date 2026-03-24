// MessagePack test suite
// Compile:
//   g++ -std=c++17 -O2 -I../src -I../src/data -I../src/msgpack \
//       -I../third_party/simdjson/singleheader \
//       -I../third_party/robin-map/include -I../third_party/xxhash \
//       -o test_msgpack test_msgpack.cpp \
//       ../src/data/data_node.cpp ../src/data/data_handle.cpp \
//       ../src/msgpack/msgpack_parse.cpp ../src/msgpack/msgpack_serialize.cpp \
//       ../third_party/simdjson/singleheader/simdjson.cpp && ./test_msgpack

#include "data_node.h"
#include "data_handle.h"
#include "msgpack_parse.h"
#include "msgpack_serialize.h"
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

#define CHECK(cond, name) do { \
    if (cond) { passed++; } \
    else { failed++; printf("  FAIL: %s\n", name); } \
} while(0)

static void test_nil() {
    uint8_t data[] = {0xc0};
    auto* n = MsgPackParse(data, 1);
    CHECK(n && n->type == DataType::Null, "nil");
    DataNode::Decref(n);
}

static void test_bool() {
    uint8_t t[] = {0xc3};
    uint8_t f[] = {0xc2};
    auto* nt = MsgPackParse(t, 1);
    auto* nf = MsgPackParse(f, 1);
    CHECK(nt && nt->type == DataType::Bool && nt->bool_val == true, "true");
    CHECK(nf && nf->type == DataType::Bool && nf->bool_val == false, "false");
    DataNode::Decref(nt);
    DataNode::Decref(nf);
}

static void test_positive_fixint() {
    uint8_t data[] = {0x00};
    auto* n0 = MsgPackParse(data, 1);
    CHECK(n0 && n0->type == DataType::Int && n0->int_val == 0, "fixint 0");
    DataNode::Decref(n0);

    data[0] = 0x7f;
    auto* n127 = MsgPackParse(data, 1);
    CHECK(n127 && n127->type == DataType::Int && n127->int_val == 127, "fixint 127");
    DataNode::Decref(n127);

    data[0] = 42;
    auto* n42 = MsgPackParse(data, 1);
    CHECK(n42 && n42->type == DataType::Int && n42->int_val == 42, "fixint 42");
    DataNode::Decref(n42);
}

static void test_negative_fixint() {
    uint8_t data[] = {0xff}; // -1
    auto* n = MsgPackParse(data, 1);
    CHECK(n && n->type == DataType::Int && n->int_val == -1, "neg fixint -1");
    DataNode::Decref(n);

    data[0] = 0xe0; // -32
    auto* n32 = MsgPackParse(data, 1);
    CHECK(n32 && n32->type == DataType::Int && n32->int_val == -32, "neg fixint -32");
    DataNode::Decref(n32);
}

static void test_uint8() {
    uint8_t data[] = {0xcc, 0xff};
    auto* n = MsgPackParse(data, 2);
    CHECK(n && n->type == DataType::Int && n->int_val == 255, "uint8 255");
    DataNode::Decref(n);
}

static void test_uint16() {
    uint8_t data[] = {0xcd, 0x01, 0x00};
    auto* n = MsgPackParse(data, 3);
    CHECK(n && n->type == DataType::Int && n->int_val == 256, "uint16 256");
    DataNode::Decref(n);
}

static void test_uint32() {
    uint8_t data[] = {0xce, 0x00, 0x01, 0x00, 0x00};
    auto* n = MsgPackParse(data, 5);
    CHECK(n && n->type == DataType::Int && n->int_val == 65536, "uint32 65536");
    DataNode::Decref(n);
}

static void test_uint64() {
    uint8_t data[] = {0xcf, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    auto* n = MsgPackParse(data, 9);
    CHECK(n && n->type == DataType::Int && n->int_val == 4294967296LL, "uint64");
    DataNode::Decref(n);
}

static void test_int8() {
    uint8_t data[] = {0xd0, 0x80}; // -128
    auto* n = MsgPackParse(data, 2);
    CHECK(n && n->type == DataType::Int && n->int_val == -128, "int8 -128");
    DataNode::Decref(n);
}

static void test_int16() {
    uint8_t data[] = {0xd1, 0x80, 0x00}; // -32768
    auto* n = MsgPackParse(data, 3);
    CHECK(n && n->type == DataType::Int && n->int_val == -32768, "int16 -32768");
    DataNode::Decref(n);
}

static void test_int32() {
    uint8_t data[] = {0xd2, 0x80, 0x00, 0x00, 0x00}; // -2147483648
    auto* n = MsgPackParse(data, 5);
    CHECK(n && n->type == DataType::Int && n->int_val == -2147483648LL, "int32 min");
    DataNode::Decref(n);
}

static void test_int64() {
    // -1 as int64
    uint8_t data[] = {0xd3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    auto* n = MsgPackParse(data, 9);
    CHECK(n && n->type == DataType::Int && n->int_val == -1, "int64 -1");
    DataNode::Decref(n);
}

static void test_float32() {
    // 3.14 as float32: 0x4048f5c3
    uint8_t data[] = {0xca, 0x40, 0x48, 0xf5, 0xc3};
    auto* n = MsgPackParse(data, 5);
    CHECK(n && n->type == DataType::Float && n->float_val > 3.13 && n->float_val < 3.15, "float32");
    DataNode::Decref(n);
}

static void test_float64() {
    // 3.14 as float64: 0x40091EB851EB851F
    uint8_t data[] = {0xcb, 0x40, 0x09, 0x1e, 0xb8, 0x51, 0xeb, 0x85, 0x1f};
    auto* n = MsgPackParse(data, 9);
    CHECK(n && n->type == DataType::Float && n->float_val > 3.139 && n->float_val < 3.141, "float64");
    DataNode::Decref(n);
}

static void test_fixstr() {
    uint8_t data[] = {0xa5, 'h', 'e', 'l', 'l', 'o'};
    auto* n = MsgPackParse(data, 6);
    CHECK(n && n->type == DataType::String && n->str_val == "hello", "fixstr");
    DataNode::Decref(n);
}

static void test_str8() {
    // "test" as str8
    uint8_t data[] = {0xd9, 0x04, 't', 'e', 's', 't'};
    auto* n = MsgPackParse(data, 6);
    CHECK(n && n->type == DataType::String && n->str_val == "test", "str8");
    DataNode::Decref(n);
}

static void test_empty_str() {
    uint8_t data[] = {0xa0};
    auto* n = MsgPackParse(data, 1);
    CHECK(n && n->type == DataType::String && n->str_val == "", "empty fixstr");
    DataNode::Decref(n);
}

static void test_bin8() {
    uint8_t data[] = {0xc4, 0x03, 0xde, 0xad, 0xbe};
    auto* n = MsgPackParse(data, 5);
    CHECK(n && n->type == DataType::Binary && n->bin.size() == 3 &&
          n->bin[0] == 0xde && n->bin[1] == 0xad && n->bin[2] == 0xbe, "bin8");
    DataNode::Decref(n);
}

static void test_bin16() {
    std::vector<uint8_t> data = {0xc5, 0x01, 0x00}; // 256 bytes
    data.resize(3 + 256, 0x42);
    auto* n = MsgPackParse(data.data(), data.size());
    CHECK(n && n->type == DataType::Binary && n->bin.size() == 256, "bin16");
    DataNode::Decref(n);
}

static void test_fixarray() {
    // [1, 2, 3]
    uint8_t data[] = {0x93, 0x01, 0x02, 0x03};
    auto* n = MsgPackParse(data, 4);
    CHECK(n && n->type == DataType::Array && n->arr.size() == 3 &&
          n->arr[0]->int_val == 1 && n->arr[1]->int_val == 2 && n->arr[2]->int_val == 3,
          "fixarray");
    DataNode::Decref(n);
}

static void test_empty_array() {
    uint8_t data[] = {0x90};
    auto* n = MsgPackParse(data, 1);
    CHECK(n && n->type == DataType::Array && n->arr.empty(), "empty array");
    DataNode::Decref(n);
}

static void test_fixmap() {
    // {"a": 1}
    uint8_t data[] = {0x81, 0xa1, 'a', 0x01};
    auto* n = MsgPackParse(data, 4);
    CHECK(n && n->type == DataType::Object && n->ObjSize() == 1, "fixmap");
    auto* val = n->ObjFind("a");
    CHECK(val && val->type == DataType::Int && val->int_val == 1, "fixmap value");
    DataNode::Decref(n);
}

static void test_empty_map() {
    uint8_t data[] = {0x80};
    auto* n = MsgPackParse(data, 1);
    CHECK(n && n->type == DataType::Object && n->ObjSize() == 0, "empty map");
    DataNode::Decref(n);
}

static void test_int_key_map() {
    // {1: "value"} — integer key, should parse as IntMap
    uint8_t data[] = {0x81, 0x01, 0xa5, 'v', 'a', 'l', 'u', 'e'};
    auto* n = MsgPackParse(data, 8);
    CHECK(n && n->type == DataType::IntMap, "int key map parsed as IntMap");
    if (n) {
        auto* val = n->IntMapFind(1);
        CHECK(val && val->type == DataType::String && val->str_val == "value",
              "int key map value");
    }
    DataNode::Decref(n);
}

static void test_truncated() {
    uint8_t data[] = {0xcd, 0x01}; // uint16 needs 2 more bytes, only 1
    auto* n = MsgPackParse(data, 2);
    CHECK(n == nullptr, "truncated data");
    DataNode::Decref(n);
}

static void test_extra_bytes() {
    uint8_t data[] = {0xc0, 0xc0}; // two nils — should reject
    auto* n = MsgPackParse(data, 2);
    CHECK(n == nullptr, "extra bytes rejected");
    DataNode::Decref(n);
}

static void test_empty_input() {
    auto* n = MsgPackParse(nullptr, 0);
    CHECK(n == nullptr, "null input");
    uint8_t empty = 0;
    n = MsgPackParse(&empty, 0);
    CHECK(n == nullptr, "empty input");
}

// Round-trip tests: build DataNode → serialize → parse → verify equality
static void test_roundtrip_null() {
    auto* n = DataNode::MakeNull();
    auto buf = MsgPackSerialize(*n);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->Equals(n), "roundtrip null");
    DataNode::Decref(n);
    DataNode::Decref(parsed);
}

static void test_roundtrip_bool() {
    auto* t = DataNode::MakeBool(true);
    auto* f = DataNode::MakeBool(false);
    auto bt = MsgPackSerialize(*t);
    auto bf = MsgPackSerialize(*f);
    auto* pt = MsgPackParse(bt.data(), bt.size());
    auto* pf = MsgPackParse(bf.data(), bf.size());
    CHECK(pt && pt->Equals(t), "roundtrip true");
    CHECK(pf && pf->Equals(f), "roundtrip false");
    DataNode::Decref(t); DataNode::Decref(f);
    DataNode::Decref(pt); DataNode::Decref(pf);
}

static void test_roundtrip_ints() {
    int64_t vals[] = {0, 1, 127, 128, 255, 256, 65535, 65536, 2147483647LL,
                      4294967295LL, 4294967296LL,
                      -1, -32, -33, -128, -129, -32768, -32769, -2147483648LL};
    for (int64_t v : vals) {
        auto* n = DataNode::MakeInt(v);
        auto buf = MsgPackSerialize(*n);
        auto* parsed = MsgPackParse(buf.data(), buf.size());
        char name[64];
        snprintf(name, sizeof(name), "roundtrip int %lld", (long long)v);
        CHECK(parsed && parsed->type == DataType::Int && parsed->int_val == v, name);
        DataNode::Decref(n);
        DataNode::Decref(parsed);
    }
}

static void test_roundtrip_float() {
    double vals[] = {0.0, 1.5, -1.5, 3.14159265358979, 1e100, -1e-100};
    for (double v : vals) {
        auto* n = DataNode::MakeFloat(v);
        auto buf = MsgPackSerialize(*n);
        auto* parsed = MsgPackParse(buf.data(), buf.size());
        char name[64];
        snprintf(name, sizeof(name), "roundtrip float %g", v);
        CHECK(parsed && parsed->type == DataType::Float && parsed->float_val == v, name);
        DataNode::Decref(n);
        DataNode::Decref(parsed);
    }
}

static void test_roundtrip_string() {
    const char* strs[] = {"", "hello", "a longer test string with spaces"};
    for (const char* s : strs) {
        auto* n = DataNode::MakeString(s);
        auto buf = MsgPackSerialize(*n);
        auto* parsed = MsgPackParse(buf.data(), buf.size());
        char name[128];
        snprintf(name, sizeof(name), "roundtrip string \"%s\"", s);
        CHECK(parsed && parsed->type == DataType::String && parsed->str_val == s, name);
        DataNode::Decref(n);
        DataNode::Decref(parsed);
    }
}

static void test_roundtrip_binary() {
    uint8_t bin_data[] = {0x00, 0x01, 0xff, 0xfe, 0x42};
    auto* n = DataNode::MakeBinary(bin_data, 5);
    auto buf = MsgPackSerialize(*n);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Binary && parsed->bin.size() == 5 &&
          memcmp(parsed->bin.data(), bin_data, 5) == 0, "roundtrip binary");
    DataNode::Decref(n);
    DataNode::Decref(parsed);
}

static void test_roundtrip_empty_binary() {
    auto* n = DataNode::MakeBinary(nullptr, 0);
    auto buf = MsgPackSerialize(*n);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Binary && parsed->bin.empty(), "roundtrip empty binary");
    DataNode::Decref(n);
    DataNode::Decref(parsed);
}

static void test_roundtrip_array() {
    auto* arr = DataNode::MakeArray();
    arr->arr.push_back(DataNode::MakeInt(1));
    arr->arr.push_back(DataNode::MakeString("two"));
    arr->arr.push_back(DataNode::MakeBool(true));
    arr->arr.push_back(DataNode::MakeNull());

    auto buf = MsgPackSerialize(*arr);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Array && parsed->arr.size() == 4, "roundtrip array size");
    CHECK(parsed && parsed->arr[0]->int_val == 1, "roundtrip array[0]");
    CHECK(parsed && parsed->arr[1]->str_val == "two", "roundtrip array[1]");
    CHECK(parsed && parsed->arr[2]->bool_val == true, "roundtrip array[2]");
    CHECK(parsed && parsed->arr[3]->type == DataType::Null, "roundtrip array[3]");
    DataNode::Decref(arr);
    DataNode::Decref(parsed);
}

static void test_roundtrip_object() {
    auto* obj = DataNode::MakeObject();
    obj->ObjInsert("name", DataNode::MakeString("test"));
    obj->ObjInsert("count", DataNode::MakeInt(42));
    obj->ObjInsert("active", DataNode::MakeBool(true));

    auto buf = MsgPackSerialize(*obj);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Object && parsed->ObjSize() == 3, "roundtrip object size");
    auto* name = parsed ? parsed->ObjFind("name") : nullptr;
    auto* count = parsed ? parsed->ObjFind("count") : nullptr;
    auto* active = parsed ? parsed->ObjFind("active") : nullptr;
    CHECK(name && name->str_val == "test", "roundtrip object name");
    CHECK(count && count->int_val == 42, "roundtrip object count");
    CHECK(active && active->bool_val == true, "roundtrip object active");
    DataNode::Decref(obj);
    DataNode::Decref(parsed);
}

static void test_roundtrip_nested() {
    auto* obj = DataNode::MakeObject();
    auto* inner = DataNode::MakeArray();
    inner->arr.push_back(DataNode::MakeInt(1));
    inner->arr.push_back(DataNode::MakeInt(2));
    obj->ObjInsert("items", inner);
    obj->ObjInsert("meta", DataNode::MakeString("info"));

    auto buf = MsgPackSerialize(*obj);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Object, "roundtrip nested type");
    auto* items = parsed ? parsed->ObjFind("items") : nullptr;
    CHECK(items && items->type == DataType::Array && items->arr.size() == 2, "roundtrip nested items");
    DataNode::Decref(obj);
    DataNode::Decref(parsed);
}

static void test_roundtrip_large_array() {
    auto* arr = DataNode::MakeArray();
    for (int i = 0; i < 100; i++)
        arr->arr.push_back(DataNode::MakeInt(i));

    auto buf = MsgPackSerialize(*arr);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Array && parsed->arr.size() == 100, "roundtrip large array");
    bool all_ok = true;
    if (parsed) {
        for (int i = 0; i < 100; i++) {
            if (parsed->arr[i]->int_val != i) { all_ok = false; break; }
        }
    }
    CHECK(all_ok, "roundtrip large array values");
    DataNode::Decref(arr);
    DataNode::Decref(parsed);
}

static void test_roundtrip_long_string() {
    std::string s(300, 'x');
    auto* n = DataNode::MakeString(s.c_str());
    auto buf = MsgPackSerialize(*n);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::String && parsed->str_val == s, "roundtrip long string");
    DataNode::Decref(n);
    DataNode::Decref(parsed);
}

// Compact encoding tests
static void test_compact_encoding() {
    // Small positive int should be 1 byte (fixint)
    {
        auto* n = DataNode::MakeInt(42);
        auto buf = MsgPackSerialize(*n);
        CHECK(buf.size() == 1 && buf[0] == 42, "compact fixint");
        DataNode::Decref(n);
    }
    // Small negative int should be 1 byte (neg fixint)
    {
        auto* n = DataNode::MakeInt(-1);
        auto buf = MsgPackSerialize(*n);
        CHECK(buf.size() == 1 && buf[0] == 0xff, "compact neg fixint");
        DataNode::Decref(n);
    }
    // Short string should use fixstr
    {
        auto* n = DataNode::MakeString("hi");
        auto buf = MsgPackSerialize(*n);
        CHECK(buf.size() == 3 && buf[0] == 0xa2, "compact fixstr");
        DataNode::Decref(n);
    }
    // 128 should use uint8 (2 bytes)
    {
        auto* n = DataNode::MakeInt(128);
        auto buf = MsgPackSerialize(*n);
        CHECK(buf.size() == 2 && buf[0] == 0xcc, "compact uint8 for 128");
        DataNode::Decref(n);
    }
}

// IntMap tests

static void test_intmap_create() {
    auto* n = DataNode::MakeIntMap();
    CHECK(n && n->type == DataType::IntMap && n->IntMapSize() == 0, "intmap create empty");
    DataNode::Decref(n);
}

static void test_intmap_insert_find() {
    auto* n = DataNode::MakeIntMap();
    n->IntMapInsert(1, DataNode::MakeInt(100));
    n->IntMapInsert(2, DataNode::MakeString("hello"));
    n->IntMapInsert(-1, DataNode::MakeBool(true));
    n->IntMapInsert(0, DataNode::MakeNull());

    CHECK(n->IntMapSize() == 4, "intmap insert size");
    auto* v1 = n->IntMapFind(1);
    CHECK(v1 && v1->type == DataType::Int && v1->int_val == 100, "intmap find 1");
    auto* v2 = n->IntMapFind(2);
    CHECK(v2 && v2->type == DataType::String && v2->str_val == "hello", "intmap find 2");
    auto* vm1 = n->IntMapFind(-1);
    CHECK(vm1 && vm1->type == DataType::Bool && vm1->bool_val == true, "intmap find -1");
    auto* v0 = n->IntMapFind(0);
    CHECK(v0 && v0->type == DataType::Null, "intmap find 0");
    CHECK(n->IntMapFind(999) == nullptr, "intmap find missing");
    DataNode::Decref(n);
}

static void test_intmap_overwrite() {
    auto* n = DataNode::MakeIntMap();
    n->IntMapInsert(1, DataNode::MakeInt(100));
    n->IntMapInsert(1, DataNode::MakeInt(200));
    CHECK(n->IntMapSize() == 1, "intmap overwrite size");
    auto* v = n->IntMapFind(1);
    CHECK(v && v->int_val == 200, "intmap overwrite value");
    DataNode::Decref(n);
}

static void test_intmap_erase() {
    auto* n = DataNode::MakeIntMap();
    n->IntMapInsert(1, DataNode::MakeInt(100));
    n->IntMapInsert(2, DataNode::MakeInt(200));
    CHECK(n->IntMapErase(1), "intmap erase returns true");
    CHECK(!n->IntMapErase(999), "intmap erase missing returns false");
    CHECK(n->IntMapSize() == 1, "intmap erase size");
    CHECK(n->IntMapFind(1) == nullptr, "intmap erase removed");
    CHECK(n->IntMapFind(2) != nullptr, "intmap erase other intact");
    DataNode::Decref(n);
}

static void test_intmap_contains() {
    auto* n = DataNode::MakeIntMap();
    CHECK(!n->IntMapContains(1), "intmap contains empty");
    n->IntMapInsert(1, DataNode::MakeInt(100));
    CHECK(n->IntMapContains(1), "intmap contains after insert");
    CHECK(!n->IntMapContains(2), "intmap contains other");
    DataNode::Decref(n);
}

static void test_intmap_clear() {
    auto* n = DataNode::MakeIntMap();
    n->IntMapInsert(1, DataNode::MakeInt(100));
    n->IntMapInsert(2, DataNode::MakeInt(200));
    n->IntMapClear();
    CHECK(n->IntMapSize() == 0, "intmap clear empty");
    CHECK(!n->IntMapContains(1), "intmap clear key gone");
    // still usable
    n->IntMapInsert(3, DataNode::MakeInt(300));
    CHECK(n->IntMapSize() == 1, "intmap usable after clear");
    DataNode::Decref(n);
}

static void test_intmap_merge() {
    auto* a = DataNode::MakeIntMap();
    a->IntMapInsert(1, DataNode::MakeInt(100));
    a->IntMapInsert(2, DataNode::MakeInt(200));

    auto* b = DataNode::MakeIntMap();
    b->IntMapInsert(2, DataNode::MakeInt(999));
    b->IntMapInsert(3, DataNode::MakeInt(300));

    // Merge without overwrite
    a->IntMapMerge(b, false);
    CHECK(a->IntMapSize() == 3, "intmap merge no-overwrite size");
    CHECK(a->IntMapFind(2)->int_val == 200, "intmap merge no-overwrite keeps existing");
    CHECK(a->IntMapFind(3)->int_val == 300, "intmap merge no-overwrite adds new");

    // Merge with overwrite
    a->IntMapMerge(b, true);
    CHECK(a->IntMapFind(2)->int_val == 999, "intmap merge overwrite");

    DataNode::Decref(a);
    DataNode::Decref(b);
}

static void test_intmap_deepcopy() {
    auto* n = DataNode::MakeIntMap();
    n->IntMapInsert(1, DataNode::MakeInt(100));
    n->IntMapInsert(2, DataNode::MakeString("hello"));
    auto* inner = DataNode::MakeArray();
    inner->arr.push_back(DataNode::MakeInt(42));
    n->IntMapInsert(3, inner);

    auto* copy = n->DeepCopy();
    CHECK(copy && copy->type == DataType::IntMap, "intmap deepcopy type");
    CHECK(copy->IntMapSize() == 3, "intmap deepcopy size");
    CHECK(copy->IntMapFind(1)->int_val == 100, "intmap deepcopy val 1");
    CHECK(copy->IntMapFind(2)->str_val == "hello", "intmap deepcopy val 2");
    auto* arr = copy->IntMapFind(3);
    CHECK(arr && arr->type == DataType::Array && arr->arr.size() == 1, "intmap deepcopy val 3");

    // Mutate original — copy unaffected
    n->IntMapInsert(1, DataNode::MakeInt(999));
    CHECK(copy->IntMapFind(1)->int_val == 100, "intmap deepcopy independent");

    DataNode::Decref(n);
    DataNode::Decref(copy);
}

static void test_intmap_equals() {
    auto* a = DataNode::MakeIntMap();
    a->IntMapInsert(1, DataNode::MakeInt(100));
    a->IntMapInsert(2, DataNode::MakeString("hello"));

    auto* b = DataNode::MakeIntMap();
    b->IntMapInsert(1, DataNode::MakeInt(100));
    b->IntMapInsert(2, DataNode::MakeString("hello"));

    auto* c = DataNode::MakeIntMap();
    c->IntMapInsert(1, DataNode::MakeInt(100));
    c->IntMapInsert(2, DataNode::MakeString("world"));

    CHECK(a->Equals(b), "intmap equals same");
    CHECK(!a->Equals(c), "intmap equals different");

    // Different sizes
    auto* d = DataNode::MakeIntMap();
    d->IntMapInsert(1, DataNode::MakeInt(100));
    CHECK(!a->Equals(d), "intmap equals different size");

    // IntMap != Object
    auto* obj = DataNode::MakeObject();
    CHECK(!a->Equals(obj), "intmap != object");

    DataNode::Decref(a);
    DataNode::Decref(b);
    DataNode::Decref(c);
    DataNode::Decref(d);
    DataNode::Decref(obj);
}

static void test_intmap_estimate_bytes() {
    auto* empty = DataNode::MakeIntMap();
    size_t empty_size = empty->EstimateBytes();
    CHECK(empty_size > 0, "intmap estimate bytes empty > 0");

    auto* filled = DataNode::MakeIntMap();
    filled->IntMapInsert(1, DataNode::MakeInt(100));
    filled->IntMapInsert(2, DataNode::MakeString("a fairly long string"));
    size_t filled_size = filled->EstimateBytes();
    CHECK(filled_size > empty_size, "intmap estimate bytes filled > empty");

    DataNode::Decref(empty);
    DataNode::Decref(filled);
}

static void test_intmap_json_serialize() {
    auto* n = DataNode::MakeIntMap();
    n->IntMapInsert(1, DataNode::MakeInt(42));
    auto json = DataSerializeJson(*n);
    CHECK(json == "null", "intmap json serialize is null");
    DataNode::Decref(n);
}

static void test_intmap_msgpack_roundtrip() {
    auto* n = DataNode::MakeIntMap();
    n->IntMapInsert(1, DataNode::MakeString("one"));
    n->IntMapInsert(2, DataNode::MakeInt(200));
    n->IntMapInsert(-1, DataNode::MakeBool(true));

    auto buf = MsgPackSerialize(*n);
    CHECK(buf.size() > 0, "intmap msgpack serialize non-empty");

    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::IntMap, "intmap msgpack roundtrip type");
    CHECK(parsed->IntMapSize() == 3, "intmap msgpack roundtrip size");

    auto* v1 = parsed->IntMapFind(1);
    CHECK(v1 && v1->type == DataType::String && v1->str_val == "one", "intmap msgpack roundtrip val 1");
    auto* v2 = parsed->IntMapFind(2);
    CHECK(v2 && v2->type == DataType::Int && v2->int_val == 200, "intmap msgpack roundtrip val 2");
    auto* vm1 = parsed->IntMapFind(-1);
    CHECK(vm1 && vm1->type == DataType::Bool && vm1->bool_val == true, "intmap msgpack roundtrip val -1");

    DataNode::Decref(n);
    DataNode::Decref(parsed);
}

static void test_intmap_msgpack_empty_roundtrip() {
    auto* n = DataNode::MakeIntMap();
    auto buf = MsgPackSerialize(*n);
    CHECK(buf.size() > 0, "intmap empty msgpack serialize");

    // Empty map has no keys → parses as Object (no key type to inspect)
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Object, "intmap empty msgpack → Object");
    CHECK(parsed->ObjSize() == 0, "intmap empty msgpack size 0");

    DataNode::Decref(n);
    DataNode::Decref(parsed);
}

static void test_intmap_msgpack_nested() {
    // Object containing IntMap
    auto* obj = DataNode::MakeObject();
    auto* inner = DataNode::MakeIntMap();
    inner->IntMapInsert(10, DataNode::MakeInt(100));
    inner->IntMapInsert(20, DataNode::MakeInt(200));
    obj->ObjInsert("map", inner);

    auto buf = MsgPackSerialize(*obj);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Object, "intmap nested parse outer");
    auto* child = parsed->ObjFind("map");
    CHECK(child && child->type == DataType::IntMap, "intmap nested parse inner type");
    CHECK(child->IntMapFind(10)->int_val == 100, "intmap nested parse inner val");

    DataNode::Decref(obj);
    DataNode::Decref(parsed);
}

static void test_intmap_msgpack_mixed_keys_fail() {
    // Manually build a msgpack map with first key=int, second key=string
    // fixmap(2) + fixint(1) + fixint(100) + fixstr("k") + fixint(200)
    uint8_t data[] = {0x82, 0x01, 0x64, 0xa1, 'k', 0xc8};  // 0x82=fixmap(2), 1, 100, "k", 200
    // Actually let's be more precise:
    // fixmap(2): 0x82
    // key1: fixint(1) = 0x01, value1: fixint(100) = 0x64
    // key2: fixstr("k") = 0xa1 0x6b, value2: uint8(200) = 0xcc 0xc8
    uint8_t data2[] = {0x82, 0x01, 0x64, 0xa1, 0x6b, 0xcc, 0xc8};
    auto* n = MsgPackParse(data2, sizeof(data2));
    CHECK(n == nullptr, "msgpack mixed key types rejected");
    DataNode::Decref(n);
}

static void test_intmap_as_value() {
    // IntMap inside an array
    auto* arr = DataNode::MakeArray();
    auto* inner = DataNode::MakeIntMap();
    inner->IntMapInsert(1, DataNode::MakeInt(42));
    arr->arr.push_back(inner);

    auto buf = MsgPackSerialize(*arr);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::Array && parsed->arr.size() == 1, "intmap in array parse");
    CHECK(parsed->arr[0]->type == DataType::IntMap, "intmap in array type");
    CHECK(parsed->arr[0]->IntMapFind(1)->int_val == 42, "intmap in array value");

    DataNode::Decref(arr);
    DataNode::Decref(parsed);
}

static void test_intmap_large_keys() {
    auto* n = DataNode::MakeIntMap();
    // INT64_MAX
    n->IntMapInsert(INT64_MAX, DataNode::MakeString("max"));
    // INT64_MIN
    n->IntMapInsert(INT64_MIN, DataNode::MakeString("min"));
    // 0
    n->IntMapInsert(0, DataNode::MakeString("zero"));

    CHECK(n->IntMapSize() == 3, "intmap large keys size");
    CHECK(n->IntMapFind(INT64_MAX)->str_val == "max", "intmap INT64_MAX");
    CHECK(n->IntMapFind(INT64_MIN)->str_val == "min", "intmap INT64_MIN");
    CHECK(n->IntMapFind(0)->str_val == "zero", "intmap key 0");

    // Roundtrip through msgpack
    auto buf = MsgPackSerialize(*n);
    auto* parsed = MsgPackParse(buf.data(), buf.size());
    CHECK(parsed && parsed->type == DataType::IntMap, "intmap large keys roundtrip type");
    CHECK(parsed->IntMapSize() == 3, "intmap large keys roundtrip size");
    CHECK(parsed->IntMapFind(INT64_MAX)->str_val == "max", "intmap large keys roundtrip max");
    CHECK(parsed->IntMapFind(INT64_MIN)->str_val == "min", "intmap large keys roundtrip min");

    DataNode::Decref(n);
    DataNode::Decref(parsed);
}

static void test_intmap_wrong_type_guards() {
    // IntMap helpers on non-IntMap nodes should be no-ops / return defaults
    auto* obj = DataNode::MakeObject();
    CHECK(obj->IntMapFind(1) == nullptr, "IntMapFind on object returns null");
    CHECK(!obj->IntMapContains(1), "IntMapContains on object returns false");
    CHECK(obj->IntMapSize() == 0, "IntMapSize on object returns 0");
    CHECK(!obj->IntMapErase(1), "IntMapErase on object returns false");

    // IntMapInsert on object should destroy the value (no leak)
    obj->IntMapInsert(1, DataNode::MakeInt(42));
    CHECK(obj->IntMapSize() == 0, "IntMapInsert on object is no-op");

    DataNode::Decref(obj);
}

int main() {
    printf("MessagePack Test Suite\n");
    printf("======================\n\n");

    printf("[1/5] Parse individual types...\n");
    test_nil();
    test_bool();
    test_positive_fixint();
    test_negative_fixint();
    test_uint8();
    test_uint16();
    test_uint32();
    test_uint64();
    test_int8();
    test_int16();
    test_int32();
    test_int64();
    test_float32();
    test_float64();
    test_fixstr();
    test_str8();
    test_empty_str();
    test_bin8();
    test_bin16();
    test_fixarray();
    test_empty_array();
    test_fixmap();
    test_empty_map();
    printf("  passed: %d, failed: %d\n", passed, failed);

    int parse_passed = passed, parse_failed = failed;
    passed = 0; failed = 0;

    printf("[2/5] Error handling...\n");
    test_int_key_map();
    test_truncated();
    test_extra_bytes();
    test_empty_input();
    printf("  passed: %d, failed: %d\n", passed, failed);

    int err_passed = passed, err_failed = failed;
    passed = 0; failed = 0;

    printf("[3/5] Round-trip tests...\n");
    test_roundtrip_null();
    test_roundtrip_bool();
    test_roundtrip_ints();
    test_roundtrip_float();
    test_roundtrip_string();
    test_roundtrip_binary();
    test_roundtrip_empty_binary();
    printf("  passed: %d, failed: %d\n", passed, failed);

    int rt_passed = passed, rt_failed = failed;
    passed = 0; failed = 0;

    printf("[4/5] Round-trip containers...\n");
    test_roundtrip_array();
    test_roundtrip_object();
    test_roundtrip_nested();
    test_roundtrip_large_array();
    test_roundtrip_long_string();
    printf("  passed: %d, failed: %d\n", passed, failed);

    int container_passed = passed, container_failed = failed;
    passed = 0; failed = 0;

    printf("[5/5] Compact encoding...\n");
    test_compact_encoding();
    printf("  passed: %d, failed: %d\n", passed, failed);

    int compact_passed = passed, compact_failed = failed;
    passed = 0; failed = 0;

    printf("[6/6] IntMap tests...\n");
    test_intmap_create();
    test_intmap_insert_find();
    test_intmap_overwrite();
    test_intmap_erase();
    test_intmap_contains();
    test_intmap_clear();
    test_intmap_merge();
    test_intmap_deepcopy();
    test_intmap_equals();
    test_intmap_estimate_bytes();
    test_intmap_json_serialize();
    test_intmap_msgpack_roundtrip();
    test_intmap_msgpack_empty_roundtrip();
    test_intmap_msgpack_nested();
    test_intmap_msgpack_mixed_keys_fail();
    test_intmap_as_value();
    test_intmap_large_keys();
    test_intmap_wrong_type_guards();
    printf("  passed: %d, failed: %d\n", passed, failed);

    int intmap_passed = passed, intmap_failed = failed;

    int total_passed = parse_passed + err_passed + rt_passed + container_passed + compact_passed + intmap_passed;
    int total_failed = parse_failed + err_failed + rt_failed + container_failed + compact_failed + intmap_failed;

    printf("\n========================================\n");
    printf("TOTAL: %d passed, %d failed\n", total_passed, total_failed);
    printf("========================================\n");

    return total_failed > 0 ? 1 : 0;
}
