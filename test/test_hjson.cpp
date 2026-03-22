// HJSON parser test suite
// Includes unit tests and the hjson-cpp reference test suite

#include "data_node.h"
#include "hjson_parse.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>

static int passed = 0;
static int failed = 0;

#define CHECK(cond, name) do { \
    if (cond) { passed++; } \
    else { failed++; printf("  FAIL: %s\n", name); } \
} while(0)

static DataNode* parse(const char* s) {
    return HjsonParse(s, strlen(s));
}

// --- Basic JSON compatibility ---

static void test_json_object() {
    auto* n = parse(R"({"key": "value"})");
    CHECK(n && n->type == DataType::Object, "json object");
    auto* v = n->ObjFind("key");
    CHECK(v && v->type == DataType::String && v->str_val == "value", "json object value");
    DataNode::Destroy(n);
}

static void test_json_array() {
    auto* n = parse(R"([1, 2, 3])");
    CHECK(n && n->type == DataType::Array, "json array");
    CHECK(n->arr.size() == 3, "json array length");
    CHECK(n->arr[0]->int_val == 1, "json array[0]");
    CHECK(n->arr[2]->int_val == 3, "json array[2]");
    DataNode::Destroy(n);
}

static void test_json_nested() {
    auto* n = parse(R"({"a": {"b": [1, true, null]}})");
    CHECK(n && n->type == DataType::Object, "nested object");
    auto* a = n->ObjFind("a");
    CHECK(a && a->type == DataType::Object, "nested inner object");
    auto* b = a->ObjFind("b");
    CHECK(b && b->type == DataType::Array && b->arr.size() == 3, "nested array");
    CHECK(b->arr[0]->int_val == 1, "nested array int");
    CHECK(b->arr[1]->bool_val == true, "nested array bool");
    CHECK(b->arr[2]->type == DataType::Null, "nested array null");
    DataNode::Destroy(n);
}

static void test_json_string_escapes() {
    auto* n = parse(R"({"a": "hello\nworld\t!"})");
    CHECK(n != nullptr, "string escapes parse");
    auto* v = n->ObjFind("a");
    CHECK(v && v->str_val == "hello\nworld\t!", "string escapes value");
    DataNode::Destroy(n);
}

static void test_json_numbers() {
    auto* n = parse(R"({"i": 42, "f": 3.14, "neg": -7})");
    CHECK(n != nullptr, "numbers parse");
    CHECK(n->ObjFind("i")->int_val == 42, "int 42");
    CHECK(n->ObjFind("f")->type == DataType::Float, "float type");
    CHECK(n->ObjFind("neg")->int_val == -7, "negative int");
    DataNode::Destroy(n);
}

// --- HJSON-specific features ---

static void test_hash_comments() {
    auto* n = parse(R"({
        # This is a hash comment
        "key": "value" # inline comment after quoted value is a comment
    })");
    CHECK(n && n->type == DataType::Object, "hash comments");
    CHECK(n->ObjFind("key")->str_val == "value", "hash comment value");
    DataNode::Destroy(n);
}

static void test_line_comments() {
    auto* n = parse(R"({
        // This is a comment
        "key": "value" // inline comment
    })");
    CHECK(n && n->type == DataType::Object, "line comments");
    CHECK(n->ObjFind("key")->str_val == "value", "line comment value");
    DataNode::Destroy(n);
}

static void test_block_comments() {
    auto* n = parse(R"({
        /* block comment */
        "key": /* inline */ "value"
    })");
    CHECK(n && n->type == DataType::Object, "block comments");
    CHECK(n->ObjFind("key")->str_val == "value", "block comment value");
    DataNode::Destroy(n);
}

static void test_unquoted_keys() {
    auto* n = parse(R"({
        name: "John"
        age: 30
    })");
    CHECK(n && n->type == DataType::Object, "unquoted keys");
    CHECK(n->ObjFind("name")->str_val == "John", "unquoted key name");
    CHECK(n->ObjFind("age")->int_val == 30, "unquoted key age");
    DataNode::Destroy(n);
}

static void test_unquoted_values() {
    auto* n = parse(R"({
        message: hello world
        path: /usr/local/bin
    })");
    CHECK(n && n->type == DataType::Object, "unquoted values");
    CHECK(n->ObjFind("message")->str_val == "hello world", "unquoted value message");
    CHECK(n->ObjFind("path")->str_val == "/usr/local/bin", "unquoted value path");
    DataNode::Destroy(n);
}

