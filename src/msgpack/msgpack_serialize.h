#ifndef ASYNC2_MSGPACK_SERIALIZE_H
#define ASYNC2_MSGPACK_SERIALIZE_H

#include <cstdint>
#include <vector>

struct DataNode;

// Serialize a DataNode tree into MessagePack binary format.
std::vector<uint8_t> MsgPackSerialize(const DataNode& node);

#endif
