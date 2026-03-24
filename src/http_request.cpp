#include "http_request.h"
#include "compression.h"
#include "handle_manager.h"
#include "extension.h"
#include "data/data_handle.h"
#include "msgpack/msgpack_serialize.h"
#include "msgpack/msgpack_parse.h"
#include <cstring>
#include <vector>

extern HandleManager g_handle_manager;

// Curl handle pool — game-thread only.
// Keeps max(outstanding, kMinPool) idle handles: the floor survives bursts,
// outstanding tracking sheds excess when concurrency truly drops.
static constexpr size_t kMinPool = 16;
static std::vector<CURL*> g_curl_pool;
static size_t g_curl_outstanding = 0;

CURL* HttpRequest::AcquireCurl() {
    g_curl_outstanding++;
    if (!g_curl_pool.empty()) {
        CURL* c = g_curl_pool.back();
        g_curl_pool.pop_back();
        return c;
    }
    return curl_easy_init();
}

void HttpRequest::ReleaseCurl(CURL* c) {
    g_curl_outstanding--;
    curl_easy_reset(c);
    size_t cap = g_curl_outstanding > kMinPool ? g_curl_outstanding : kMinPool;
    if (g_curl_pool.size() < cap) {
        g_curl_pool.push_back(c);
    } else {
        curl_easy_cleanup(c);
    }
}

void HttpRequest::DrainCurlPool() {
    for (CURL* c : g_curl_pool)
        curl_easy_cleanup(c);
    g_curl_pool.clear();
}
IPluginFunction* GetSourcepawnFunction(IPluginContext* context, cell_t id);

HttpRequest::HttpRequest(CURL* c, IPluginContext* plugin) {
    curl = c;
    curlcode = static_cast<CURLcode>(-1);
    compress_body = false;
    plugin_context = plugin;
    callback_id = 0;
    handle_id = 0;
    userdata = 0;
    client_index = 0;
    in_event_thread = false;
    handle_closed = false;
    completed = false;
    curl_error_message[0] = 0;
    max_retries = 0;
    retry_count = 0;
    retry_delay_ms = 1000;
    retry_backoff = 2.0f;
    retry_max_delay_ms = 30000;
    in_retry_wait = false;
    log_retries = false;
    log_caller_line = 0;
    body_node = nullptr;
    body_format = BodyFormat::NONE;
    response_node = nullptr;
    parse_mode = 0;
}

HttpRequest::~HttpRequest() {
    if (curl) {
        ReleaseCurl(curl);
        curl = nullptr;
    }
    if (built_headers_)
        curl_slist_free_all(built_headers_);
    if (body_node)
        DataNode::Decref(body_node);
    if (response_node)
        DataNode::Decref(response_node);
}

void HttpRequest::SetHeader(const char* key, const char* value) {
    std::lock_guard<std::mutex> lock(headers_mutex_);
    headers_[key] = value;
}

void HttpRequest::RemoveHeader(const char* key) {
    std::lock_guard<std::mutex> lock(headers_mutex_);
    headers_.erase(key);
}

void HttpRequest::ClearHeaders() {
    std::lock_guard<std::mutex> lock(headers_mutex_);
    headers_.clear();
}

void HttpRequest::SetBody(const char* data, size_t length) {
    if (body_node) {
        DataNode::Decref(body_node);
        body_node = nullptr;
        body_format = BodyFormat::NONE;
    }
    post_body.assign(data, length);
}

void HttpRequest::SetBodyString(const char* str) {
    size_t len = strlen(str);
    SetBody(str, len);
}

void HttpRequest::SetBodyNode(DataNode* node, BodyFormat format) {
    post_body.clear();
    if (body_node)
        DataNode::Decref(body_node);
    body_node = node;
    body_format = format;
}

void HttpRequest::PrepareForSend() {
    in_event_thread = true;
}

