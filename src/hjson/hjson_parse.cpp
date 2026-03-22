#include "hjson_parse.h"
#include "data_node.h"
#include <cstring>
#include <string>
#include <cstdlib>
#include <cmath>
#include <cerrno>
#include <memory>

// ---------------------------------------------------------------------------
// simdjson-inspired optimizations:
//   1. Padded input buffer — SIMD reads can safely overread past the end
//   2. AVX2-accelerated scanning — bulk whitespace skip, quote/backslash find,
//      newline find (32 bytes at a time)
//   3. Character classification lookup table — single table lookup replaces
//      multiple comparisons
//   4. Batch string appends — copy runs of non-special chars in bulk via
//      memcpy instead of char-by-char
//   5. Runtime CPU detection — scalar by default, AVX2 if detected
// ---------------------------------------------------------------------------

static constexpr int kMaxDepth = 256;
static constexpr size_t kPadding = 64; // safe overread zone for SIMD

// ===== Character classification table ======================================
// Replaces chains of if/switch with a single indexed load.

static constexpr uint8_t CC_WS      = 1;   // space, tab, \r, \n
static constexpr uint8_t CC_NL      = 2;   // \n, \r  (subset of WS)
static constexpr uint8_t CC_STRUCT  = 4;   // { } [ ] , :
static constexpr uint8_t CC_QUOTE   = 8;   // " '
static constexpr uint8_t CC_BSLASH  = 16;  // backslash
static constexpr uint8_t CC_HASH    = 32;  // #
static constexpr uint8_t CC_SLASH   = 64;  // /
static constexpr uint8_t CC_SQUOTE  = 128; // ' (also has CC_QUOTE)

static uint8_t g_cc[256];

static void init_char_table() {
    memset(g_cc, 0, sizeof(g_cc));
    g_cc[(unsigned char)' ']  = CC_WS;
    g_cc[(unsigned char)'\t'] = CC_WS;
    g_cc[(unsigned char)'\r'] = CC_WS | CC_NL;
    g_cc[(unsigned char)'\n'] = CC_WS | CC_NL;
    g_cc[(unsigned char)'{']  = CC_STRUCT;
    g_cc[(unsigned char)'}']  = CC_STRUCT;
    g_cc[(unsigned char)'[']  = CC_STRUCT;
    g_cc[(unsigned char)']']  = CC_STRUCT;
    g_cc[(unsigned char)',']  = CC_STRUCT;
    g_cc[(unsigned char)':']  = CC_STRUCT;
    g_cc[(unsigned char)'"']  = CC_QUOTE;
    g_cc[(unsigned char)'\''] = CC_QUOTE | CC_SQUOTE;
    g_cc[(unsigned char)'\\'] = CC_BSLASH;
    g_cc[(unsigned char)'#']  = CC_HASH;
    g_cc[(unsigned char)'/']  = CC_SLASH;
}

static inline uint8_t cc(char c) { return g_cc[(unsigned char)c]; }

// ===== SIMD scanner ========================================================
// Default: scalar. Upgraded to AVX2 at runtime if CPUID confirms support.
// AVX2 processes 32 bytes per iteration vs 1 byte for scalar.

// --- Platform helpers ------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define HJSON_X86
#endif

#ifdef HJSON_X86
#ifdef _MSC_VER
#include <intrin.h>
#include <immintrin.h>
static inline int hjson_ctz(uint32_t x) { unsigned long i; _BitScanForward(&i, x); return (int)i; }
#else
#include <cpuid.h>
#include <immintrin.h>
static inline int hjson_ctz(uint32_t x) { return __builtin_ctz(x); }
#endif
#endif // HJSON_X86

// --- Runtime AVX2 detection ------------------------------------------------

