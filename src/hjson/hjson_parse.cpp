#include "hjson_parse.h"
#include "data_node.h"
#include <cstring>
#include <string>
#include <cstdlib>
#include <cmath>
#include <cerrno>

static constexpr int kMaxDepth = 256;

namespace {

struct Reader {
    const char* data;
    size_t len;
    size_t pos;

    bool eof() const { return pos >= len; }
    char peek() const { return eof() ? '\0' : data[pos]; }
    char next() { return eof() ? '\0' : data[pos++]; }

    char peek_at(size_t offset) const {
        size_t p = pos + offset;
        return p < len ? data[p] : '\0';
    }

    // Skip whitespace and comments (// /* */ #)
    void skip_ws() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                pos++;
                continue;
            }
            if (c == '#') {
                while (!eof() && peek() != '\n') pos++;
                if (!eof()) pos++;
                continue;
            }
            if (c == '/' && pos + 1 < len) {
                if (data[pos + 1] == '/') {
                    pos += 2;
                    while (!eof() && peek() != '\n') pos++;
                    if (!eof()) pos++;
                    continue;
                }
                if (data[pos + 1] == '*') {
                    pos += 2;
                    while (!eof()) {
                        if (peek() == '*' && pos + 1 < len && data[pos + 1] == '/') {
                            pos += 2;
                            break;
                        }
                        pos++;
                    }
                    continue;
                }
            }
            break;
        }
    }

    // Skip whitespace, comments, and at most one comma (as element separator)
    void skip_separator() {
        skip_ws();
        if (!eof() && peek() == ',') {
            pos++;
            skip_ws();
        }
    }
};

static DataNode* read_value(Reader& r, int depth);

static bool is_punctuator(char c) {
    return c == '{' || c == '}' || c == '[' || c == ']' || c == ',' || c == ':';
}

// UTF-8 encode a code point into a string
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

// Parse \uXXXX hex escape
static bool parse_u_escape(Reader& r, uint32_t& cp) {
    if (r.pos + 4 > r.len) return false;
    cp = 0;
    for (int i = 0; i < 4; i++) {
        char h = r.data[r.pos + i];
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

// Read a quoted string (double or single quotes).
// allowML: true for value context (allows '''), false for key context.
static bool read_quoted_string(Reader& r, std::string& out, bool allowML = true) {
    char quote = r.peek();
    if (quote != '"' && quote != '\'') return false;
    r.next(); // skip opening quote

    // Check for ''' multiline or '' empty
    if (quote == '\'') {
        if (allowML && r.peek() == '\'' && r.peek_at(1) == '\'') {
            r.pos += 2; // skip the two extra '
            goto multiline;
        }
        if (r.peek() == '\'') {
            r.next();
            out.clear();
            return true;
        }
    }

    out.clear();
    while (!r.eof()) {
        char c = r.next();
        if (c == quote) return true;
        if (c == '\n' || c == '\r') return false;
        if (c == '\\') {
            if (r.eof()) return false;
            char esc = r.next();
            switch (esc) {
                case '"': out += '"'; break;
                case '\'': out += '\''; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!parse_u_escape(r, cp)) return false;
                    utf8_encode(out, cp);
                    break;
                }
                default: return false;
            }
        } else {
            out += c;
        }
    }
    return false;

multiline:
    {
        // Measure indent before the opening '''
        size_t tripleStart = r.pos - 3;
        int indent = 0;
        for (size_t i = tripleStart; i > 0; i--) {
            char ch = r.data[i - 1];
            if (ch == '\n' || ch == '\0') break;
            indent++;
        }

        auto skipIndent = [&]() {
            int skip = indent;
            while (!r.eof() && r.peek() > 0 && r.peek() <= ' ' && r.peek() != '\n' && skip > 0) {
                skip--;
                r.next();
            }
        };

        // Skip whitespace (not newline) after opening '''
        while (!r.eof() && r.peek() > 0 && r.peek() <= ' ' && r.peek() != '\n')
            r.next();
        if (!r.eof() && r.peek() == '\n') {
            r.next();
            skipIndent();
        }

        out.clear();
        int triple = 0;
        bool lastLf = false;

        while (!r.eof()) {
            char c = r.peek();
            if (c == '\'') {
                triple++;
                r.next();
                if (triple == 3) {
                    if (lastLf && !out.empty() && out.back() == '\n')
                        out.pop_back();
                    return true;
                }
                continue;
            }
            while (triple > 0) {
                out += '\'';
                triple--;
                lastLf = false;
            }
            if (c == '\n') {
                out += '\n';
                lastLf = true;
                r.next();
                skipIndent();
            } else if (c == '\r') {
                r.next(); // strip \r
            } else {
                out += c;
                lastLf = false;
                r.next();
            }
        }
        return false;
    }
}

