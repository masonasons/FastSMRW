#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Networking abstraction. The core issues HTTP through IHttpClient; the Windows
// build provides a WinHttpClient implementation. Keeping this an interface lets
// tests inject a fake transport and lets a future non-Windows front end supply
// its own implementation.

namespace fastsm::net {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct HttpRequest {
    std::string method = "GET"; // "GET", "POST", "PUT", "DELETE", ...
    std::string url;
    Headers headers;
    std::string body; // raw request body (already encoded for its content-type)
};

struct HttpResponse {
    long status = 0;     // HTTP status code; 0 if the request never completed
    std::string body;    // response body
    Headers headers;     // response headers
    std::string error;   // non-empty describes a transport-level failure

    bool ok() const { return error.empty() && status >= 200 && status < 300; }

    // Case-insensitive lookup of the first matching response header.
    std::optional<std::string> header(std::string_view name) const;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    // Performs the request synchronously. Implementations must be callable from
    // the core's worker thread (never the UI thread).
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

} // namespace fastsm::net