#ifdef HJSON_X86
static bool detect_avx2() {
#ifdef _MSC_VER
    // MSVC: use __cpuid / __cpuidex / _xgetbv intrinsics
    int info[4];
    __cpuid(info, 1);
    // Check OSXSAVE (bit 27) and AVX (bit 28) in ECX
    if ((info[2] & ((1 << 27) | (1 << 28))) != ((1 << 27) | (1 << 28)))
        return false;
    // Verify OS has enabled AVX state saving (XCR0 bits 1 and 2)
    unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6)
        return false;
    // Check AVX2 (CPUID.7.0:EBX bit 5)
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
#else
    // GCC/Clang: use __get_cpuid and inline asm for xgetbv
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return false;
    if ((ecx & ((1 << 27) | (1 << 28))) != ((1 << 27) | (1 << 28)))
        return false;
    // xgetbv — read XCR0
    unsigned int xcr0_lo, xcr0_hi;
    __asm__ __volatile__("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
    if ((xcr0_lo & 0x6) != 0x6)
        return false;
    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
        return false;
    return (ebx & (1 << 5)) != 0;
#endif
}
#endif // HJSON_X86

// --- Scalar fallbacks (always compiled, used by default) -------------------

static size_t skip_ws_scalar(const char* buf, size_t len) {
    size_t i = 0;
    while (i < len && (cc(buf[i]) & CC_WS)) i++;
    return i;
}

static size_t find_quote_or_special_scalar(const char* buf, size_t len, char quote) {
    for (size_t i = 0; i < len; i++) {
        char c = buf[i];
        if (c == quote || c == '\\' || c == '\n' || c == '\r') return i;
    }
    return len;
}

static size_t find_newline_scalar(const char* buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '\n' || buf[i] == '\r') return i;
    return len;
}

static size_t find_char_scalar(const char* buf, size_t len, char target) {
    for (size_t i = 0; i < len; i++)
        if (buf[i] == target) return i;
    return len;
}

// --- AVX2 implementations (32 bytes at a time) -----------------------------
// On GCC/Clang: __attribute__((target("avx2"))) enables AVX2 codegen for
// these functions without requiring -mavx2 globally.
// On MSVC: intrinsics are always available; /arch:AVX2 is not required.

#ifdef HJSON_X86

#ifdef _MSC_VER
#define HJSON_AVX2_FUNC
#else
#define HJSON_AVX2_FUNC __attribute__((target("avx2")))
#endif

HJSON_AVX2_FUNC
static size_t skip_ws_avx2(const char* buf, size_t len) {
    const __m256i sp  = _mm256_set1_epi8(' ');
    const __m256i tab = _mm256_set1_epi8('\t');
    const __m256i cr  = _mm256_set1_epi8('\r');
    const __m256i lf  = _mm256_set1_epi8('\n');

    size_t i = 0;
    for (; i < len; i += 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + i));
        __m256i m = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(v, sp), _mm256_cmpeq_epi8(v, tab)),
            _mm256_or_si256(_mm256_cmpeq_epi8(v, cr), _mm256_cmpeq_epi8(v, lf)));
        uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(m));
        if (mask != 0xFFFFFFFF) {
            _mm256_zeroupper();
            size_t pos = i + hjson_ctz(~mask);
            return pos < len ? pos : len;
        }
    }
    _mm256_zeroupper();
    return len;
}

HJSON_AVX2_FUNC
static size_t find_quote_or_special_avx2(const char* buf, size_t len, char quote) {
    const __m256i q  = _mm256_set1_epi8(quote);
    const __m256i bs = _mm256_set1_epi8('\\');
    const __m256i nl = _mm256_set1_epi8('\n');
    const __m256i cr = _mm256_set1_epi8('\r');

    size_t i = 0;
    for (; i < len; i += 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + i));
        __m256i m = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(v, q), _mm256_cmpeq_epi8(v, bs)),
            _mm256_or_si256(_mm256_cmpeq_epi8(v, nl), _mm256_cmpeq_epi8(v, cr)));
        uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(m));
        if (mask) {
            _mm256_zeroupper();
            size_t pos = i + hjson_ctz(mask);
            return pos < len ? pos : len;
        }
    }
    _mm256_zeroupper();
    return len;
}

