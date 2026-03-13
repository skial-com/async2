#if defined _async2_crypto_included
    #endinput
#endif
#define _async2_crypto_included

// ============================================================================
// Crypto / Utility Natives
// ============================================================================

/**
 * Base64-encodes binary data.
 * @param input   Raw input bytes.
 * @param length  Number of input bytes.
 * @param output  Output buffer for base64 string.
 * @param maxlen  Output buffer size. Must be >= 4*ceil(length/3) + 1.
 * @return        Number of characters written (not counting null), or 0 on failure.
 */
native int async2_Base64Encode(const char[] input, int length, char[] output, int maxlen);

/**
 * Decodes a base64 string into raw bytes.
 * @param input   Null-terminated base64 string.
 * @param output  Output buffer for decoded bytes.
 * @param maxlen  Output buffer size.
 * @return        Number of bytes decoded, or -1 on failure.
 */
native int async2_Base64Decode(const char[] input, char[] output, int maxlen);

/**
 * Computes a SHA-256 hash. Output is a 64-character lowercase hex string.
 * @param input   Raw input bytes.
 * @param length  Number of input bytes.
 * @param output  Output buffer (must be >= 65 bytes).
 * @param maxlen  Output buffer size.
 * @return        1 on success, 0 on failure.
 */
native int async2_SHA256(const char[] input, int length, char[] output, int maxlen);

/**
 * Computes a SHA-1 hash. Output is a 40-character lowercase hex string.
 * @param input   Raw input bytes.
 * @param length  Number of input bytes.
 * @param output  Output buffer (must be >= 41 bytes).
 * @param maxlen  Output buffer size.
 * @return        1 on success, 0 on failure.
 */
native int async2_SHA1(const char[] input, int length, char[] output, int maxlen);

/**
 * Computes an MD5 hash. Output is a 32-character lowercase hex string.
 * @param input   Raw input bytes.
 * @param length  Number of input bytes.
 * @param output  Output buffer (must be >= 33 bytes).
 * @param maxlen  Output buffer size.
 * @return        1 on success, 0 on failure.
 */
native int async2_MD5(const char[] input, int length, char[] output, int maxlen);

/**
 * Computes a CRC32 checksum.
 * @param input   Raw input bytes.
 * @param length  Number of input bytes.
 * @return        CRC32 value as an integer. Use Format(buf, sizeof(buf), "%08x", val) to convert to hex.
 */
native int async2_CRC32(const char[] input, int length);

/**
 * Computes an HMAC. Output is a lowercase hex string.
 * @param algo    Algorithm: "sha256", "sha1", or "md5".
 * @param key     Secret key bytes.
 * @param keyLen  Number of key bytes.
 * @param input   Raw input bytes.
 * @param inputLen Number of input bytes.
 * @param output  Output buffer (must be >= digest*2+1: 65 for sha256, 41 for sha1, 33 for md5).
 * @param maxlen  Output buffer size.
 * @return        1 on success, 0 on failure.
 */
native int async2_HMAC(const char[] algo, const char[] key, int keyLen, const char[] input, int inputLen, char[] output, int maxlen);

/**
 * Hex-encodes binary data. Output is a lowercase hex string.
 * @param input   Raw input bytes.
 * @param length  Number of input bytes.
 * @param output  Output buffer for hex string.
 * @param maxlen  Output buffer size. Must be >= length*2 + 1.
 * @return        Number of characters written (not counting null), or 0 on failure.
 */
native int async2_HexEncode(const char[] input, int length, char[] output, int maxlen);

/**
 * Decodes a hex string into raw bytes. Accepts both upper and lowercase hex.
 * @param input   Null-terminated hex string (must have even length).
 * @param output  Output buffer for decoded bytes.
 * @param maxlen  Output buffer size.
 * @return        Number of bytes decoded, or -1 on failure (odd length, invalid chars, buffer too small).
 */
native int async2_HexDecode(const char[] input, char[] output, int maxlen);
