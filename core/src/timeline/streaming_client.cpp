#include "fastsm/timeline/streaming_client.hpp"

#include <chrono>
#include <string_view>
#include <utility>

#include "fastsm/net/sse_parser.hpp"
#include "fastsm/util/log.hpp"

namespace fastsm {

StreamingClient::StreamingClient(net::IHttpClient* http, runtime::IMainExecutor* main)
    : http_(http), main_(main) {}

StreamingClient::~StreamingClient() { stop(); }

void StreamingClient::start(SocialAccount* account, std::function<void(StreamItem)> on_item) {
    stop();
    if (!account)
        return;
    auto spec = account->user_stream_request();
    if (!spec)
        return; // platform/account doesn't stream

    account_key_ = account->account_key();
    log::write("stream[" + account_key_ + "]: start");
    running_ = std::make_shared<std::atomic<bool>>(true);
    auto running = running_;
    const std::string key = account_key_;

    net::HttpRequest req;
    req.method = "GET";
    req.url = spec->url;
    req.headers = spec->headers;
    req.headers.push_back({"Accept", "text/event-stream"});

    thread_ = std::thread([this, account, req = std::move(req), on_item = std::move(on_item),
                           running, key]() {
        while (running->load()) {
            net::SseParser parser;
            log::write("stream[" + key + "]: connecting");
            http_->send_stream(
                req, [&running] { return running->load(); },
                [&](std::string_view bytes) {
                    parser.feed(bytes, [&](const net::SseEvent& e) {
                        if (auto item = account->parse_stream_event(e.event, e.data)) {
                            main_->post([cb = on_item, it = std::move(*item)]() mutable {
                                cb(std::move(it));
                            });
                        }
                    });
                });
            if (!running->load()) {
                log::write("stream[" + key + "]: connection ended, stopping");
                break;
            }
            // Reconnect after a drop: ~5s, but wake promptly on stop().
            log::write("stream[" + key + "]: connection dropped, reconnecting in ~5s");
            for (int i = 0; i < 50 && running->load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void StreamingClient::stop() {
    if (running_) {
        log::write("stream[" + account_key_ + "]: stop requested (cancels ALL shared streams)");
        running_->store(false);
    }
    if (http_)
        http_->cancel_streams(); // unblock a read so the thread can exit
    if (thread_.joinable())
        thread_.join();
    running_.reset();
}

} // namespace fastsm
