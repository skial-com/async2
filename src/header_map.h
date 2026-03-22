#ifndef ASYNC2_HEADER_MAP_H
#define ASYNC2_HEADER_MAP_H

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <curl/curl.h>

// Strip CR and LF characters from a string to prevent header injection.
inline std::string StripCRLF(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c != '\r' && c != '\n')
            result += c;
    }
    return result;
}

// Case-insensitive comparator for HTTP header keys (RFC 7230)
struct HeaderCaseInsensitive {
    bool operator()(const std::string& a, const std::string& b) const {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](unsigned char ca, unsigned char cb) { return std::tolower(ca) < std::tolower(cb); });
    }
};

using HeaderMap = std::map<std::string, std::string, HeaderCaseInsensitive>;

// Rebuild a curl_slist from a header map. Frees the old slist, locks the mutex,
// and builds a new one. If defaults is provided, its entries are appended only
// for keys not already present in headers (user headers take priority).
// Call on the event thread only.
inline void BuildHeaderSlist(HeaderMap& headers, std::mutex& mutex, curl_slist*& built,
                             const HeaderMap* defaults = nullptr) {
    if (built) {
        curl_slist_free_all(built);
        built = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& [key, value] : headers) {
        std::string header = StripCRLF(key) + ": " + StripCRLF(value);
        built = curl_slist_append(built, header.c_str());
    }
    if (defaults) {
        for (const auto& [key, value] : *defaults) {
            if (headers.find(key) == headers.end()) {
                std::string header = StripCRLF(key) + ": " + StripCRLF(value);
                built = curl_slist_append(built, header.c_str());
            }
        }
    }
}

#endif
