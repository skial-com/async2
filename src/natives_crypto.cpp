#include <cstring>
#include <zlib.h>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#endif

#include "smsdk_ext.h"
#include "natives.h"

// async2_Base64Encode(const char[] input, int length, char[] output, int maxlen)
static cell_t Native_Base64Encode(IPluginContext* pContext, const cell_t* params) {
    char* input;
    pContext->LocalToString(params[1], &input);
    int input_length = params[2];
    if (input_length <= 0)
        return 0;

    char* output;
    pContext->LocalToString(params[3], &output);
    int maxlen = params[4];

    int needed = 4 * ((input_length + 2) / 3) + 1;
    if (maxlen < needed)
        return 0;

#ifdef _WIN32
    DWORD written = static_cast<DWORD>(maxlen);
    if (!CryptBinaryToStringA(
            reinterpret_cast<const BYTE*>(input),
            static_cast<DWORD>(input_length),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            output,
            &written)) {
        return 0;
    }
    output[written] = '\0';
    return static_cast<cell_t>(written);
#else
    int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output),
                                  reinterpret_cast<const unsigned char*>(input),
                                  input_length);
    if (written < 0)
        return 0;

    output[written] = '\0';
    return written;
#endif
}

// async2_Base64Decode(const char[] input, char[] output, int maxlen)
static cell_t Native_Base64Decode(IPluginContext* pContext, const cell_t* params) {
    char* input;
    pContext->LocalToString(params[1], &input);

    char* output;
    pContext->LocalToString(params[2], &output);
    int maxlen = params[3];

    int input_length = static_cast<int>(strlen(input));
    if (input_length == 0)
        return 0;

    // Check output buffer size before decoding to prevent overflow
    int max_decoded = 3 * (input_length / 4);
    if (maxlen < max_decoded + 1)
        return -1;

#ifdef _WIN32
    DWORD decoded_size = static_cast<DWORD>(maxlen);
    if (!CryptStringToBinaryA(
            input,
            static_cast<DWORD>(input_length),
            CRYPT_STRING_BASE64,
            reinterpret_cast<BYTE*>(output),
            &decoded_size,
            nullptr,
            nullptr)) {
        return -1;
    }
    output[decoded_size] = '\0';
    return static_cast<cell_t>(decoded_size);
#else
    int decoded = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(output),
                                  reinterpret_cast<const unsigned char*>(input),
                                  input_length);
    if (decoded < 0)
        return -1;

    // Adjust for padding: EVP_DecodeBlock returns max length including padding bytes
    if (input_length >= 1 && input[input_length - 1] == '=') decoded--;
    if (input_length >= 2 && input[input_length - 2] == '=') decoded--;

    if (decoded < 0)
        return -1;

    output[decoded] = '\0';
    return decoded;
#endif
}

static const char hex_chars[] = "0123456789abcdef";

#ifdef _WIN32

static bool ComputeHash(BCRYPT_ALG_HANDLE hAlg, ULONG digest_len,
                        const unsigned char* data, size_t len,
                        char* hex_output, int maxlen) {
    unsigned char digest[64];

    int needed = static_cast<int>(digest_len) * 2 + 1;
    if (maxlen < needed) return false;

    NTSTATUS status = BCryptHash(
        hAlg,
        nullptr, 0,
        const_cast<PUCHAR>(data), static_cast<ULONG>(len),
        digest, digest_len);
    if (!BCRYPT_SUCCESS(status)) return false;

    for (ULONG i = 0; i < digest_len; i++) {
        hex_output[i * 2]     = hex_chars[(digest[i] >> 4) & 0xf];
        hex_output[i * 2 + 1] = hex_chars[digest[i] & 0xf];
    }
    hex_output[digest_len * 2] = '\0';
    return true;
}

static cell_t HashNative(IPluginContext* pContext, const cell_t* params,
                         BCRYPT_ALG_HANDLE hAlg, ULONG digest_len) {
    char* input;
    pContext->LocalToString(params[1], &input);
    int length = params[2];
    if (length < 0)
        return 0;

    char* output;
    pContext->LocalToString(params[3], &output);
    int maxlen = params[4];

    if (!ComputeHash(hAlg, digest_len,
                     reinterpret_cast<const unsigned char*>(input),
                     static_cast<size_t>(length), output, maxlen))
        return 0;

    return 1;
}

