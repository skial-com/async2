#ifndef ASYNC2_MSGPACK_PARSE_H
#define ASYNC2_MSGPACK_PARSE_H

#include <cstdint>
#include <cstddef>

struct DataNode;

// Parse MessagePack binary data into a DataNode tree.
// Returns nullptr on malformed data or non-string map keys.
DataNode* MsgPackParse(const uint8_t* data, size_t len);

// Get/set the maximum number of elements allowed in a single msgpack array or map.
// Payloads exceeding this limit return nullptr from MsgPackParse.
// Default: 1,000,000.
void MsgPackSetMaxElements(size_t limit);
size_t MsgPackGetMaxElements();

#endif