// Read an unquoted key. Rejects whitespace within the key name.
static bool read_unquoted_key(Reader& r, std::string& out) {
    size_t keyStart = r.pos;
    size_t keyEnd = keyStart;
    int firstSpace = -1;

    while (!r.eof()) {
        char c = r.peek();
        if (c == ':') {
            if (keyEnd <= keyStart) return false; // empty key
            if (firstSpace >= 0 && static_cast<size_t>(firstSpace) != keyEnd)
                return false; // whitespace within key
            out.assign(r.data + keyStart, keyEnd - keyStart);
            return true;
        }
        if (c <= ' ') {
            if (c == 0) return false;
            if (firstSpace < 0) firstSpace = static_cast<int>(r.pos);
            r.next();
        } else {
            if (is_punctuator(c)) return false;
            r.next();
            keyEnd = r.pos;
        }
    }
    return false;
}

// Read a key (quoted or unquoted)
static bool read_key(Reader& r, std::string& out) {
    char c = r.peek();
    if (c == '"' || c == '\'') {
        if (c == '\'' && r.peek_at(1) == '\'' && r.peek_at(2) == '\'')
            return false; // ''' not allowed as key
        return read_quoted_string(r, out, false);
    }
    return read_unquoted_key(r, out);
}

// Validate and parse a number string following HJSON/JSON rules.
// Rejects leading zeros, NaN, Infinity.
static DataNode* try_parse_number(const char* start, size_t len) {
    if (len == 0) return nullptr;

    const char* p = start;
    const char* pend = start + len;

    if (*p == '-') {
        p++;
        if (p == pend) return nullptr;
    }
    if (*p < '0' || *p > '9') return nullptr;

    // Leading zeros: "0" is ok, "0." and "0e" ok, but "00"/"01" not
    if (*p == '0' && p + 1 < pend && p[1] >= '0' && p[1] <= '9')
        return nullptr;

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

    // Integer overflow — parse as double
    char* dend;
    double dv = strtod(start, &dend);
    if (dend == pend && !std::isinf(dv) && !std::isnan(dv))
        return DataNode::MakeFloat(dv);

    return nullptr;
}

// Try to parse a trimmed unquoted value as keyword or number.
static DataNode* try_parse_keyword_or_number(const char* start, size_t len) {
    if (len == 0) return nullptr;
    if (len == 4 && memcmp(start, "true", 4) == 0) return DataNode::MakeBool(true);
    if (len == 5 && memcmp(start, "false", 5) == 0) return DataNode::MakeBool(false);
    if (len == 4 && memcmp(start, "null", 4) == 0) return DataNode::MakeNull();

    if (*start == '-' || (*start >= '0' && *start <= '9'))
        return try_parse_number(start, len);

    return nullptr;
}

// Check if position is at a comment start
static bool is_comment_start(const Reader& r) {
    if (r.eof()) return false;
    char c = r.peek();
    if (c == '#') return true;
    if (c == '/' && r.pos + 1 < r.len)
        return r.data[r.pos + 1] == '/' || r.data[r.pos + 1] == '*';
    return false;
}

// Read an unquoted value following HJSON spec:
// - Reads until EOL/EOF
// - At break points (# // /* , } ]), checks if value so far is keyword/number
// - If yes, returns keyword/number (break char left for caller)
// - If no, consumes break char as part of the string
// - At EOL/EOF, returns trimmed result as string (or keyword/number)
static DataNode* read_unquoted_value(Reader& r) {
    char first = r.peek();
    if (is_punctuator(first)) return nullptr;

    size_t valStart = r.pos;
    size_t valEnd = valStart;

    // Skip leading whitespace in valStart
    if (first <= ' ' && first > 0) {
        valStart = r.pos + 1;
    }

    for (;;) {
        if (r.eof()) break;
        char c = r.peek();
        bool isEol = (c == '\r' || c == '\n');
        if (isEol) break;

        bool isBreak = (c == ',' || c == '}' || c == ']' || is_comment_start(r));

        if (isBreak) {
            size_t valLen = valEnd - valStart;
            DataNode* kw = try_parse_keyword_or_number(r.data + valStart, valLen);
            if (kw) return kw;
            // Not keyword/number — consume break char as part of string
        }

        r.next();
        if (static_cast<unsigned char>(c) > ' ') {
            valEnd = r.pos;
        }
    }

    size_t valLen = valEnd - valStart;
    if (valLen == 0) return nullptr;

    DataNode* kw = try_parse_keyword_or_number(r.data + valStart, valLen);
    if (kw) return kw;

    return DataNode::MakeString(r.data + valStart, valLen);
}

