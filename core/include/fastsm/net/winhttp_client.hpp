#pragma once

#include <string>

#include "fastsm/net/http_client.hpp"

namespace fastsm::net {

// IHttpClient implementation over WinHTTP (TLS handled natively). Synchronous;
// call only from the core's worker thread. A single instance is reusable across
// requests and is thread-safe to the extent WinHTTP handles are created
// per-call.
class WinHttpClient : public IHttpClient {
public:
    explicit WinHttpClient(std::string user_agent = "FastSMRW/0.0.1");

    HttpResponse send(const HttpRequest& request) override;

private:
    std::wstring user_agent_;
};

} // namespace fastsm::net