static void test_trailing_commas() {
    auto* n = parse(R"({
        "a": 1,
        "b": 2,
    })");
    CHECK(n && n->type == DataType::Object, "trailing commas object");
    CHECK(n->ObjFind("a")->int_val == 1, "trailing comma a");
    CHECK(n->ObjFind("b")->int_val == 2, "trailing comma b");
    DataNode::Destroy(n);

    auto* arr = parse("[1, 2, 3,]");
    CHECK(arr && arr->type == DataType::Array && arr->arr.size() == 3, "trailing comma array");
    DataNode::Destroy(arr);
}

static void test_multiline_string() {
    auto* n = parse(R"({
    text:
        '''
        first line
          indented line
        last line
        '''
    })");
    CHECK(n && n->type == DataType::Object, "multiline string");
    auto* v = n->ObjFind("text");
    CHECK(v && v->type == DataType::String, "multiline is string");
    CHECK(v->str_val == "first line\n  indented line\nlast line", "multiline indent stripped");
    DataNode::Destroy(n);
}

static void test_multiline_embedded_quote() {
    // Single quotes inside ''' blocks should not terminate early
    auto* n = parse(R"({
    text:
        '''
        it's a test
        '''
    })");
    CHECK(n && n->type == DataType::Object, "multiline embedded quote");
    auto* v = n->ObjFind("text");
    CHECK(v && v->str_val.find("it's") != std::string::npos, "multiline contains it's");
    DataNode::Destroy(n);
}

static void test_single_quoted_string() {
    auto* n = parse(R"({
        a: 'single quoted'
        b: 'with \' escape'
        c: 'with \n newline'
    })");
    CHECK(n && n->type == DataType::Object, "single quoted strings");
    CHECK(n->ObjFind("a")->str_val == "single quoted", "single quoted value");
    CHECK(n->ObjFind("b")->str_val == "with ' escape", "single quote escape");
    CHECK(n->ObjFind("c")->str_val == "with \n newline", "single quote newline escape");
    DataNode::Destroy(n);
}

static void test_root_object_no_braces() {
    auto* n = parse(R"(
        name: John
        age: 30
        active: true
    )");
    CHECK(n && n->type == DataType::Object, "root no braces");
    CHECK(n->ObjFind("name")->str_val == "John", "root no braces name");
    CHECK(n->ObjFind("age")->int_val == 30, "root no braces age");
    CHECK(n->ObjFind("active")->bool_val == true, "root no braces bool");
    DataNode::Destroy(n);
}

static void test_keywords() {
    auto* n = parse(R"({
        a: true
        b: false
        c: null
    })");
    CHECK(n != nullptr, "keywords parse");
    CHECK(n->ObjFind("a")->type == DataType::Bool && n->ObjFind("a")->bool_val == true, "true keyword");
    CHECK(n->ObjFind("b")->type == DataType::Bool && n->ObjFind("b")->bool_val == false, "false keyword");
    CHECK(n->ObjFind("c")->type == DataType::Null, "null keyword");
    DataNode::Destroy(n);
}

static void test_mixed_separators() {
    auto* n = parse(R"({
        a: 1
        b: 2
        c: 3
    })");
    CHECK(n && n->type == DataType::Object, "newline separators");
    CHECK(n->ObjFind("a")->int_val == 1, "newline sep a");
    CHECK(n->ObjFind("b")->int_val == 2, "newline sep b");
    CHECK(n->ObjFind("c")->int_val == 3, "newline sep c");
    DataNode::Destroy(n);
}

static void test_empty_containers() {
    auto* obj = parse("{}");
    CHECK(obj && obj->type == DataType::Object && obj->ObjSize() == 0, "empty object");
    DataNode::Destroy(obj);

    auto* arr = parse("[]");
    CHECK(arr && arr->type == DataType::Array && arr->arr.size() == 0, "empty array");
    DataNode::Destroy(arr);
}

