#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fastsm/models/models.hpp"
#include "fastsm/platform/social_account.hpp"
#include "fastsm/runtime/main_executor.hpp"
#include "fastsm/runtime/worker_queue.hpp"
#include "fastsm/store/timeline_cache.hpp"
#include "fastsm/timeline/timeline_source.hpp"

namespace fastsm {

// Stateful timeline: cache-first load, refresh/scrollback, optimistic
// boost/favorite, and posting. Network and cache I/O run on the worker thread;
// all list state lives on and is mutated from the main thread (results are
// marshalled back via the executor), so reads from the UI need no locking.
//
// Client-side filtering is a first-class concern: raw rows are kept separate
// from the filtered `items()` the UI displays. Adding a filter is just calling
// set_filter() — fetch/cache/merge never change.
class TimelineController {
public:
    TimelineController(SocialAccount* account, TimelineSource source, store::TimelineCache* cache,
                       runtime::WorkerQueue* worker, runtime::IMainExecutor* main,
                       int fetch_limit = 40);

    // Callbacks fire on the main thread.
    std::function<void()> on_change;            // the visible list changed
    std::function<void(std::string)> on_error;  // a network action failed
    // N new rows arrived (that pass the filter); has_direct = any is a DM/direct mention.
    std::function<void(int n, bool has_direct)> on_received_new;

    // The filtered rows the UI should display.
    const std::vector<TimelineItem>& items() const { return visible_; }
    const TimelineSource& source() const { return source_; }
    SocialAccount* account() const { return account_; }
    std::string cache_key() const;

    // Client-side filter chokepoint: predicate returns true to KEEP a row.
    void set_filter(std::function<bool(const TimelineItem&)> predicate);

    // --- Refresh page-scan (pure; exposed for testing) ---
    // The decision logic behind refresh(): walk pages newest-first via
    // `fetch(cursor)`, collecting items whose id isn't in `known`. Known items
    // sitting ABOVE the first fresh one — a realtime-streamed post or a floated
    // pinned post at the top — are skipped; the scan only stops once it reaches
    // known content BELOW a fresh one (reconnected), runs out of pages, hits a
    // short/empty page, or a full page brings nothing new. This is what lets a
    // refresh fill the gap beneath a streamed top instead of short-circuiting on
    // it. No threads or network, so it's unit-testable directly.
    struct RefreshScan {
        std::vector<TimelineItem> fresh;
        std::vector<std::pair<std::string, PageCursor>> marks; // cold-load scrollback marks
        std::optional<PageCursor> tail;                        // cursor past the fetched region
        bool hit_known = false;                                // reconnected to known content
    };
    static RefreshScan scan_refresh(const std::unordered_set<std::string>& known, bool was_empty,
                                    int max_pages, int fetch_limit,
                                    const std::function<TimelinePage(const PageCursor&)>& fetch);

    // Global "reverse timelines" preference: when on, the visible list is flipped
    // (oldest at top, newest at bottom) for time-ordered feeds only. raw_ stays
    // canonical newest-first, so merge/gap/pagination are unaffected — only the
    // projection into visible_ is reversed. Threads/searches keep their order.
    void set_reversed(bool reversed);
    bool reversed() const { return reversed_ && source_.is_time_ordered(); }

    // "Pinned tab": the user has locked this timeline so it can't be dismissed.
    // Purely a per-controller flag (persisted with the open-timelines list); it
    // has no effect on fetching/ordering.
    void set_pinned(bool pinned) { pinned_ = pinned; }
    bool pinned() const { return pinned_; }

    // "Muted tab": the user has silenced this timeline's new-item earcon. Purely a
    // per-controller flag (persisted with the open-timelines list); it only gates
    // the chime — fetching/ordering are unaffected.
    void set_muted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }

    // Populate a static (non-fetched) timeline with a fixed set of user rows —
    // e.g. the users referenced in one post. Safe to call again to replace them
    // (e.g. after enriching sparse mentions with full profiles); the selected id
    // is preserved if it still exists.
    void seed_users(std::vector<User> users);

    // Restore a reading position remembered from a previous run: the row id, plus
    // when that row was posted. The id alone isn't enough — the cache keeps only the
    // newest N rows, so a position deep in a busy feed routinely falls off the end
    // between sessions (and the post may simply have been deleted). The timestamp
    // lets the cache load resume at the nearest surviving row instead of the top.
    void set_position_hint(const std::string& id, std::int64_t sort_date);

    void load_cached();
    void refresh();
    // Fetch the next page(s) of older posts. `automatic` marks a load triggered by
    // navigation/rendering rather than by the user asking for it: a sparse feed
    // (Mentions especially) can return a whole multi-page fetch of nothing new, and
    // without a gate every keypress near the bottom would fire another one, dragging
    // ever-older posts into the list. Once an automatic load lands nothing, further
    // automatic loads are suppressed until the row count actually changes. A manual
    // load (the Load Older command) is never suppressed.
    void load_older(bool automatic = false);
    // Fill a tracked middle gap (after the row with after_id) by fetching its
    // cursor's pages, stitching them in, and closing/advancing the gap.
    void load_gap(const std::string& after_id);
    const std::vector<store::CacheGap>& gaps() const { return gaps_; }
    void clear(); // empties the timeline and removes its cache

