#ifndef ASYNC2_HTTP_REQUEST_H
#define ASYNC2_HTTP_REQUEST_H

#include <atomic>
#include <vector>
#include <string>
#include <unordered_map>
#include <curl/curl.h>
#include "header_map.h"

#include "smsdk_ext.h"
#include "data/data_node.h"

class HttpRequest {
public:
    CURL* curl;
    CURLcode curlcode;

    // Request headers (game thread writes, event thread reads via BuildHeaders)
    HeaderMap headers_;
    std::mutex headers_mutex_;
    curl_slist* built_headers_ = nullptr;  // event thread only
    HeaderMap auto_headers_;  // event thread only (Content-Encoding, Expect)

    std::unordered_map<std::string, std::string> response_headers;
    std::vector<char> response_body;
    std::string method;
    std::string post_body;
    bool compress_body;
    IPluginContext* plugin_context;
    funcid_t callback_id;
    int handle_id;
    int userdata;
    int client_index;
    std::atomic<bool> in_event_thread;
    std::atomic<bool> handle_closed;
    std::atomic<bool> completed;
    char curl_error_message[CURL_ERROR_SIZE];

    // Retry settings
    int max_retries;
    int retry_count;
    int retry_delay_ms;
    float retry_backoff;
    int retry_max_delay_ms;
    bool in_retry_wait;  // true while waiting on retry timer (not in curl_multi)

    // Retry logging (opt-in via SetRetry)
    bool log_retries;
    std::string log_caller_file;
    int log_caller_line;

    // Response auto-parse (0=raw, 1=JSON, 2=MsgPack)
    DataNode* response_node;
    int parse_mode;

    HttpRequest(CURL* c, IPluginContext* plugin);
    ~HttpRequest();

    void SetHeader(const char* key, const char* value);
    void RemoveHeader(const char* key);
    void ClearHeaders();
    void SetBody(const char* data, size_t length);
    void SetBodyString(const char* str);
    void PrepareForSend();
    void SetupCurl();
    void OnCompleted();
    void OnCompletedGameThread();
    bool ShouldRetry() const;
    void PrepareForRetry();

    // Curl handle pool — reuses handles to avoid cross-thread alloc/free
    // fragmentation from curl's internal state being allocated on the event
    // thread and freed on the game thread. Game-thread only, no locking.
    static CURL* AcquireCurl();
    static void ReleaseCurl(CURL* c);
    static void DrainCurlPool();
};

#endif
