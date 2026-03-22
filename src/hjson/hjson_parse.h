#ifndef ASYNC2_HJSON_PARSE_H
#define ASYNC2_HJSON_PARSE_H

#include <cstddef>

struct DataNode;

// Parse HJSON text into a DataNode tree.
// Supports: comments (// and /* */), unquoted keys, unquoted string values,
// multiline strings ('''), trailing commas, optional root braces.
// Returns nullptr on malformed input.
DataNode* HjsonParse(const char* data, size_t len);

#endif