    // Merge one real-time (streamed) item: prepend if new, re-sort, chime, cache.
    void ingest_realtime(TimelineItem item);

    // Merge one streamed notification, collapsing it into an on-screen group (by
    // group_key) when one exists — bumping its actor count and floating it to the
    // top — instead of adding a duplicate row. Falls back to ingest_realtime for a
    // brand-new group / ungrouped notification.
    void ingest_notification(Notification n);

    // Max pages fetched per refresh (gap fill), from the fetch-pages setting.
    void set_max_refresh_pages(int n) { max_refresh_pages_ = n < 1 ? 1 : n; }

    // Optimistic toggles on the row at `visible_index`; returns the new state.
    // `done(ok, active)` runs on the main thread once the server responds so the
    // caller can chime only on success (ok) — active = the resulting state.
    bool toggle_favorite(int visible_index, std::function<void(bool ok, bool active)> done = {});
    bool toggle_boost(int visible_index, std::function<void(bool ok, bool active)> done = {});
    // Pin/unpin one of your own posts to your profile. Returns 1 if now pinned, 0
    // if now unpinned, or -1 if the row isn't your own post (nothing changes).
    // `done(ok, active)` fires after the server responds (not for the -1 case).
    int toggle_pin_post(int visible_index, std::function<void(bool ok, bool active)> done = {});
    // Mute/unmute the conversation a row's post belongs to. Returns true if now
    // muted. `done(ok, active)` fires after the server responds.
    bool toggle_mute_conversation(int visible_index,
                                  std::function<void(bool ok, bool active)> done = {});
    // Replace the poll on a row's status (after voting) and refresh the view.
    void set_poll(const std::string& row_id, const Poll& poll);
    // Remove a row (e.g. a post you just deleted) and refresh + re-persist.
    void remove_status(const std::string& id);
    // Replace an edited post (matched by id) in place — from a streamed edit.
    void update_status(const Status& updated);

    void post(const PostDraft& draft, std::function<void(bool)> done);
    void edit_post(const std::string& id, const PostDraft& draft, std::function<void(bool)> done);

    // Per-timeline selected-row memory (by item id). The UI records the focused
    // row here; on switching back or after a refresh it restores this position
    // instead of jumping to the top (Mac parity). A "jump" (moving more than one
    // row) is pushed onto the nav history for Go Back.
    void note_selection(const std::string& id);
    const std::string& selected_id() const { return selected_id_; }
    int visible_index_of(const std::string& id) const;

    // Cache key of the timeline that was current when this one was spawned, so
    // closing it returns there instead of a neighbor (Mac parity). Empty for the
    // standing feeds.
    void set_origin_key(std::string key) { origin_key_ = std::move(key); }
    const std::string& origin_key() const { return origin_key_; }

    // Replace the display title (only affects title_text) — used to refresh a
    // re-seeded static timeline's title, e.g. a User Analysis result count.
    void set_source_title(std::string title) { source_.title_text = std::move(title); }

    // Go Back (Ctrl+Z): pop the nav history to the most recent still-present row,
    // make it the position, and return its id (empty if nothing to undo). This
    // restore itself is not recorded.
    std::string undo_navigation();

private:
    void rebuild_visible();
    void merge_fresh(std::vector<TimelineItem> fresh, std::optional<PageCursor> next);
    void persist();
    TimelineItem* find_raw(const std::string& id);

    SocialAccount* account_;
    TimelineSource source_;
    store::TimelineCache* cache_;
    runtime::WorkerQueue* worker_;
    runtime::IMainExecutor* main_;
    int fetch_limit_;

    std::string selected_id_;           // remembered selected row (by id)
    std::string origin_key_;            // where we came from (for close-returns)
    std::vector<std::string> nav_history_; // prior positions, for Go Back (jumps)
    std::vector<TimelineItem> raw_;     // everything fetched/cached (canonical newest-first)
    std::vector<TimelineItem> visible_; // filtered view the UI reads
    bool reversed_ = false;             // global reverse-timelines preference
    bool pinned_ = false;               // user "pinned" this tab (locks dismissal)
    bool muted_ = false;                // user muted this tab's new-item earcon
    std::optional<PageCursor> scrollback_cursor_;
    std::vector<store::CacheGap> gaps_; // tracked middle gaps (after_id -> cursor)
    // Page-boundary cursors (row id -> cursor to fetch the page just below it),
    // recorded as we page downward. Lets persist() cap the cache to a real page
    // boundary with a valid cursor, so scrollback resumes even for opaque
    // (Bluesky) cursors after the cache is truncated.
    std::unordered_map<std::string, PageCursor> page_marks_;
    std::function<bool(const TimelineItem&)> filter_;
    int max_refresh_pages_ = 5;
    bool loading_ = false;
    // Automatic-paging gate (see load_older): set when an automatic load landed no
    // new rows, cleared as soon as raw_ changes size by any other means.
    bool auto_load_dry_ = false;
    std::size_t auto_load_mark_ = 0;
    // When the remembered row was posted; 0 once the position has been resolved.
    std::int64_t restore_date_hint_ = 0;
    void resolve_position_hint();
};

} // namespace fastsm
