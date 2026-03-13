// Crypto / Utility Tests (synchronous)

void Test_CRC32() {
    // CRC32 of empty string is 0
    AssertEq(async2_CRC32("", 0), 0, "CRC32 empty string");

    // CRC32("hello") = 0x3610A686 = 907060870
    AssertEq(async2_CRC32("hello", 5), 907060870, "CRC32 hello");

    // CRC32("123456789") = 0xCBF43926 = -873187034 (signed)
    AssertEq(async2_CRC32("123456789", 9), -873187034, "CRC32 123456789");
}

void Test_HMAC_SHA256() {
    char out[65];

    // HMAC-SHA256("key", "hello") — RFC 4231 / known test vector
    int ret = async2_HMAC("sha256", "key", 3, "hello", 5, out, sizeof(out));
    AssertEq(ret, 1, "HMAC-SHA256 returns 1");
    AssertStrEq(out, "9307b3b915efb5171ff14d8cb55fbcc798c6c0ef1456d66ded1a6aa723a58b7b", "HMAC-SHA256 key+hello");
}

void Test_HMAC_SHA1() {
    char out[41];

    // HMAC-SHA1("key", "hello")
    int ret = async2_HMAC("sha1", "key", 3, "hello", 5, out, sizeof(out));
    AssertEq(ret, 1, "HMAC-SHA1 returns 1");
    AssertStrEq(out, "b34ceac4516ff23a143e61d79d0fa7a4fbe5f266", "HMAC-SHA1 key+hello");
}

void Test_HMAC_MD5() {
    char out[33];

    // HMAC-MD5("key", "hello")
    int ret = async2_HMAC("md5", "key", 3, "hello", 5, out, sizeof(out));
    AssertEq(ret, 1, "HMAC-MD5 returns 1");
    AssertStrEq(out, "04130747afca4d79e32e87cf2104f087", "HMAC-MD5 key+hello");
}

void Test_HMAC_InvalidAlgo() {
    char out[65];
    int ret = async2_HMAC("sha512", "key", 3, "hello", 5, out, sizeof(out));
    AssertEq(ret, 0, "HMAC invalid algo returns 0");
}

void Test_HMAC_EmptyKey() {
    char out[65];

    // HMAC-SHA256 with empty key
    int ret = async2_HMAC("sha256", "", 0, "hello", 5, out, sizeof(out));
    AssertEq(ret, 1, "HMAC-SHA256 empty key returns 1");
    AssertStrEq(out, "4352b26e33fe0d769a8922a6ba29004109f01688e26acc9e6cb347e5a5afc4da", "HMAC-SHA256 empty key");
}

void Test_HMAC_EmptyMessage() {
    char out[65];

    // HMAC-SHA256 with empty message
    int ret = async2_HMAC("sha256", "key", 3, "", 0, out, sizeof(out));
    AssertEq(ret, 1, "HMAC-SHA256 empty message returns 1");
    AssertStrEq(out, "5d5d139563c95b5967b9bd9a8c9b233a9dedb45072794cd232dc1b74832607d0", "HMAC-SHA256 empty message");
}

void Test_HMAC_BufferTooSmall() {
    char out[10];
    int ret = async2_HMAC("sha256", "key", 3, "hello", 5, out, sizeof(out));
    AssertEq(ret, 0, "HMAC buffer too small returns 0");
}

void Test_HexEncode() {
    char out[32];

    // Encode "hello" -> "68656c6c6f"
    int ret = async2_HexEncode("hello", 5, out, sizeof(out));
    AssertEq(ret, 10, "HexEncode hello length");
    AssertStrEq(out, "68656c6c6f", "HexEncode hello");

    // Empty input
    AssertEq(async2_HexEncode("", 0, out, sizeof(out)), 0, "HexEncode empty returns 0");

    // Buffer too small
    char tiny[4];
    AssertEq(async2_HexEncode("hello", 5, tiny, sizeof(tiny)), 0, "HexEncode buffer too small");
}

void Test_HexDecode() {
    char out[32];

    // Decode "68656c6c6f" -> "hello"
    int ret = async2_HexDecode("68656c6c6f", out, sizeof(out));
    AssertEq(ret, 5, "HexDecode length");
    AssertStrEq(out, "hello", "HexDecode hello");

    // Uppercase hex
    ret = async2_HexDecode("48454C4C4F", out, sizeof(out));
    AssertEq(ret, 5, "HexDecode uppercase length");
    AssertStrEq(out, "HELLO", "HexDecode uppercase");

    // Empty input
    AssertEq(async2_HexDecode("", out, sizeof(out)), 0, "HexDecode empty returns 0");

    // Odd length
    AssertEq(async2_HexDecode("abc", out, sizeof(out)), -1, "HexDecode odd length returns -1");

    // Invalid hex char
    AssertEq(async2_HexDecode("zz", out, sizeof(out)), -1, "HexDecode invalid char returns -1");
}

void Test_HexRoundTrip() {
    char hex[32];
    char decoded[16];

    // Encode then decode
    async2_HexEncode("test", 4, hex, sizeof(hex));
    int ret = async2_HexDecode(hex, decoded, sizeof(decoded));
    AssertEq(ret, 4, "Hex roundtrip length");
    AssertStrEq(decoded, "test", "Hex roundtrip value");
}

void RunCryptoTests() {
    Test_CRC32();
    Test_HMAC_SHA256();
    Test_HMAC_SHA1();
    Test_HMAC_MD5();
    Test_HMAC_InvalidAlgo();
    Test_HMAC_EmptyKey();
    Test_HMAC_EmptyMessage();
    Test_HMAC_BufferTooSmall();
    Test_HexEncode();
    Test_HexDecode();
    Test_HexRoundTrip();
}