static void test_nested_hjson() {
    auto* n = parse(R"({
        // Server config
        server: {
            host: localhost
            port: 8080
            ssl: true
        }
        plugins: [
            plugin_a
            plugin_b
            plugin_c
        ]
    })");
    CHECK(n && n->type == DataType::Object, "nested hjson");
    auto* server = n->ObjFind("server");
    CHECK(server && server->type == DataType::Object, "nested server object");
    CHECK(server->ObjFind("host")->str_val == "localhost", "server host");
    CHECK(server->ObjFind("port")->int_val == 8080, "server port");
    CHECK(server->ObjFind("ssl")->bool_val == true, "server ssl");
    auto* plugins = n->ObjFind("plugins");
    CHECK(plugins && plugins->type == DataType::Array && plugins->arr.size() == 3, "plugins array");
    CHECK(plugins->arr[0]->str_val == "plugin_a", "plugin_a");
    DataNode::Destroy(n);
}

static void test_malformed_input() {
    CHECK(parse("") == nullptr, "empty string");
    CHECK(parse("{") == nullptr, "unclosed brace");
    CHECK(parse("[") == nullptr, "unclosed bracket");
    CHECK(parse("{key}") == nullptr, "missing colon");
    DataNode::Destroy(nullptr); // safe no-op
}

static void test_leading_zeros() {
    // Leading zeros: in HJSON, commas/braces are consumed into unquoted strings
    // if the value isn't a keyword/number. Use newlines as separators.
    auto* n = parse("{\n  a: 00\n  b: 01\n}");
    CHECK(n != nullptr, "leading zeros parse");
    CHECK(n->ObjFind("a")->type == DataType::String, "00 is string");
    CHECK(n->ObjFind("b")->type == DataType::String, "01 is string");
    DataNode::Destroy(n);

    // Single 0 is fine
    auto* n2 = parse("{\n  a: 0\n}");
    CHECK(n2 && n2->ObjFind("a")->type == DataType::Int && n2->ObjFind("a")->int_val == 0, "0 is int");
    DataNode::Destroy(n2);
}

static void test_unicode_escape() {
    auto* n = parse(R"({"key": "\u0041\u0042"})");
    CHECK(n != nullptr, "unicode escape parse");
    CHECK(n->ObjFind("key")->str_val == "AB", "unicode AB");
    DataNode::Destroy(n);
}

static void test_empty_multiline() {
    auto* n = parse("''''''");
    CHECK(n && n->type == DataType::String && n->str_val.empty(), "empty multiline string");
    DataNode::Destroy(n);
}

static void test_root_value_fallback() {
    // Bare string that looks like it could start a key but isn't
    auto* n = parse("allow quoteless strings");
    CHECK(n && n->type == DataType::String, "bare quoteless string");
    CHECK(n->str_val == "allow quoteless strings", "bare quoteless string value");
    DataNode::Destroy(n);

    // Bare number
    auto* n2 = parse("10");
    CHECK(n2 && n2->type == DataType::Int && n2->int_val == 10, "bare number");
    DataNode::Destroy(n2);

    // Bare number with whitespace
    auto* n3 = parse("\n10\n");
    CHECK(n3 && n3->type == DataType::Int && n3->int_val == 10, "bare number with ws");
    DataNode::Destroy(n3);
}

static void test_depth_limit() {
    std::string deep;
    for (int i = 0; i < 300; i++) deep += "{\"a\":";
    deep += "1";
    for (int i = 0; i < 300; i++) deep += "}";
    CHECK(parse(deep.c_str()) == nullptr, "depth limit");
}

// --- hjson-cpp reference test suite ---

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Recursively compare two DataNode trees for semantic equality
static bool deep_equal(const DataNode* a, const DataNode* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    switch (a->type) {
        case DataType::Null: return true;
        case DataType::Bool: return a->bool_val == b->bool_val;
        case DataType::Int: return a->int_val == b->int_val;
        case DataType::Float: {
            // Allow small relative difference for float serialization round-trips
            double da = a->float_val, db = b->float_val;
            if (da == db) return true;
            double diff = std::abs(da - db);
            double mag = std::max(std::abs(da), std::abs(db));
            return diff <= mag * 1e-10;
        }
        case DataType::String: return a->str_val == b->str_val;
        case DataType::Array:
            if (a->arr.size() != b->arr.size()) return false;
            for (size_t i = 0; i < a->arr.size(); i++)
                if (!deep_equal(a->arr[i], b->arr[i])) return false;
            return true;
        case DataType::Object:
            if (a->ObjSize() != b->ObjSize()) return false;
            for (auto it = a->obj.begin(); it != a->obj.end(); ++it) {
                auto* bv = b->ObjFind(it->first.c_str());
                if (!bv || !deep_equal(it->second, bv)) return false;
            }
            return true;
        default: return false;
    }
}

