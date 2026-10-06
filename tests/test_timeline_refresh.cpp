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
// A conversation row: a status whose conversation_id is set (its id() is stable
// "c:<convo>", but refresh_key folds in the latest message id).
TimelineItem mkconvo(const std::string& convo, const std::string& msg) {
    Status s;
    s.id = msg;
    s.conversation_id = convo;
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
	CHECK_EQ(scan.updated.size(), size_t(3));
    CHECK(!scan.hit_known);
    CHECK_EQ(fetches, 1);
}

void test_refresh_updates_loaded_quotes() {
	TimelineSource src;
	src.kind = TimelineSource::Kind::PostUsers; // no cache or workers needed
	TimelineController tc(nullptr, src, nullptr, nullptr, nullptr);
	Status pending;
	pending.id = "quoting";
	pending.text = "RE: https://example.social/@alice/1 My thoughts";
	tc.ingest_realtime(TimelineItem{pending});
	tc.note_selection("s:quoting");
	int received = 0;
	int autoread = 0;
	tc.on_received_new = [&](int n, bool) { received += n; };
	tc.on_new_items = [&](const auto&) { ++autoread; };

	Status accepted = pending;
	accepted.text = "My thoughts";
	accepted.quote = std::make_shared<Status>();
	accepted.quote->id = "original";
	accepted.quote->text = "Original post";
	const auto scan = TimelineController::scan_refresh({"s:quoting"}, false, 5, 40,
		[&](const PageCursor&) {
			TimelinePage page;
			page.items.push_back(TimelineItem{accepted});
			return page;
		});
	CHECK(scan.fresh.empty());
	CHECK_EQ(scan.updated.size(), size_t(1));
	for (const auto& item : scan.updated)
		tc.ingest_realtime(item);
	CHECK_EQ(tc.items().size(), size_t(1));
	CHECK(tc.items()[0].status()->quote != nullptr);
	CHECK_EQ(tc.items()[0].status()->text, std::string("My thoughts"));
	CHECK_EQ(tc.selected_id(), std::string("s:quoting"));
	CHECK_EQ(received, 0);
	CHECK_EQ(autoread, 0);

	// A later server copy can also withdraw a quote; do not keep stale content.
	tc.ingest_realtime(TimelineItem{pending});
	CHECK(!tc.items()[0].status()->quote);
	CHECK_EQ(received, 0);
	CHECK_EQ(autoread, 0);
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

// A conversation that gained a new message must NOT be dropped by the refresh
// dedup. Its row id ("c:k1") is stable across messages, so an id-only dedup treats
// the updated conversation as "already have it" and leaves the buffer stale — the
// exact "Conversations doesn't update" bug. refresh_key folds in the latest message
// id, so the updated row reads as fresh and flows through to merge_fresh.
void test_refresh_keeps_updated_conversation() {
    const std::unordered_set<std::string> known = {mkconvo("k1", "m1").refresh_key(),
                                                    mkconvo("k2", "n1").refresh_key()};
    int fetches = 0;
    auto fetch = [&](const PageCursor&) {
        ++fetches;
        TimelinePage p;
        p.items.push_back(mkconvo("k1", "m2")); // k1 gained a newer message
        p.items.push_back(mkconvo("k2", "n1")); // k2 unchanged -> reconnects to known
        p.next_cursor = PageCursor::max_id("n1");
        return p;
    };
    const auto scan = TimelineController::scan_refresh(known, /*was_empty=*/false,
                                                       /*max_pages=*/5, /*fetch_limit=*/6, fetch);
    CHECK_EQ(scan.fresh.size(), size_t(1)); // the updated conversation came through
    if (scan.fresh.size() == 1)
        CHECK_EQ(scan.fresh[0].refresh_key(), std::string("c:k1:m2"));
    CHECK(scan.hit_known); // and the unchanged conversation stopped the scan
}

// A server-synced home marker can be older than this device's cache. Recovery
// must keep paging until that exact status arrives, retain the contiguous history
// above it, and stop there rather than needlessly walking farther back.
void test_marker_recovery_pages_until_found() {
    int fetches = 0;
    auto fetch = [&](const PageCursor& cursor) {
        ++fetches;
        if (cursor.kind == CursorKind::Start)
            return mkpage({"n6", "n5"}, PageCursor::max_id("n5"));
        if (cursor.value == "n5")
            return mkpage({"n4", "n3"}, PageCursor::max_id("n3"));
        return mkpage({"n2", "marker", "older"}, PageCursor::max_id("older"));
    };
    const auto scan = TimelineController::scan_to_marker(
        "marker", /*max_pages=*/10, /*fetch_limit=*/2, fetch);
    CHECK(scan.found);
    CHECK_EQ(fetches, 3);
    CHECK_EQ(scan.history.size(), size_t(6));
    if (scan.history.size() == 6)
        CHECK_EQ(scan.history.back().id(), std::string("s:marker"));
}

// A missing/deleted marker cannot cause an unbounded network walk. Reaching the
// safety cap returns the fetched prefix for stitching/manual scrollback.
void test_marker_recovery_obeys_page_bound() {
    int fetches = 0;
    auto fetch = [&](const PageCursor&) {
        ++fetches;
        return mkpage({"new", "old"}, PageCursor::max_id(std::to_string(fetches)));
    };
    const auto scan = TimelineController::scan_to_marker(
        "missing", /*max_pages=*/4, /*fetch_limit=*/2, fetch);
    CHECK(!scan.found);
    CHECK_EQ(fetches, 4);
}

// Losing the row you're reading must not lose your place in the timeline. A post
// can vanish under the cursor at any time — the author deletes it, or a filter
// starts hiding it — and every front end finds the reading position by id, so a
// vanished row used to drop the user at the top of the feed. The controller now
// re-anchors to whatever occupies the same slot.
//
// A static source is used so persist() short-circuits and the controller never
// touches its cache/worker/executor (null here).
void test_lost_row_keeps_reading_position() {
    TimelineSource src;
    src.kind = TimelineSource::Kind::PostUsers; // static: not cacheable, not re-sorted
    TimelineController tc(nullptr, src, nullptr, nullptr, nullptr, 40);
    for (const char* id : {"d", "c", "b", "a"}) // realtime items prepend -> a, b, c, d
        tc.ingest_realtime(mkitem(id));
    CHECK_EQ(tc.items().size(), size_t(4));

    tc.note_selection("s:c"); // reading the third row
    CHECK_EQ(tc.visible_index_of("s:c"), 2);

    // "c" starts being filtered out -> we land on whatever is now third ("d"),
    // NOT back at the top.
    tc.set_filter([](const TimelineItem& it) { return it.id() != "s:c"; });
    CHECK_EQ(tc.selected_id(), std::string("s:d"));
    CHECK_EQ(tc.visible_index_of(tc.selected_id()), 2);

    // Losing the last row clamps to the new end rather than jumping to the top.
    tc.note_selection("s:d");
    tc.set_filter([](const TimelineItem& it) { return it.id() != "s:c" && it.id() != "s:d"; });
    CHECK_EQ(tc.selected_id(), std::string("s:b"));

    // A position that was never resolvable is left alone: at startup the remembered
    // row may simply not be loaded yet, and stealing it would strand the user at the
    // top permanently instead of letting the row arrive.
    tc.note_selection("s:not-loaded-yet");
    tc.set_filter([](const TimelineItem&) { return true; });
    CHECK_EQ(tc.selected_id(), std::string("s:not-loaded-yet"));
}

// The remembered reading position is an id, but the cache only keeps the newest N
// rows — so on a busy feed the row you were reading routinely isn't there at the
// next launch, and the id alone puts the user back at the top. The saved timestamp
// anchors them to the nearest surviving row instead.
void test_position_hint_falls_back_to_nearest() {
    TimelineSource src;
    src.kind = TimelineSource::Kind::PostUsers; // static: persist() short-circuits
    TimelineController tc(nullptr, src, nullptr, nullptr, nullptr, 40);

    // Rows posted at t=500, 400, 300 (newest first, as raw_ is kept).
    std::int64_t when = 300;
    for (const char* id : {"old", "mid", "new"}) { // prepend order -> new, mid, old
        Status s;
        s.id = id;
        s.created_at = when;
        when += 100;
        tc.ingest_realtime(TimelineItem{std::move(s)});
    }
    CHECK_EQ(tc.items().size(), size_t(3));

    // Remembered row is gone, but it was posted at t=310 -> nearest is "old" (300),
    // not the top of the list.
    tc.set_position_hint("s:fell-past-the-cache-cap", 310);
    tc.set_filter(nullptr); // any rebuild settles the hint
    CHECK_EQ(tc.selected_id(), std::string("s:old"));

    // A hint whose row IS present is left exactly alone.
    tc.set_position_hint("s:mid", 400);
    tc.set_filter(nullptr);
    CHECK_EQ(tc.selected_id(), std::string("s:mid"));
}

// List controls can focus the first row automatically while a startup marker
// restore is pending. Only that default-edge echo is passive; navigating even
// one row away must still count as a real move and cancel the restore.
void test_marker_restore_ignores_only_provisional_edge_focus() {
    TimelineSource src;
    src.kind = TimelineSource::Kind::PostUsers; // static: no I/O
    TimelineController tc(nullptr, src, nullptr, nullptr, nullptr, 40);
    for (const char* id : {"third", "second", "first"})
        tc.ingest_realtime(mkitem(id));

    int moves = 0;
    tc.on_user_moved = [&] { ++moves; };
    tc.begin_marker_restore();
    tc.note_selection(tc.items().front().id());
    CHECK_EQ(moves, 0);
    tc.note_selection(tc.items()[1].id());
    CHECK_EQ(moves, 1);
}

// Restoring the synced position has three outcomes, and the session acts very
// differently on each: only "not in our window" may trigger the expensive walk
// back through history. Reporting "already on that row" as a failure is what made
// an idle client re-walk its whole timeline on every refresh.
void test_marker_restore_reports_already_there() {
    using MarkerRestore = TimelineController::MarkerRestore;
    TimelineSource src;
    src.kind = TimelineSource::Kind::PostUsers; // static: no I/O
    TimelineController tc(nullptr, src, nullptr, nullptr, nullptr, 40);
    for (const char* id : {"c", "b", "a"}) // prepend order -> a, b, c
        tc.ingest_realtime(mkitem(id));

    // Sitting somewhere else: the position moves to the marker.
    tc.note_selection("s:a");
    CHECK(tc.restore_marker_position("c") == MarkerRestore::Moved);
    CHECK_EQ(tc.selected_id(), std::string("s:c"));

    // Sitting ON the marker: a no-op success, not a miss.
    CHECK(tc.restore_marker_position("c") == MarkerRestore::AlreadyThere);
    CHECK_EQ(tc.selected_id(), std::string("s:c"));

    // Genuinely absent: the one case worth fetching history for.
    CHECK(tc.restore_marker_position("gone") == MarkerRestore::NotFound);
    CHECK_EQ(tc.selected_id(), std::string("s:c"));

    // Present but filtered out: anchor to a neighbor ("a", the nearest visible row
    // to "b"), and never ask for a walk — more history can't unhide a filtered row.
    tc.note_selection("s:c");
    tc.set_filter([](const TimelineItem& it) { return it.id() != "s:b"; });
    CHECK(tc.restore_marker_position("b") == MarkerRestore::Moved);
    CHECK(tc.restore_marker_position("b") == MarkerRestore::AlreadyThere);
}

// Front ends re-report the current row constantly (a list re-selecting after a
// reload, a phone's scroll settling, adopting the position the core just
// restored). None of that is the user moving: counting it as movement made an
// idle client stop following the synced home position.
void test_note_selection_same_row_is_not_a_move() {
    TimelineSource src;
    src.kind = TimelineSource::Kind::PostUsers; // static: no I/O
    TimelineController tc(nullptr, src, nullptr, nullptr, nullptr, 40);
    for (const char* id : {"c", "b", "a"}) // prepend order -> a, b, c
        tc.ingest_realtime(mkitem(id));

    int moves = 0;
    tc.on_user_moved = [&] { ++moves; };
    tc.note_selection("s:b");
    CHECK_EQ(moves, 1);
    tc.note_selection("s:b"); // the same row again: an echo, not a move
    CHECK_EQ(moves, 1);

    // A position the core restored, then echoed back by the front end.
    CHECK(tc.restore_marker_position("c") == TimelineController::MarkerRestore::Moved);
    tc.note_selection("s:c");
    CHECK_EQ(moves, 1);

    tc.note_selection("s:a"); // a real move still counts
    CHECK_EQ(moves, 2);
}

// The position restored from disk at startup must survive the echo a list control
// sends when it populates and focuses its own top row. Persisting that echo is what
// replaced the user's place with the newest post, so the next launch began at the
// top — and, with home sync on, dragged every other device up there too.
void test_restored_position_survives_default_edge_echo() {
    TimelineSource src;
    src.kind = TimelineSource::Kind::PostUsers; // static: no I/O
    TimelineController tc(nullptr, src, nullptr, nullptr, nullptr, 40);
    for (const char* id : {"c", "b", "a"}) // prepend order -> a, b, c
        tc.ingest_realtime(mkitem(id));

    // Startup: the remembered row is restored (it is present, so the date is unused).
    tc.set_position_hint("s:c", 100);
    CHECK_EQ(tc.selected_id(), std::string("s:c"));

    // The list appears and focuses its top row. Not the user's doing: not a move,
    // so the caller must not persist it.
    CHECK(!tc.note_selection("s:a"));

    // Now the user really moves. That is a move, and it disarms the guard.
    CHECK(tc.note_selection("s:b"));
    // From here on a top-row echo is an ordinary move again.
    CHECK(tc.note_selection("s:a"));
}
