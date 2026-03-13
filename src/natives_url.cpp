#include <cstring>
#include <string>
#include <string_view>

#include <curl/curl.h>

#include "smsdk_ext.h"
#include "data/data_handle.h"
#include "natives.h"

// async2_UrlEncode(const char[] input, char[] output, int maxlen) -> int
static cell_t Native_UrlEncode(IPluginContext* pContext, const cell_t* params) {
    char* input;
    pContext->LocalToString(params[1], &input);

    char* output;
    pContext->LocalToString(params[2], &output);
    int maxlen = params[3];

    char* encoded = curl_easy_escape(nullptr, input, 0);
    if (!encoded)
        return 0;

    int len = static_cast<int>(strlen(encoded));
    if (len >= maxlen) {
        curl_free(encoded);
        return -1;
    }

    memcpy(output, encoded, len + 1);
    curl_free(encoded);
    return len;
}

// async2_UrlDecode(const char[] input, char[] output, int maxlen) -> int
static cell_t Native_UrlDecode(IPluginContext* pContext, const cell_t* params) {
    char* input;
    pContext->LocalToString(params[1], &input);

    char* output;
    pContext->LocalToString(params[2], &output);
    int maxlen = params[3];

    int decoded_len = 0;
    char* decoded = curl_easy_unescape(nullptr, input, 0, &decoded_len);
    if (!decoded)
        return 0;

    if (decoded_len >= maxlen) {
        curl_free(decoded);
        return -1;
    }

    memcpy(output, decoded, decoded_len);
    output[decoded_len] = '\0';
    curl_free(decoded);
    return decoded_len;
}

// async2_QueryStringParse(const char[] queryString) -> Json
static cell_t Native_QueryStringParse(IPluginContext* pContext, const cell_t* params) {
    char* input;
    pContext->LocalToString(params[1], &input);

    DataHandle* json = DataHandle::CreateObject();
    if (!json)
        return 0;

    // Skip leading '?' if present
    if (*input == '?')
        input++;

    // Parse "key=value&key2=value2&..."
    std::string_view qs(input);
    size_t pos = 0;

    while (pos < qs.size()) {
        size_t amp = qs.find('&', pos);
        if (amp == std::string_view::npos)
            amp = qs.size();

        std::string_view pair = qs.substr(pos, amp - pos);
        pos = amp + 1;

        if (pair.empty())
            continue;

        // Split on first '='
        std::string_view raw_key, raw_val;
        size_t eq = pair.find('=');
        if (eq == std::string_view::npos) {
            raw_key = pair;
        } else {
            raw_key = pair.substr(0, eq);
            raw_val = pair.substr(eq + 1);
        }

        if (raw_key.empty())
            continue;

        // Strip trailing "[]" from key (PHP-style array notation)
        if (raw_key.size() >= 2 && raw_key.substr(raw_key.size() - 2) == "[]")
            raw_key.remove_suffix(2);

        // URL-decode key and value
        int key_len = 0;
        char* dec_key = curl_easy_unescape(nullptr, raw_key.data(), static_cast<int>(raw_key.size()), &key_len);
        if (!dec_key) continue;

        int val_len = 0;
        char* dec_val = curl_easy_unescape(nullptr, raw_val.data(), static_cast<int>(raw_val.size()), &val_len);
        if (!dec_val) { curl_free(dec_key); continue; }

        DataNode* existing = json->node->ObjFind(dec_key);
        if (existing) {
            if (existing->type == DataType::Array) {
                // Already an array — append
                existing->arr.push_back(DataNode::MakeString(dec_val));
            } else {
                // Promote to array: steal existing node, avoid DeepCopy.
                // Null the map entry so ObjInsert's Destroy() is a no-op.
                auto it = json->node->obj.find(dec_key);
                DataNode* old_node = it->second;
                it.value() = nullptr;

                DataNode* arr = DataNode::MakeArray();
                arr->arr.push_back(old_node);
                arr->arr.push_back(DataNode::MakeString(dec_val));
                json->node->ObjInsert(dec_key, arr);
            }
        } else {
            json->SetString(dec_key, dec_val);
        }

        curl_free(dec_key);
        curl_free(dec_val);
    }

    int handle = g_handle_manager.CreateHandle(static_cast<void*>(json), HANDLE_JSON_VALUE);
    if (handle == 0) { delete json; return 0; }
    return handle;
}

static std::string NodeToString(const DataNode* val) {
    if (!val) return {};
    switch (val->type) {
        case DataType::String: return val->str_val;
        case DataType::Int:    return std::to_string(val->int_val);
        case DataType::Float: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", val->float_val);
            return buf;
        }
        case DataType::Bool:   return val->bool_val ? "true" : "false";
        default:               return {};
    }
}

static void AppendEncoded(std::string& result, const char* enc_key, const DataNode* val) {
    if (!result.empty())
        result += '&';
    result += enc_key;
    result += '=';

    std::string str_val = NodeToString(val);
    char* enc_val = curl_easy_escape(nullptr, str_val.c_str(), static_cast<int>(str_val.size()));
    if (enc_val) {
        result += enc_val;
        curl_free(enc_val);
    }
}

// async2_QueryStringBuild(Json jsonHandle, char[] output, int maxlen) -> int
static cell_t Native_QueryStringBuild(IPluginContext* pContext, const cell_t* params) {
    DataHandle* json = g_handle_manager.GetDataHandle(params[1]);
    if (!json || json->node->type != DataType::Object)
        return -1;

    char* output;
    pContext->LocalToString(params[2], &output);
    int maxlen = params[3];
    if (maxlen <= 0) return -1;

    std::string result;

    for (const auto& [key, val] : json->node->obj) {
        char* enc_key = curl_easy_escape(nullptr, key.c_str(), static_cast<int>(key.size()));
        if (!enc_key) continue;

        if (val && val->type == DataType::Array) {
            for (const auto* elem : val->arr)
                AppendEncoded(result, enc_key, elem);
        } else {
            AppendEncoded(result, enc_key, val);
        }

        curl_free(enc_key);
    }

    size_t len = result.size();
    if (len >= static_cast<size_t>(maxlen))
        return -1;

    memcpy(output, result.c_str(), len + 1);
    return static_cast<cell_t>(len);
}

sp_nativeinfo_t g_UrlNatives[] = {
    {"async2_UrlEncode",         Native_UrlEncode},
    {"async2_UrlDecode",         Native_UrlDecode},
    {"async2_QueryStringParse",  Native_QueryStringParse},
    {"async2_QueryStringBuild",  Native_QueryStringBuild},
    {nullptr,                    nullptr},
};