static cell_t Native_SHA256(IPluginContext* ctx, const cell_t* p) { return HashNative(ctx, p, BCRYPT_SHA256_ALG_HANDLE, 32); }
static cell_t Native_SHA1(IPluginContext* ctx, const cell_t* p) { return HashNative(ctx, p, BCRYPT_SHA1_ALG_HANDLE, 20); }
static cell_t Native_MD5(IPluginContext* ctx, const cell_t* p) { return HashNative(ctx, p, BCRYPT_MD5_ALG_HANDLE, 16); }

static bool GetAlgorithm(const char* algo, LPCWSTR* alg_id, ULONG* digest_len) {
    if (strcmp(algo, "sha256") == 0) { *alg_id = BCRYPT_SHA256_ALGORITHM; *digest_len = 32; }
    else if (strcmp(algo, "sha1") == 0) { *alg_id = BCRYPT_SHA1_ALGORITHM; *digest_len = 20; }
    else if (strcmp(algo, "md5") == 0) { *alg_id = BCRYPT_MD5_ALGORITHM; *digest_len = 16; }
    else return false;
    return true;
}

// async2_HMAC(const char[] algo, const char[] key, int keyLen, const char[] input, int inputLen, char[] output, int maxlen)
static cell_t Native_HMAC(IPluginContext* pContext, const cell_t* params) {
    char* algo;
    pContext->LocalToString(params[1], &algo);
    char* key;
    pContext->LocalToString(params[2], &key);
    int key_len = params[3];
    char* input;
    pContext->LocalToString(params[4], &input);
    int input_len = params[5];
    char* output;
    pContext->LocalToString(params[6], &output);
    int maxlen = params[7];

    if (key_len < 0 || input_len < 0)
        return 0;

    LPCWSTR alg_id;
    ULONG digest_len;
    if (!GetAlgorithm(algo, &alg_id, &digest_len))
        return 0;

    int needed = static_cast<int>(digest_len) * 2 + 1;
    if (maxlen < needed) return 0;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, alg_id, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) return 0;

    unsigned char digest[64];
    status = BCryptHash(hAlg,
                        reinterpret_cast<PUCHAR>(key), static_cast<ULONG>(key_len),
                        reinterpret_cast<PUCHAR>(input), static_cast<ULONG>(input_len),
                        digest, digest_len);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (!BCRYPT_SUCCESS(status)) return 0;

    for (ULONG i = 0; i < digest_len; i++) {
        output[i * 2]     = hex_chars[(digest[i] >> 4) & 0xf];
        output[i * 2 + 1] = hex_chars[digest[i] & 0xf];
    }
    output[digest_len * 2] = '\0';
    return 1;
}

#else // Linux/Mac — OpenSSL

static bool ComputeHash(const EVP_MD* md, const unsigned char* data, size_t len,
                        char* hex_output, int maxlen) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;

    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data, len) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    EVP_MD_CTX_free(ctx);

    int needed = digest_len * 2 + 1;
    if (maxlen < needed) return false;

    for (unsigned int i = 0; i < digest_len; i++) {
        hex_output[i * 2]     = hex_chars[(digest[i] >> 4) & 0xf];
        hex_output[i * 2 + 1] = hex_chars[digest[i] & 0xf];
    }
    hex_output[digest_len * 2] = '\0';
    return true;
}

static cell_t HashNative(IPluginContext* pContext, const cell_t* params, const EVP_MD* md) {
    char* input;
    pContext->LocalToString(params[1], &input);
    int length = params[2];
    if (length < 0)
        return 0;

    char* output;
    pContext->LocalToString(params[3], &output);
    int maxlen = params[4];

    if (!ComputeHash(md, reinterpret_cast<const unsigned char*>(input),
                     static_cast<size_t>(length), output, maxlen))
        return 0;

    return 1;
}

static cell_t Native_SHA256(IPluginContext* ctx, const cell_t* p) { return HashNative(ctx, p, EVP_sha256()); }
static cell_t Native_SHA1(IPluginContext* ctx, const cell_t* p) { return HashNative(ctx, p, EVP_sha1()); }
static cell_t Native_MD5(IPluginContext* ctx, const cell_t* p) { return HashNative(ctx, p, EVP_md5()); }

static const EVP_MD* GetAlgorithm(const char* algo) {
    if (strcmp(algo, "sha256") == 0) return EVP_sha256();
    if (strcmp(algo, "sha1") == 0) return EVP_sha1();
    if (strcmp(algo, "md5") == 0) return EVP_md5();
    return nullptr;
}

