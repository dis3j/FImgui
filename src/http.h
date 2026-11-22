#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace http
{
    struct Result
    {
        int status = 0;
        std::string errorBody;   // body of non-2xx responses
        std::string error;       // transport error
    };

    using Headers = std::vector<std::pair<std::string, std::string>>;

    // Simple GET; the response body (any status) goes to *body.
    Result Get(const std::string& url, const Headers& headers, std::string* body);

    // POSTs 'body' and streams the 2xx response body into onData (return false to abort).
    Result PostStream(const std::string& url, const Headers& headers, const std::string& body,
                      const std::function<bool(const char* data, size_t size)>& onData, std::atomic<bool>* cancel);
}