// Parse JSON with our simdjson-based parser for expected results
extern DataNode* DataParseJson(const char* data, size_t len, std::string* error_out);

static void run_hjson_cpp_tests(const std::string& assets_dir) {
    std::string listpath = assets_dir + "/testlist.txt";
    std::ifstream listfile(listpath);
    if (!listfile.is_open()) {
        printf("  SKIP: hjson-cpp test assets not found at %s\n", assets_dir.c_str());
        return;
    }

    std::string line;
    int suite_passed = 0, suite_failed = 0, suite_skipped = 0;

    while (std::getline(listfile, line)) {
        // Trim
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;

        std::string testname = line;
        // Strip _test.hjson or _test.json suffix to get base name
        std::string basename;
        if (testname.size() > 11 && testname.substr(testname.size() - 11) == "_test.hjson") {
            basename = testname.substr(0, testname.size() - 11);
        } else if (testname.size() > 10 && testname.substr(testname.size() - 10) == "_test.json") {
            basename = testname.substr(0, testname.size() - 10);
        } else {
            continue;
        }

        bool expect_fail = basename.substr(0, 4) == "fail";

        // Read input
        std::string input = read_file(assets_dir + "/" + testname);
        if (input.empty() && !expect_fail) {
            suite_skipped++;
            continue;
        }

        DataNode* result = HjsonParse(input.c_str(), input.size());

        if (expect_fail) {
            if (result == nullptr) {
                suite_passed++;
                passed++;
            } else {
                suite_failed++;
                failed++;
                printf("  FAIL: %s (should have failed but parsed ok)\n", basename.c_str());
                DataNode::Destroy(result);
            }
            continue;
        }

        // Pass test: must parse successfully
        if (!result) {
            suite_failed++;
            failed++;
            printf("  FAIL: %s (parse failed)\n", basename.c_str());
            continue;
        }

        // Compare against expected JSON output
        std::string expected_json = read_file(assets_dir + "/" + basename + "_result.json");
        if (expected_json.empty()) {
            // No expected output file, just verify it parsed
            suite_passed++;
            passed++;
            DataNode::Destroy(result);
            continue;
        }

        std::string parse_err;
        DataNode* expected = DataParseJson(expected_json.c_str(), expected_json.size(), &parse_err);
        if (!expected) {
            suite_skipped++;
            DataNode::Destroy(result);
            continue;
        }

        if (deep_equal(result, expected)) {
            suite_passed++;
            passed++;
        } else {
            suite_failed++;
            failed++;
            printf("  FAIL: %s (output mismatch)\n", basename.c_str());
        }

        DataNode::Destroy(result);
        DataNode::Destroy(expected);
    }

    printf("  hjson-cpp suite: %d passed, %d failed, %d skipped\n",
           suite_passed, suite_failed, suite_skipped);
}

int main(int argc, char* argv[]) {
    printf("HJSON parser tests\n\n");
    printf("Unit tests:\n");

    test_json_object();
    test_json_array();
    test_json_nested();
    test_json_string_escapes();
    test_json_numbers();
    test_hash_comments();
    test_line_comments();
    test_block_comments();
    test_unquoted_keys();
    test_unquoted_values();
    test_trailing_commas();
    test_multiline_string();
    test_multiline_embedded_quote();
    test_single_quoted_string();
    test_root_object_no_braces();
    test_keywords();
    test_mixed_separators();
    test_empty_containers();
    test_nested_hjson();
    test_malformed_input();
    test_leading_zeros();
    test_unicode_escape();
    test_empty_multiline();
    test_root_value_fallback();
    test_depth_limit();

    printf("  unit: %d passed, %d failed\n\n", passed, failed);

    // Run hjson-cpp reference tests
    // Try multiple paths for the test assets
    std::string assets_dir;
    if (argc > 1) {
        assets_dir = argv[1];
    } else {
        // Try relative to executable
        assets_dir = "../hjson_assets";
        std::ifstream probe(assets_dir + "/testlist.txt");
        if (!probe.is_open()) {
            assets_dir = "hjson_assets";
            std::ifstream probe2(assets_dir + "/testlist.txt");
            if (!probe2.is_open()) {
                assets_dir = "../../test/hjson_assets";
            }
        }
    }

    printf("hjson-cpp reference tests:\n");
    run_hjson_cpp_tests(assets_dir);

    printf("\nTotal: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
