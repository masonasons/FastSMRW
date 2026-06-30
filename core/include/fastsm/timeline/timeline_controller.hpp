#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
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
    std::function<void(int)> on_received_new;    // N new rows arrived on refresh

    // The filtered rows the UI should display.
    const std::vector<TimelineItem>& items() const { return visible_; }
    const TimelineSource& source() const { return source_; }
    SocialAccount* account() const { return account_; }
    std::string cache_key() const;

    // Client-side filter chokepoint: predicate returns true to KEEP a row.
    void set_filter(std::function<bool(const TimelineItem&)> predicate);

    void load_cached();
    void refresh();
    void load_older();
    // Fill a tracked middle gap (after the row with after_id) by fetching its
    // cursor's pages, stitching them in, and closing/advancing the gap.
    void load_gap(const std::string& after_id);
    const std::vector<store::CacheGap>& gaps() const { return gaps_; }
    void clear(); // empties the timeline and removes its cache

    // Merge one real-time (streamed) item: prepend if new, re-sort, chime, cache.
    void ingest_realtime(TimelineItem item);

    // Max pages fetched per refresh (gap fill), from the fetch-pages setting.
    void set_max_refresh_pages(int n) { max_refresh_pages_ = n < 1 ? 1 : n; }

    // Optimistic toggles on the row at `visible_index`; returns the new state.
    bool toggle_favorite(int visible_index);
    bool toggle_boost(int visible_index);

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
    std::vector<TimelineItem> raw_;     // everything fetched/cached
    std::vector<TimelineItem> visible_; // filtered view the UI reads
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
};

} // namespace fastsm
