#pragma once

#include <mutex>
#include <string>
#include <vector>

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
    void send_stream(const HttpRequest& request, const std::function<bool()>& should_continue,
                     const std::function<void(std::string_view)>& on_chunk) override;

    // Aborts any in-progress streaming reads (closes their request handles) so a
    // blocked send_stream returns promptly. Safe to call from another thread.
    void cancel_streams();

private:
    std::wstring user_agent_;
    std::mutex stream_mutex_;
    std::vector<void*> active_streams_; // HINTERNET request handles being read
};

} // namespace fastsm::net