HJSON_AVX2_FUNC
static size_t find_newline_avx2(const char* buf, size_t len) {
    const __m256i nl = _mm256_set1_epi8('\n');
    const __m256i cr = _mm256_set1_epi8('\r');

    size_t i = 0;
    for (; i < len; i += 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + i));
        __m256i m = _mm256_or_si256(_mm256_cmpeq_epi8(v, nl), _mm256_cmpeq_epi8(v, cr));
        uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(m));
        if (mask) {
            _mm256_zeroupper();
            size_t pos = i + hjson_ctz(mask);
            return pos < len ? pos : len;
        }
    }
    _mm256_zeroupper();
    return len;
}

HJSON_AVX2_FUNC
static size_t find_char_avx2(const char* buf, size_t len, char target) {
    const __m256i t = _mm256_set1_epi8(target);

    size_t i = 0;
    for (; i < len; i += 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + i));
        uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, t)));
        if (mask) {
            _mm256_zeroupper();
            size_t pos = i + hjson_ctz(mask);
            return pos < len ? pos : len;
        }
    }
    _mm256_zeroupper();
    return len;
}

#endif // HJSON_X86

// --- Function pointer dispatch ---------------------------------------------
// Defaults to scalar. init_scanner() upgrades to AVX2 if runtime CPUID check
// confirms the CPU and OS support it.

struct Scanner {
    size_t (*skip_ws)(const char*, size_t);
    size_t (*find_quote_special)(const char*, size_t, char);
    size_t (*find_newline)(const char*, size_t);
    size_t (*find_char)(const char*, size_t, char);
};

static Scanner g_scan;

static void init_scanner() {
    // Default: scalar (safe on all CPUs)
    g_scan.skip_ws            = skip_ws_scalar;
    g_scan.find_quote_special = find_quote_or_special_scalar;
    g_scan.find_newline       = find_newline_scalar;
    g_scan.find_char          = find_char_scalar;

#ifdef HJSON_X86
    // HJSON_NO_SIMD=1 forces scalar path for testing
    const char* no_simd = getenv("HJSON_NO_SIMD");
    if (!no_simd && detect_avx2()) {
        g_scan.skip_ws            = skip_ws_avx2;
        g_scan.find_quote_special = find_quote_or_special_avx2;
        g_scan.find_newline       = find_newline_avx2;
        g_scan.find_char          = find_char_avx2;
    }
#endif
}

// ===== Parser ==============================================================

namespace {

struct Reader {
    const char* data;
    size_t len;
    size_t pos;

    bool eof() const { return pos >= len; }
    char peek() const { return pos < len ? data[pos] : '\0'; }
    char next() { return pos < len ? data[pos++] : '\0'; }
    char peek_at(size_t off) const { size_t p = pos + off; return p < len ? data[p] : '\0'; }

    // SIMD-accelerated whitespace + comment skip
    void skip_ws() {
        while (pos < len) {
            // Bulk-skip whitespace using SIMD scanner
            size_t adv = g_scan.skip_ws(data + pos, len - pos);
            pos += adv;
            if (pos >= len) return;

            char c = data[pos];
            if (c == '#') {
                pos++;
                size_t nl = g_scan.find_newline(data + pos, len - pos);
                pos += nl;
                if (pos < len) pos++; // skip \n
                continue;
            }
            if (c == '/' && pos + 1 < len) {
                if (data[pos + 1] == '/') {
                    pos += 2;
                    size_t nl = g_scan.find_newline(data + pos, len - pos);
                    pos += nl;
                    if (pos < len) pos++;
                    continue;
                }
                if (data[pos + 1] == '*') {
                    pos += 2;
                    while (pos + 1 < len) {
                        size_t star = g_scan.find_char(data + pos, len - pos, '*');
                        pos += star;
                        if (pos + 1 < len && data[pos] == '*' && data[pos + 1] == '/') {
                            pos += 2;
                            break;
                        }
                        if (pos < len) pos++;
                    }
                    continue;
                }
            }
            break;
        }
    }

