#pragma once

#include <functional>
#include <string>
#include <string_view>

// Server-Sent Events (text/event-stream) parser. Mastodon's streaming API can be
// consumed as SSE over a long-lived HTTP GET, which (unlike WebSockets) works on
// Windows 7's WinHTTP. Feed raw bytes as they arrive; the parser invokes the
// callback once per complete event (terminated by a blank line).

namespace fastsm::net {

struct SseEvent {
    std::string event; // the "event:" name (e.g. "update", "notification", "delete")
    std::string data;  // the joined "data:" payload (multiple data lines joined by \n)
};

class SseParser {
public:
    // Append bytes and dispatch every complete event found so far. Partial
    // trailing data is buffered until the next feed().
    void feed(std::string_view bytes, const std::function<void(const SseEvent&)>& on_event);

private:
    void dispatch(const std::function<void(const SseEvent&)>& on_event);

    std::string buf_;   // bytes not yet split into a complete line
    std::string event_; // current event name
    std::string data_;  // accumulated data lines (each followed by \n)
    bool has_field_ = false; // saw at least one field since the last dispatch
};

} // namespace fastsm::net