void HttpRequest::SetupCurl() {
    // Serialize body_node on event thread
    if (body_node) {
        if (body_format == BodyFormat::JSON) {
            post_body = DataSerializeJson(*body_node, false);
        } else if (body_format == BodyFormat::MSGPACK) {
            auto buf = MsgPackSerialize(*body_node);
            post_body.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
        }
        DataNode::Decref(body_node);
        body_node = nullptr;
    }

    if (!post_body.empty() && compress_body) {
        std::vector<char> compressed;
        if (CompressDeflate(post_body.data(), post_body.size(), compressed)) {
            post_body.assign(compressed.data(), compressed.size());
            auto_headers_["Content-Encoding"] = "deflate";
        }
    }

    if (!post_body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(post_body.size()));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body.data());
        // Suppress Expect: 100-continue. libcurl sends it by default for POST
        // bodies, but nginx/cloudflare proxies often don't respond with 100
        // Continue, causing the transfer to stall until the 1-second timeout.
        auto_headers_["Expect"] = "";
    }

    BuildHeaderSlist(headers_, headers_mutex_, built_headers_, &auto_headers_);
}

void HttpRequest::OnCompleted() {
    // Don't free post_body if we might retry — it's still needed by curl
    if (in_retry_wait)
        return;
    std::string().swap(post_body);
}

bool HttpRequest::ShouldRetry() const {
    if (max_retries == 0 || (max_retries > 0 && retry_count >= max_retries))
        return false;
    if (handle_closed)
        return false;

    // Transport errors are always retryable
    if (curlcode != CURLE_OK)
        return true;

    // Check HTTP status code
    long httpcode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode);

    // 429 Too Many Requests
    if (httpcode == 429)
        return true;

    // 5xx Server Error
    if (httpcode >= 500 && httpcode < 600)
        return true;

    return false;
}

void HttpRequest::PrepareForRetry() {
    retry_count++;
    response_body.clear();
    response_headers.clear();
    curl_error_message[0] = 0;
    curlcode = static_cast<CURLcode>(-1);
    completed = false;
    in_retry_wait = true;
    if (response_node) {
        DataNode::Decref(response_node);
        response_node = nullptr;
    }
}

void HttpRequest::OnCompletedGameThread() {
    in_event_thread = false;

    IPluginFunction* func = GetSourcepawnFunction(plugin_context, callback_id);
    if (!func) {
        UntrackClientHandle(client_index, handle_id);
        g_handle_manager.FreeHandle(handle_id);
        return;
    }

    long httpresult = 0;
    long size = 0;

    if (handle_closed) {
        // User called Close() while in-flight — fire callback with CURLE_ABORTED_BY_CALLBACK
        curlcode = CURLE_ABORTED_BY_CALLBACK;
    } else if (curlcode == 0) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpresult);
        size = response_body.size();
    }

    if (parse_mode > 0 && !handle_closed) {
        // Simplified callback: push Json handle instead of size
        DataNode* node = response_node;
        response_node = nullptr;

        // Fallback: event-thread parse failed, try game thread
        if (!node && curlcode == CURLE_OK && !response_body.empty()) {
            if (parse_mode == 1)
                node = DataParseJson(response_body.data(), response_body.size());
            else if (parse_mode == 2)
                node = MsgPackParse(
                    reinterpret_cast<const uint8_t*>(response_body.data()),
                    response_body.size());
        }

        int json_handle = 0;
        if (node) {
            DataHandle* dh = new DataHandle(node);
            json_handle = g_handle_manager.CreateHandle(
                static_cast<void*>(dh), HANDLE_JSON_VALUE, plugin_context);
            if (json_handle == 0) delete dh;
        }

        func->PushCell(handle_id);
        func->PushCell(curlcode);
        func->PushCell(httpresult);
        func->PushCell(json_handle);
        func->PushCell(userdata);
        func->Execute(nullptr);
    } else {
        // Raw callback: existing behavior
        func->PushCell(handle_id);
        func->PushCell(curlcode);
        func->PushCell(httpresult);
        func->PushCell(size);
        func->PushCell(userdata);
        func->Execute(nullptr);
    }

    // Auto-close after callback — FreeHandle deletes this object
    UntrackClientHandle(client_index, handle_id);
    g_handle_manager.FreeHandle(handle_id);
}