// braced: true when called after '{', false for root-level braceless objects
static DataNode* read_object(Reader& r, int depth, bool braced = true) {
    auto* obj = DataNode::MakeObject();

    r.skip_ws();
    while (!r.eof() && !(braced && r.peek() == '}')) {
        if (!braced && r.eof()) break;

        std::string key;
        if (!read_key(r, key)) {
            DataNode::Destroy(obj);
            return nullptr;
        }

        r.skip_ws();
        if (r.peek() != ':') {
            DataNode::Destroy(obj);
            return nullptr;
        }
        r.next();
        r.skip_ws();

        DataNode* val = read_value(r, depth + 1);
        if (!val) {
            DataNode::Destroy(obj);
            return nullptr;
        }

        obj->ObjInsert(key, val);
        r.skip_separator();
    }

    if (braced) {
        if (r.eof() || r.peek() != '}') {
            DataNode::Destroy(obj);
            return nullptr;
        }
        r.next();
    }
    return obj;
}

static DataNode* read_array(Reader& r, int depth) {
    auto* arr = DataNode::MakeArray();

    r.skip_ws();
    while (!r.eof() && r.peek() != ']') {
        DataNode* val = read_value(r, depth + 1);
        if (!val) {
            DataNode::Destroy(arr);
            return nullptr;
        }
        arr->arr.push_back(val);
        r.skip_separator();
    }

    if (r.eof() || r.peek() != ']') {
        DataNode::Destroy(arr);
        return nullptr;
    }
    r.next();
    return arr;
}

static DataNode* read_value(Reader& r, int depth) {
    if (depth > kMaxDepth) return nullptr;

    r.skip_ws();
    if (r.eof()) return nullptr;

    char c = r.peek();

    if (c == '{') { r.next(); return read_object(r, depth); }
    if (c == '[') { r.next(); return read_array(r, depth); }
    if (c == '"' || c == '\'') {
        std::string s;
        if (!read_quoted_string(r, s, true)) return nullptr;
        return DataNode::MakeString(s.c_str(), s.size());
    }

    return read_unquoted_value(r);
}

// Check if the top-level content is a root-level object without braces
static bool is_root_object(const char* data, size_t len) {
    Reader r{data, len, 0};
    r.skip_ws();
    if (r.eof()) return false;

    char c = r.peek();
    if (c == '{' || c == '[') return false;

    std::string key;
    size_t saved = r.pos;
    if (c == '"' || c == '\'') {
        if (c == '\'' && r.peek_at(1) == '\'' && r.peek_at(2) == '\'')
            return false;
        if (!read_quoted_string(r, key, false)) {
            r.pos = saved;
            return false;
        }
    } else {
        if (!read_unquoted_key(r, key)) {
            r.pos = saved;
            return false;
        }
    }
    r.skip_ws();
    return !r.eof() && r.peek() == ':';
}

} // anonymous namespace

DataNode* HjsonParse(const char* data, size_t len) {
    if (!data || len == 0) return nullptr;

    Reader r{data, len, 0};

    if (is_root_object(data, len)) {
        r.skip_ws();
        DataNode* result = read_object(r, 0, false);
        if (result) {
            r.skip_ws();
            if (r.eof()) return result;
            DataNode::Destroy(result);
        }
        r.pos = 0;
    }

    r.skip_ws();
    if (r.eof()) return nullptr;

    char c = r.peek();
    if (c != '{' && c != '[') {
        // Try as single value
        DataNode* result = read_value(r, 0);
        if (result) {
            r.skip_ws();
            if (r.eof()) return result;
            DataNode::Destroy(result);
        }
        return nullptr;
    }

    DataNode* result = read_value(r, 0);
    if (!result) return nullptr;

    r.skip_ws();
    if (!r.eof()) {
        DataNode::Destroy(result);
        return nullptr;
    }

    return result;
}
