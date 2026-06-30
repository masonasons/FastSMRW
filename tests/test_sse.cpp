#include "check.hpp"

#include <vector>

#include "fastsm/net/sse_parser.hpp"

using namespace fastsm;

void test_sse_basic() {
    net::SseParser p;
    std::vector<net::SseEvent> events;
    auto sink = [&](const net::SseEvent& e) { events.push_back(e); };

    p.feed("event: update\ndata: {\"id\":\"1\"}\n\n", sink);
    CHECK_EQ(events.size(), size_t(1));
    CHECK_EQ(events[0].event, std::string("update"));
    CHECK_EQ(events[0].data, std::string("{\"id\":\"1\"}"));
}

void test_sse_split_across_feeds() {
    // An event arriving in two chunks, split mid-line.
    net::SseParser p;
    std::vector<net::SseEvent> events;
    auto sink = [&](const net::SseEvent& e) { events.push_back(e); };

    p.feed("event: notif", sink);
    CHECK_EQ(events.size(), size_t(0));
    p.feed("ication\ndata: abc\n\n", sink);
    CHECK_EQ(events.size(), size_t(1));
    CHECK_EQ(events[0].event, std::string("notification"));
    CHECK_EQ(events[0].data, std::string("abc"));
}

void test_sse_multiline_crlf_comments() {
    // CRLF endings, a comment/keepalive line, and two data lines joined by \n.
    net::SseParser p;
    std::vector<net::SseEvent> events;
    auto sink = [&](const net::SseEvent& e) { events.push_back(e); };

    p.feed(":keepalive\r\nevent: update\r\ndata: line1\r\ndata: line2\r\n\r\n", sink);
    CHECK_EQ(events.size(), size_t(1));
    CHECK_EQ(events[0].event, std::string("update"));
    CHECK_EQ(events[0].data, std::string("line1\nline2"));
}
