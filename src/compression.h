#ifndef ASYNC2_COMPRESSION_H
#define ASYNC2_COMPRESSION_H

#include <vector>
#include <cstddef>

bool CompressDeflate(const char* input, size_t input_len, std::vector<char>& output);

#endif
