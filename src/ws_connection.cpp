#include "ws_connection.h"
#include "data/data_node.h"

WsOp::~WsOp() {
    if (body_node) DataNode::Destroy(body_node);
}

WsConnection::WsConnection(IPluginContext* ctx, int ud)
    : plugin_context(ctx), userdata(ud) {}

void WsConnection::SetHeader(const char* key, const char* value) {
    std::lock_guard<std::mutex> lock(headers_mutex_);
    headers_[key] = value;
}

void WsConnection::RemoveHeader(const char* key) {
    std::lock_guard<std::mutex> lock(headers_mutex_);
    headers_.erase(key);
}

void WsConnection::ClearHeaders() {
    std::lock_guard<std::mutex> lock(headers_mutex_);
    headers_.clear();
}

WsConnection::~WsConnection() {
    if (built_headers_) {
        curl_slist_free_all(built_headers_);
        built_headers_ = nullptr;
    }
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = nullptr;
    }
}
