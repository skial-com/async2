#include "msgpack_serialize.h"
#include "data_node.h"
#include <cstring>

namespace {

static void write_u8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

static void write_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

static void write_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

static void write_u64(std::vector<uint8_t>& out, uint64_t v) {
    write_u32(out, static_cast<uint32_t>(v >> 32));
    write_u32(out, static_cast<uint32_t>(v));
}

static void write_f64(std::vector<uint8_t>& out, double v) {
    uint64_t bits;
    memcpy(&bits, &v, 8);
    write_u64(out, bits);
}

static void write_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
    out.insert(out.end(), data, data + len);
}

static void write_msgpack_str(std::vector<uint8_t>& out, const char* data, size_t len) {
    if (len <= 31) {
        write_u8(out, 0xa0 | static_cast<uint8_t>(len));
    } else if (len <= 0xff) {
        write_u8(out, 0xd9);
        write_u8(out, static_cast<uint8_t>(len));
    } else if (len <= 0xffff) {
        write_u8(out, 0xda);
        write_u16(out, static_cast<uint16_t>(len));
    } else {
        write_u8(out, 0xdb);
        write_u32(out, static_cast<uint32_t>(len));
    }
    write_bytes(out, reinterpret_cast<const uint8_t*>(data), len);
}

static void write_msgpack_int(std::vector<uint8_t>& out, int64_t v) {
    if (v >= 0) {
        if (v <= 0x7f) {
            write_u8(out, static_cast<uint8_t>(v));
        } else if (v <= 0xff) {
            write_u8(out, 0xcc);
            write_u8(out, static_cast<uint8_t>(v));
        } else if (v <= 0xffff) {
            write_u8(out, 0xcd);
            write_u16(out, static_cast<uint16_t>(v));
        } else if (v <= 0xffffffffLL) {
            write_u8(out, 0xce);
            write_u32(out, static_cast<uint32_t>(v));
        } else {
            write_u8(out, 0xcf);
            write_u64(out, static_cast<uint64_t>(v));
        }
    } else {
        if (v >= -32) {
            write_u8(out, static_cast<uint8_t>(static_cast<int8_t>(v)));
        } else if (v >= -128) {
            write_u8(out, 0xd0);
            write_u8(out, static_cast<uint8_t>(static_cast<int8_t>(v)));
        } else if (v >= -32768) {
            write_u8(out, 0xd1);
            write_u16(out, static_cast<uint16_t>(static_cast<int16_t>(v)));
        } else if (v >= -2147483648LL) {
            write_u8(out, 0xd2);
            write_u32(out, static_cast<uint32_t>(static_cast<int32_t>(v)));
        } else {
            write_u8(out, 0xd3);
            write_u64(out, static_cast<uint64_t>(v));
        }
    }
}

static void write_msgpack_map_header(std::vector<uint8_t>& out, size_t count) {
    if (count <= 15) {
        write_u8(out, 0x80 | static_cast<uint8_t>(count));
    } else if (count <= 0xffff) {
        write_u8(out, 0xde);
        write_u16(out, static_cast<uint16_t>(count));
    } else {
        write_u8(out, 0xdf);
        write_u32(out, static_cast<uint32_t>(count));
    }
}

static void serialize_node(const DataNode& node, std::vector<uint8_t>& out) {
    switch (node.type) {
        case DataType::Null:
            write_u8(out, 0xc0);
            break;

        case DataType::Bool:
            write_u8(out, node.bool_val ? 0xc3 : 0xc2);
            break;

        case DataType::Int:
            write_msgpack_int(out, node.int_val);
            break;

        case DataType::Float:
            write_u8(out, 0xcb); // always float64
            write_f64(out, node.float_val);
            break;

        case DataType::String:
            write_msgpack_str(out, node.str_val.data(), node.str_val.size());
            break;

        case DataType::Array: {
            size_t count = node.arr.size();
            if (count <= 15) {
                write_u8(out, 0x90 | static_cast<uint8_t>(count));
            } else if (count <= 0xffff) {
                write_u8(out, 0xdc);
                write_u16(out, static_cast<uint16_t>(count));
            } else {
                write_u8(out, 0xdd);
                write_u32(out, static_cast<uint32_t>(count));
            }
            for (const auto* elem : node.arr)
                serialize_node(*elem, out);
            break;
        }

        case DataType::Object: {
            write_msgpack_map_header(out, node.obj.size());
            for (const auto& [key, val] : node.obj) {
                write_msgpack_str(out, key.data(), key.size());
                serialize_node(*val, out);
            }
            break;
        }

        case DataType::IntMap: {
            write_msgpack_map_header(out, node.intmap.size());
            for (const auto& [key, val] : node.intmap) {
                write_msgpack_int(out, key);
                serialize_node(*val, out);
            }
            break;
        }

        case DataType::Binary: {
            size_t len = node.bin.size();
            if (len <= 0xff) {
                write_u8(out, 0xc4);
                write_u8(out, static_cast<uint8_t>(len));
            } else if (len <= 0xffff) {
                write_u8(out, 0xc5);
                write_u16(out, static_cast<uint16_t>(len));
            } else {
                write_u8(out, 0xc6);
                write_u32(out, static_cast<uint32_t>(len));
            }
            write_bytes(out, node.bin.data(), len);
            break;
        }
    }
}

} // anonymous namespace

std::vector<uint8_t> MsgPackSerialize(const DataNode& node) {
    std::vector<uint8_t> result;
    serialize_node(node, result);
    return result;
}
