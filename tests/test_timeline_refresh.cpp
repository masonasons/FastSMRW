#include "check.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "fastsm/timeline/timeline_controller.hpp"

using namespace fastsm;

namespace {
TimelineItem mkitem(const std::string& id) {
    Status s;
    s.id = id;
    return TimelineItem{std::move(s)};
}
// A page of statuses (newest-first) with an explicit next cursor.
TimelinePage mkpage(std::vector<std::string> ids, std::optional<PageCursor> next) {
    TimelinePage p;
    for (auto& id : ids)
        p.items.push_back(mkitem(id));
    p.next_cursor = std::move(next);
    return p;
}
} // namespace

// A realtime-streamed post (X) sits at the very top of a user timeline, above the
// posts made while the stream was down (C, B, A). Refresh must skip past the known
// streamed post and still collect the gap beneath it — the exact "Sent timeline
// stays stale" regression. (TimelineItem::id is kind-prefixed, e.g. "s:X".)
void test_refresh_fills_gap_below_streamed_top() {
    const std::unordered_set<std::string> known = {"s:X", "s:M", "s:old"};
    int fetches = 0;
    auto fetch = [&](const PageCursor&) {
        ++fetches;
        // Full page (== fetch_limit) with a next cursor, so ONLY reconnecting to
        // known content (M) can stop the scan — not a short/final page.
        return mkpage({"X", "C", "B", "A", "M", "old"}, PageCursor::max_id("old"));
    };
    const auto scan = TimelineController::scan_refresh(known, /*was_empty=*/false,
                                                       /*max_pages=*/5, /*fetch_limit=*/6, fetch);
    CHECK_EQ(scan.fresh.size(), size_t(3));
    if (scan.fresh.size() == 3) {
        CHECK_EQ(scan.fresh[0].id(), std::string("s:C"));
        CHECK_EQ(scan.fresh[1].id(), std::string("s:B"));
        CHECK_EQ(scan.fresh[2].id(), std::string("s:A"));
    }
    CHECK(scan.hit_known);  // reconnected to known content at M
    CHECK_EQ(fetches, 1);   // and stopped there — no needless extra page
}

// Nothing new: a fully-known first page stops the scan immediately instead of
// paging deep into history every refresh.
void test_refresh_steady_state_stops_early() {
    const std::unordered_set<std::string> known = {"s:M", "s:old", "s:older"};
    int fetches = 0;
    auto fetch = [&](const PageCursor&) {
        ++fetches;
        return mkpage({"M", "old", "older"}, PageCursor::max_id("older"));
    };
    const auto scan = TimelineController::scan_refresh(known, false, 5, 3, fetch);
    CHECK(scan.fresh.empty());
    CHECK(!scan.hit_known);
    CHECK_EQ(fetches, 1);
}

// A gap larger than one page below a streamed top is filled across pages until it
// reconnects to known content.
void test_refresh_fills_multipage_gap() {
    const std::unordered_set<std::string> known = {"s:X", "s:M"};
    int fetches = 0;
    auto fetch = [&](const PageCursor& c) {
        ++fetches;
        if (c.kind != CursorKind::MaxID) // first page: streamed top + start of gap
            return mkpage({"X", "g1", "g2", "g3", "g4", "g5"}, PageCursor::max_id("g5"));
        return mkpage({"g6", "g7", "g8", "g9", "g10", "M"}, PageCursor::max_id("M"));
    };
    const auto scan = TimelineController::scan_refresh(known, false, 5, 6, fetch);
    CHECK_EQ(scan.fresh.size(), size_t(10)); // g1..g10
    CHECK(scan.hit_known);
    CHECK_EQ(fetches, 2);
}
