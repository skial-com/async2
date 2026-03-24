#include "msgpack_parse.h"
#include "data_node.h"
#include <cstring>

static constexpr int kMaxDepth = 256;
static size_t g_max_container_elements = 1000000;

void MsgPackSetMaxElements(size_t limit) {
    g_max_container_elements = limit > 0 ? limit : 1000000;
}

size_t MsgPackGetMaxElements() {
    return g_max_container_elements;
}

namespace {

struct Reader {
    const uint8_t* data;
    size_t len;
    size_t pos;

    bool has(size_t n) const { return pos + n <= len; }

    uint8_t read_u8() { return data[pos++]; }

    uint16_t read_u16() {
        uint16_t v = (uint16_t(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        return v;
    }

    uint32_t read_u32() {
        uint32_t v = (uint32_t(data[pos]) << 24) | (uint32_t(data[pos + 1]) << 16) |
                     (uint32_t(data[pos + 2]) << 8) | data[pos + 3];
        pos += 4;
        return v;
    }

    uint64_t read_u64() {
        uint64_t hi = read_u32();
        uint64_t lo = read_u32();
        return (hi << 32) | lo;
    }

    float read_f32() {
        uint32_t bits = read_u32();
        float v;
        memcpy(&v, &bits, 4);
        return v;
    }

    double read_f64() {
        uint64_t bits = read_u64();
        double v;
        memcpy(&v, &bits, 8);
        return v;
    }

    const uint8_t* read_bytes(size_t n) {
        const uint8_t* p = data + pos;
        pos += n;
        return p;
    }
};

static DataNode* read_node(Reader& r, int depth);

static DataNode* read_map(Reader& r, size_t count, int depth) {
    if (count == 0)
        return DataNode::MakeObject();
    if (count > g_max_container_elements)
        return nullptr;

    // Peek at first key's type byte to determine int-keyed vs string-keyed map.
    // Avoids allocating a DataNode just to inspect the type.
    if (!r.has(1)) return nullptr;
    uint8_t peek = r.data[r.pos];
    bool int_keys = (peek <= 0x7f) || (peek >= 0xe0) ||
                    (peek >= 0xcc && peek <= 0xd3);
    bool str_keys = ((peek & 0xe0) == 0xa0) ||
                    (peek >= 0xd9 && peek <= 0xdb);

    if (!int_keys && !str_keys)
        return nullptr;

    if (int_keys) {
        auto* node = DataNode::MakeIntMap();
        for (size_t i = 0; i < count; i++) {
            DataNode* key_node = read_node(r, depth + 1);
            if (!key_node || key_node->type != DataType::Int) {
                DataNode::Decref(key_node);
                DataNode::Decref(node);
                return nullptr;
            }
            int64_t key = key_node->int_val;
            DataNode::Decref(key_node);
            DataNode* val = read_node(r, depth + 1);
            if (!val) { DataNode::Decref(node); return nullptr; }
            node->IntMapInsert(key, val);
        }
        return node;
    }

    // String keys — existing logic
    auto* obj = DataNode::MakeObject();
    for (size_t i = 0; i < count; i++) {
        DataNode* key_node = read_node(r, depth + 1);
        if (!key_node || key_node->type != DataType::String) {
            DataNode::Decref(key_node);
            DataNode::Decref(obj);
            return nullptr;
        }
        std::string key = key_node->str_val;
        DataNode::Decref(key_node);
        DataNode* val = read_node(r, depth + 1);
        if (!val) { DataNode::Decref(obj); return nullptr; }
        obj->ObjInsert(key, val);
    }
    return obj;
}

static DataNode* read_array(Reader& r, size_t count, int depth) {
    if (count > g_max_container_elements)
        return nullptr;
    auto* arr = DataNode::MakeArray();
    arr->arr.reserve(count);
    for (size_t i = 0; i < count; i++) {
        DataNode* elem = read_node(r, depth + 1);
        if (!elem) { DataNode::Decref(arr); return nullptr; }
        arr->arr.push_back(elem);
    }
    return arr;
}

static DataNode* read_node(Reader& r, int depth) {
    if (depth > kMaxDepth || !r.has(1))
        return nullptr;

    uint8_t b = r.read_u8();

    // Positive fixint (0x00 - 0x7f)
    if (b <= 0x7f)
        return DataNode::MakeInt(static_cast<int64_t>(b));

    // Negative fixint (0xe0 - 0xff)
    if (b >= 0xe0)
        return DataNode::MakeInt(static_cast<int64_t>(static_cast<int8_t>(b)));

    // Fixmap (0x80 - 0x8f)
    if ((b & 0xf0) == 0x80)
        return read_map(r, b & 0x0f, depth);

    // Fixarray (0x90 - 0x9f)
    if ((b & 0xf0) == 0x90)
        return read_array(r, b & 0x0f, depth);

    // Fixstr (0xa0 - 0xbf)
    if ((b & 0xe0) == 0xa0) {
        size_t len = b & 0x1f;
        if (!r.has(len)) return nullptr;
        const uint8_t* p = r.read_bytes(len);
        return DataNode::MakeString(reinterpret_cast<const char*>(p), len);
    }

    switch (b) {
        case 0xc0: return DataNode::MakeNull();      // nil
        case 0xc1: return nullptr;                     // never used
        case 0xc2: return DataNode::MakeBool(false);  // false
        case 0xc3: return DataNode::MakeBool(true);   // true

        // bin 8
        case 0xc4: {
            if (!r.has(1)) return nullptr;
            size_t len = r.read_u8();
            if (!r.has(len)) return nullptr;
            const uint8_t* p = r.read_bytes(len);
            return DataNode::MakeBinary(p, len);
        }
        // bin 16
        case 0xc5: {
            if (!r.has(2)) return nullptr;
            size_t len = r.read_u16();
            if (!r.has(len)) return nullptr;
            const uint8_t* p = r.read_bytes(len);
            return DataNode::MakeBinary(p, len);
        }
        // bin 32
        case 0xc6: {
            if (!r.has(4)) return nullptr;
            size_t len = r.read_u32();
            if (!r.has(len)) return nullptr;
            const uint8_t* p = r.read_bytes(len);
            return DataNode::MakeBinary(p, len);
        }

        // ext 8, ext 16, ext 32 — skip for v1
        case 0xc7: case 0xc8: case 0xc9:
            return nullptr;

        // float 32
        case 0xca: {
            if (!r.has(4)) return nullptr;
            return DataNode::MakeFloat(static_cast<double>(r.read_f32()));
        }
        // float 64
        case 0xcb: {
            if (!r.has(8)) return nullptr;
            return DataNode::MakeFloat(r.read_f64());
        }

        // uint 8
        case 0xcc: {
            if (!r.has(1)) return nullptr;
            return DataNode::MakeInt(static_cast<int64_t>(r.read_u8()));
        }
        // uint 16
        case 0xcd: {
            if (!r.has(2)) return nullptr;
            return DataNode::MakeInt(static_cast<int64_t>(r.read_u16()));
        }
        // uint 32
        case 0xce: {
            if (!r.has(4)) return nullptr;
            return DataNode::MakeInt(static_cast<int64_t>(r.read_u32()));
        }
        // uint 64
        case 0xcf: {
            if (!r.has(8)) return nullptr;
            return DataNode::MakeInt(static_cast<int64_t>(r.read_u64()));
        }

        // int 8
        case 0xd0: {
            if (!r.has(1)) return nullptr;
            return DataNode::MakeInt(static_cast<int64_t>(static_cast<int8_t>(r.read_u8())));
        }
        // int 16
        case 0xd1: {
            if (!r.has(2)) return nullptr;
            return DataNode::MakeInt(static_cast<int64_t>(static_cast<int16_t>(r.read_u16())));
        }
        // int 32
        case 0xd2: {
            if (!r.has(4)) return nullptr;
            return DataNode::MakeInt(static_cast<int64_t>(static_cast<int32_t>(r.read_u32())));
        }
        // int 64
        case 0xd3: {
            if (!r.has(8)) return nullptr;
            return DataNode::MakeInt(static_cast<int64_t>(r.read_u64()));
        }

        // fixext 1, 2, 4, 8, 16 — skip for v1
        case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8:
            return nullptr;

        // str 8
        case 0xd9: {
            if (!r.has(1)) return nullptr;
            size_t len = r.read_u8();
            if (!r.has(len)) return nullptr;
            const uint8_t* p = r.read_bytes(len);
            return DataNode::MakeString(reinterpret_cast<const char*>(p), len);
        }
        // str 16
        case 0xda: {
            if (!r.has(2)) return nullptr;
            size_t len = r.read_u16();
            if (!r.has(len)) return nullptr;
            const uint8_t* p = r.read_bytes(len);
            return DataNode::MakeString(reinterpret_cast<const char*>(p), len);
        }
        // str 32
        case 0xdb: {
            if (!r.has(4)) return nullptr;
            size_t len = r.read_u32();
            if (!r.has(len)) return nullptr;
            const uint8_t* p = r.read_bytes(len);
            return DataNode::MakeString(reinterpret_cast<const char*>(p), len);
        }

        // array 16
        case 0xdc: {
            if (!r.has(2)) return nullptr;
            return read_array(r, r.read_u16(), depth);
        }
        // array 32
        case 0xdd: {
            if (!r.has(4)) return nullptr;
            return read_array(r, r.read_u32(), depth);
        }

        // map 16
        case 0xde: {
            if (!r.has(2)) return nullptr;
            return read_map(r, r.read_u16(), depth);
        }
        // map 32
        case 0xdf: {
            if (!r.has(4)) return nullptr;
            return read_map(r, r.read_u32(), depth);
        }

        default:
            return nullptr;
    }
}

} // anonymous namespace

DataNode* MsgPackParse(const uint8_t* data, size_t len) {
    if (!data || len == 0)
        return nullptr;

    Reader r{data, len, 0};
    DataNode* result = read_node(r, 0);

    // If we didn't consume all input, it's malformed
    if (result && r.pos != len) {
        DataNode::Decref(result);
        return nullptr;
    }

    return result;
}
