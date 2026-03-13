#include "compression.h"
#include <zlib.h>

bool CompressDeflate(const char* input, size_t input_len, std::vector<char>& output) {
    uLongf compress_len = compressBound(input_len);
    output.resize(compress_len);

    int result = compress2(
        reinterpret_cast<Bytef*>(output.data()),
        &compress_len,
        reinterpret_cast<const Bytef*>(input),
        input_len,
        Z_BEST_SPEED
    );

    if (result != Z_OK)
        return false;

    output.resize(compress_len);
    return true;
}
