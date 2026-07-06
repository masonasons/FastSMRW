#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "fastsm/net/http_client.hpp"

namespace fastsm::net {

// IHttpClient implementation over NSURLSession (TLS handled natively by the
// system). Synchronous send(); call only from the core's worker thread. The
// analogue of WinHttpClient for Apple platforms (macOS/iOS). A single instance
// is reusable across requests and threads.
class DarwinHttpClient : public IHttpClient {
public:
    explicit DarwinHttpClient(std::string user_agent = "FastSMRW/0.0.1");
    ~DarwinHttpClient() override;

    HttpResponse send(const HttpRequest& request) override;
    void send_stream(const HttpRequest& request, const std::function<bool()>& should_continue,
                     const std::function<void(std::string_view)>& on_chunk) override;

    // Cancels any in-progress streaming reads so a blocked send_stream returns
    // promptly. Safe to call from another thread.
    void cancel_streams() override;

private:
    std::string user_agent_;
    std::mutex stream_mutex_;
    // Opaque NSURLSessionDataTask* handles of live streams (retained by the
    // owning session inside send_stream); we only cancel them here.
    std::vector<void*> active_streams_;
};

} // namespace fastsm::net