    void skip_separator() {
        skip_ws();
        if (pos < len && data[pos] == ',') {
            pos++;
            skip_ws();
        }
    }
};

static DataNode* read_value(Reader& r, int depth);

static inline bool is_punctuator(char c) { return (cc(c) & CC_STRUCT) != 0; }

static void utf8_encode(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

static bool parse_u_escape(Reader& r, uint32_t& cp) {
    if (r.pos + 4 > r.len) return false;
    cp = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char h = r.data[r.pos + i];
        uint32_t hex;
        if (h >= '0' && h <= '9') hex = h - '0';
        else if (h >= 'a' && h <= 'f') hex = h - 'a' + 10;
        else if (h >= 'A' && h <= 'F') hex = h - 'A' + 10;
        else return false;
        cp = cp * 16 + hex;
    }
    r.pos += 4;
    return true;
}

// Parse ''' multiline string. Reader must be positioned after the opening '''.
static bool read_multiline_string(Reader& r, std::string& out) {
    size_t tripleStart = r.pos - 3;
    int indent = 0;
    for (size_t i = tripleStart; i > 0; i--) {
        char ch = r.data[i - 1];
        if (ch == '\n' || ch == '\0') break;
        indent++;
    }

    auto skipIndent = [&]() {
        int skip = indent;
        while (r.pos < r.len && r.data[r.pos] > 0 && r.data[r.pos] <= ' '
               && r.data[r.pos] != '\n' && skip > 0) {
            skip--;
            r.pos++;
        }
    };

    while (r.pos < r.len && r.data[r.pos] > 0 && r.data[r.pos] <= ' ' && r.data[r.pos] != '\n')
        r.pos++;
    if (r.pos < r.len && r.data[r.pos] == '\n') {
        r.pos++;
        skipIndent();
    }

    out.clear();
    int triple = 0;
    bool lastLf = false;

    while (r.pos < r.len) {
        char c = r.data[r.pos];
        if (c == '\'') {
            triple++;
            r.pos++;
            if (triple == 3) {
                if (lastLf && !out.empty() && out.back() == '\n')
                    out.pop_back();
                return true;
            }
            continue;
        }
        while (triple > 0) { out += '\''; triple--; lastLf = false; }
        if (c == '\n') {
            out += '\n';
            lastLf = true;
            r.pos++;
            skipIndent();
        } else if (c == '\r') {
            r.pos++;
        } else {
            out += c;
            lastLf = false;
            r.pos++;
        }
    }
    return false;
}

// Quoted string reader. Bulk-copies runs of normal characters, only drops
// to per-char processing for escape sequences.
static bool read_quoted_string(Reader& r, std::string& out, bool allowML = true) {
    char quote = r.peek();
    if (!(cc(quote) & CC_QUOTE)) return false;
    r.next();

    if (cc(quote) & CC_SQUOTE) {
        if (allowML && r.peek() == '\'' && r.peek_at(1) == '\'') {
            r.pos += 2;
            return read_multiline_string(r, out);
        }
        if (r.peek() == '\'') { r.next(); out.clear(); return true; }
    }

    out.clear();
    while (r.pos < r.len) {
        size_t run = g_scan.find_quote_special(r.data + r.pos, r.len - r.pos, quote);
        if (run > 0) {
            out.append(r.data + r.pos, run);
            r.pos += run;
        }
        if (r.pos >= r.len) break;

        char c = r.next();
        if (c == quote) return true;
        if (c == '\n' || c == '\r') return false;
        if (c != '\\') { out += c; continue; }

        if (r.pos >= r.len) return false;
        char esc = r.next();
        switch (esc) {
            case '"':  out += '"';  break;
            case '\'': out += '\''; break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                uint32_t cp;
                if (!parse_u_escape(r, cp)) return false;
                utf8_encode(out, cp);
                break;
            }
            default: return false;
        }
    }
    return false;
}

static bool read_unquoted_key(Reader& r, std::string& out) {
    size_t keyStart = r.pos;
    size_t keyEnd = keyStart;
    int firstSpace = -1;

    while (r.pos < r.len) {
        char c = r.data[r.pos];
        if (c == ':') {
            if (keyEnd <= keyStart) return false;
            if (firstSpace >= 0 && static_cast<size_t>(firstSpace) != keyEnd)
                return false;
            out.assign(r.data + keyStart, keyEnd - keyStart);
            return true;
        }
        if (c <= ' ') {
            if (c == 0) return false;
            if (firstSpace < 0) firstSpace = static_cast<int>(r.pos);
            r.pos++;
        } else {
            if (is_punctuator(c)) return false;
            r.pos++;
            keyEnd = r.pos;
        }
    }
    return false;
}

