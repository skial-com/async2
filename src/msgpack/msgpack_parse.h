#ifndef ASYNC2_MSGPACK_PARSE_H
#define ASYNC2_MSGPACK_PARSE_H

#include <cstdint>
#include <cstddef>

struct DataNode;

// Parse MessagePack binary data into a DataNode tree.
// Returns nullptr on malformed data or non-string map keys.
DataNode* MsgPackParse(const uint8_t* data, size_t len);

#endif
