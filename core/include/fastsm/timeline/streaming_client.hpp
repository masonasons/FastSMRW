#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "fastsm/net/http_client.hpp"
#include "fastsm/platform/social_account.hpp"
#include "fastsm/runtime/main_executor.hpp"

// Real-time streaming for an account (Mastodon SSE user stream). Runs the
// long-lived connection on its own thread, parses events, and delivers each
// parsed item to the UI on the main thread. Reconnects after drops.

namespace fastsm {

class StreamingClient {
public:
    StreamingClient(net::IHttpClient* http, runtime::IMainExecutor* main);
    ~StreamingClient();
    StreamingClient(const StreamingClient&) = delete;
    StreamingClient& operator=(const StreamingClient&) = delete;

    // (Re)start the SSE connection `spec` for `account`; `on_item` runs on the
    // main thread once per parsed event. `route` is the source whose "update"
    // events this stream carries (home/local/a hashtag/...). The account must
    // outlive the client (the owner stops before destroying it).
    void start(SocialAccount* account, const StreamRequest& spec, TimelineSource route,
               std::function<void(StreamItem)> on_item);

    // Stop and join the connection thread (aborts a blocked read promptly).
    void stop();

private:
    net::IHttpClient* http_;
    runtime::IMainExecutor* main_;
    std::thread thread_;
    std::shared_ptr<std::atomic<bool>> running_;
    std::string account_key_; // for diagnostic logging
};

} // namespace fastsm