static bool read_key(Reader& r, std::string& out) {
    char c = r.peek();
    if (cc(c) & CC_QUOTE) {
        if ((cc(c) & CC_SQUOTE) && r.peek_at(1) == '\'' && r.peek_at(2) == '\'')
            return false;
        return read_quoted_string(r, out, false);
    }
    return read_unquoted_key(r, out);
}

static DataNode* try_parse_number(const char* start, size_t len) {
    if (len == 0) return nullptr;
    const char* p = start;
    const char* pend = start + len;

    if (*p == '-') { p++; if (p == pend) return nullptr; }
    if (*p < '0' || *p > '9') return nullptr;
    if (*p == '0' && p + 1 < pend && p[1] >= '0' && p[1] <= '9') return nullptr;

    while (p < pend && *p >= '0' && *p <= '9') p++;
    bool is_float = false;

    if (p < pend && *p == '.') {
        is_float = true;
        p++;
        if (p == pend || *p < '0' || *p > '9') return nullptr;
        while (p < pend && *p >= '0' && *p <= '9') p++;
    }
    if (p < pend && (*p == 'e' || *p == 'E')) {
        is_float = true;
        p++;
        if (p < pend && (*p == '+' || *p == '-')) p++;
        if (p == pend || *p < '0' || *p > '9') return nullptr;
        while (p < pend && *p >= '0' && *p <= '9') p++;
    }
    if (p != pend) return nullptr;

    if (is_float) {
        char* dend;
        double dv = strtod(start, &dend);
        if (dend != pend || std::isinf(dv) || std::isnan(dv)) return nullptr;
        return DataNode::MakeFloat(dv);
    }

    char* iend;
    errno = 0;
    long long iv = strtoll(start, &iend, 10);
    if (iend == pend && errno != ERANGE)
        return DataNode::MakeInt(static_cast<int64_t>(iv));

    char* dend;
    double dv = strtod(start, &dend);
    if (dend == pend && !std::isinf(dv) && !std::isnan(dv))
        return DataNode::MakeFloat(dv);

    return nullptr;
}

static DataNode* try_parse_keyword_or_number(const char* start, size_t len) {
    if (len == 0) return nullptr;
    if (len == 4 && memcmp(start, "true", 4) == 0) return DataNode::MakeBool(true);
    if (len == 5 && memcmp(start, "false", 5) == 0) return DataNode::MakeBool(false);
    if (len == 4 && memcmp(start, "null", 4) == 0) return DataNode::MakeNull();
    if (*start == '-' || (*start >= '0' && *start <= '9'))
        return try_parse_number(start, len);
    return nullptr;
}

static inline bool is_comment_start(const char* data, size_t pos, size_t len) {
    char c = data[pos];
    if (cc(c) & CC_HASH) return true;
    if ((cc(c) & CC_SLASH) && pos + 1 < len)
        return data[pos + 1] == '/' || data[pos + 1] == '*';
    return false;
}

static DataNode* read_unquoted_value(Reader& r) {
    char first = r.peek();
    if (is_punctuator(first)) return nullptr;

    size_t valStart = r.pos;
    size_t valEnd = valStart;

    if (first <= ' ' && first > 0) valStart = r.pos + 1;

    for (;;) {
        if (r.pos >= r.len) break;
        char c = r.data[r.pos];
        if (cc(c) & CC_NL) break;

        bool isBreak = (c == ',' || c == '}' || c == ']' ||
                        is_comment_start(r.data, r.pos, r.len));
        if (isBreak) {
            DataNode* kw = try_parse_keyword_or_number(r.data + valStart, valEnd - valStart);
            if (kw) return kw;
        }

        r.pos++;
        if (static_cast<unsigned char>(c) > ' ')
            valEnd = r.pos;
    }

    size_t valLen = valEnd - valStart;
    if (valLen == 0) return nullptr;

    DataNode* kw = try_parse_keyword_or_number(r.data + valStart, valLen);
    if (kw) return kw;

    return DataNode::MakeString(r.data + valStart, valLen);
}