// async2_HMAC(const char[] algo, const char[] key, int keyLen, const char[] input, int inputLen, char[] output, int maxlen)
static cell_t Native_HMAC(IPluginContext* pContext, const cell_t* params) {
    char* algo;
    pContext->LocalToString(params[1], &algo);
    char* key;
    pContext->LocalToString(params[2], &key);
    int key_len = params[3];
    char* input;
    pContext->LocalToString(params[4], &input);
    int input_len = params[5];
    char* output;
    pContext->LocalToString(params[6], &output);
    int maxlen = params[7];

    if (key_len < 0 || input_len < 0)
        return 0;

    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac) return 0;

    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
    if (!ctx) {
        EVP_MAC_free(mac);
        return 0;
    }

    OSSL_PARAM mac_params[2];
    mac_params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, algo, 0);
    mac_params[1] = OSSL_PARAM_construct_end();

    unsigned char digest[EVP_MAX_MD_SIZE];
    size_t digest_len = 0;

    int ok = EVP_MAC_init(ctx, reinterpret_cast<const unsigned char*>(key),
                          static_cast<size_t>(key_len), mac_params) &&
             EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(input),
                            static_cast<size_t>(input_len)) &&
             EVP_MAC_final(ctx, digest, &digest_len, sizeof(digest));

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);

    if (!ok) return 0;

    int needed = static_cast<int>(digest_len) * 2 + 1;
    if (maxlen < needed) return 0;

    for (size_t i = 0; i < digest_len; i++) {
        output[i * 2]     = hex_chars[(digest[i] >> 4) & 0xf];
        output[i * 2 + 1] = hex_chars[digest[i] & 0xf];
    }
    output[digest_len * 2] = '\0';
    return 1;
}

#endif

// async2_HexEncode(const char[] input, int length, char[] output, int maxlen)
static cell_t Native_HexEncode(IPluginContext* pContext, const cell_t* params) {
    char* input;
    pContext->LocalToString(params[1], &input);
    int length = params[2];
    if (length <= 0)
        return 0;

    char* output;
    pContext->LocalToString(params[3], &output);
    int maxlen = params[4];

    int needed = length * 2 + 1;
    if (maxlen < needed)
        return 0;

    const auto* data = reinterpret_cast<const unsigned char*>(input);
    for (int i = 0; i < length; i++) {
        output[i * 2]     = hex_chars[(data[i] >> 4) & 0xf];
        output[i * 2 + 1] = hex_chars[data[i] & 0xf];
    }
    output[length * 2] = '\0';
    return length * 2;
}

// async2_HexDecode(const char[] input, char[] output, int maxlen)
static cell_t Native_HexDecode(IPluginContext* pContext, const cell_t* params) {
    char* input;
    pContext->LocalToString(params[1], &input);

    char* output;
    pContext->LocalToString(params[2], &output);
    int maxlen = params[3];

    int input_len = static_cast<int>(strlen(input));
    if (input_len == 0)
        return 0;
    if (input_len % 2 != 0)
        return -1;

    int decoded_len = input_len / 2;
    if (maxlen < decoded_len + 1)
        return -1;

    for (int i = 0; i < decoded_len; i++) {
        unsigned char hi, lo;
        char c = input[i * 2];
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else return -1;

        c = input[i * 2 + 1];
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        else return -1;

        output[i] = static_cast<char>((hi << 4) | lo);
    }
    output[decoded_len] = '\0';
    return decoded_len;
}

// async2_CRC32(const char[] input, int length)
static cell_t Native_CRC32(IPluginContext* pContext, const cell_t* params) {
    char* input;
    pContext->LocalToString(params[1], &input);
    int length = params[2];
    if (length < 0)
        return 0;

    uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(input), static_cast<uInt>(length));
    return static_cast<cell_t>(crc);
}

sp_nativeinfo_t g_CryptoNatives[] = {
    {"async2_Base64Encode",  Native_Base64Encode},
    {"async2_Base64Decode",  Native_Base64Decode},
    {"async2_SHA256",        Native_SHA256},
    {"async2_SHA1",          Native_SHA1},
    {"async2_MD5",           Native_MD5},
    {"async2_CRC32",         Native_CRC32},
    {"async2_HMAC",          Native_HMAC},
    {"async2_HexEncode",     Native_HexEncode},
    {"async2_HexDecode",     Native_HexDecode},
    {nullptr,                nullptr},
};