static DataNode* read_object(Reader& r, int depth, bool braced = true) {
    auto* obj = DataNode::MakeObject();

    r.skip_ws();
    while (r.pos < r.len && !(braced && r.data[r.pos] == '}')) {
        std::string key;
        if (!read_key(r, key)) { DataNode::Destroy(obj); return nullptr; }
        r.skip_ws();
        if (r.peek() != ':') { DataNode::Destroy(obj); return nullptr; }
        r.pos++;
        r.skip_ws();

        DataNode* val = read_value(r, depth + 1);
        if (!val) { DataNode::Destroy(obj); return nullptr; }
        obj->ObjInsert(key, val);
        r.skip_separator();
    }

    if (braced) {
        if (r.pos >= r.len || r.data[r.pos] != '}') { DataNode::Destroy(obj); return nullptr; }
        r.pos++;
    }
    return obj;
}

static DataNode* read_array(Reader& r, int depth) {
    auto* arr = DataNode::MakeArray();

    r.skip_ws();
    while (r.pos < r.len && r.data[r.pos] != ']') {
        DataNode* val = read_value(r, depth + 1);
        if (!val) { DataNode::Destroy(arr); return nullptr; }
        arr->arr.push_back(val);
        r.skip_separator();
    }

    if (r.pos >= r.len || r.data[r.pos] != ']') { DataNode::Destroy(arr); return nullptr; }
    r.pos++;
    return arr;
}

static DataNode* read_value(Reader& r, int depth) {
    if (depth > kMaxDepth) return nullptr;
    r.skip_ws();
    if (r.pos >= r.len) return nullptr;

    char c = r.data[r.pos];
    if (c == '{') { r.pos++; return read_object(r, depth); }
    if (c == '[') { r.pos++; return read_array(r, depth); }
    if (cc(c) & CC_QUOTE) {
        std::string s;
        if (!read_quoted_string(r, s, true)) return nullptr;
        return DataNode::MakeString(s.c_str(), s.size());
    }
    return read_unquoted_value(r);
}

} // anonymous namespace

// ===== Public API ==========================================================

DataNode* HjsonParse(const char* data, size_t len) {
    if (!data || len == 0) return nullptr;

    // Thread-safe one-time init (C++11 guarantees static local init is safe)
    static const bool inited = [] {
        init_char_table();
        init_scanner();
        return true;
    }();
    (void)inited;

    // Padded copy — allows SIMD to safely overread past the end
    auto padded = std::unique_ptr<char[]>(new char[len + kPadding]);
    memcpy(padded.get(), data, len);
    memset(padded.get() + len, 0, kPadding);

    Reader r{padded.get(), len, 0};
    r.skip_ws();
    if (r.pos >= r.len) return nullptr;

    char c = r.data[r.pos];

    if (c == '[') {
        // Array — parse directly
        DataNode* result = read_value(r, 0);
        if (!result) return nullptr;
        r.skip_ws();
        if (r.pos < r.len) { DataNode::Destroy(result); return nullptr; }
        return result;
    }

    if (c != '{') {
        // Not { or [ — try as braceless root object first, fall back to single value.
        // This single-pass approach avoids re-parsing the first key.
        DataNode* result = read_object(r, 0, false);
        if (result) {
            r.skip_ws();
            if (r.pos >= r.len) return result;
            DataNode::Destroy(result);
        }
        // Fall back to single value
        r.pos = 0;
        r.skip_ws();
        DataNode* val = read_value(r, 0);
        if (val) {
            r.skip_ws();
            if (r.pos >= r.len) return val;
            DataNode::Destroy(val);
        }
        return nullptr;
    }

    // Braced object — parse directly
    DataNode* result = read_value(r, 0);
    if (!result) return nullptr;
    r.skip_ws();
    if (r.pos < r.len) { DataNode::Destroy(result); return nullptr; }
    return result;
}
