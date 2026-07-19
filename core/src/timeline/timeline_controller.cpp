#include "fastsm/timeline/timeline_controller.hpp"

#include <algorithm>
#include <unordered_set>

namespace fastsm {

TimelineController::TimelineController(SocialAccount* account, TimelineSource source,
                                      store::TimelineCache* cache, runtime::WorkerQueue* worker,
                                      runtime::IMainExecutor* main, int fetch_limit)
    : account_(account), source_(source), cache_(cache), worker_(worker), main_(main),
      fetch_limit_(fetch_limit) {}

std::string TimelineController::cache_key() const {
    return account_->account_key() + ":" + source_.cache_key();
}

int TimelineController::visible_index_of(const std::string& id) const {
    if (id.empty())
        return -1;
    for (size_t i = 0; i < visible_.size(); ++i)
        if (visible_[i].id() == id)
            return static_cast<int>(i);
    return -1;
}

void TimelineController::note_selection(const std::string& id) {
    // Record a "jump" (move of more than one visible row, or to/from an item not
    // in the list) so Go Back can return here; single-row steps aren't recorded.
    if (!selected_id_.empty() && selected_id_ != id) {
        const int from = visible_index_of(selected_id_);
        const int to = visible_index_of(id);
        const int diff = from > to ? from - to : to - from;
        const bool jump = from < 0 || to < 0 || diff > 1;
        if (jump && (nav_history_.empty() || nav_history_.back() != selected_id_)) {
            nav_history_.push_back(selected_id_);
            if (nav_history_.size() > 20)
                nav_history_.erase(nav_history_.begin());
        }
    }
    selected_id_ = id;
}

std::string TimelineController::undo_navigation() {
    while (!nav_history_.empty()) {
        const std::string target = nav_history_.back();
        nav_history_.pop_back();
        if (visible_index_of(target) >= 0) {
            selected_id_ = target;
            return target;
        }
    }
    return {};
}

void TimelineController::set_filter(std::function<bool(const TimelineItem&)> predicate) {
    filter_ = std::move(predicate);
    rebuild_visible();
    if (on_change)
        on_change();
}

void TimelineController::set_reversed(bool reversed) {
    if (reversed_ == reversed)
        return;
    reversed_ = reversed;
    rebuild_visible();
    if (on_change)
        on_change();
}

void TimelineController::rebuild_visible() {
    // Where the reading position sits *before* the rebuild. If that row doesn't
    // survive — someone deleted the post, a filter started hiding it — we re-anchor
    // to whatever now occupies the same slot instead of letting the front ends fall
    // back to "row 0", which threw the user to the top of the timeline.
    const int prev_index = visible_index_of(selected_id_);
    visible_.clear();
    visible_.reserve(raw_.size());
    // Guarantee the visible list holds each id at most once. Navigation tracks
    // position by id (visible_index_of), so a duplicate id would strand the
    // cursor on it -- stepping off always resolves back to the first copy. This
    // is the last-line defense; the merge paths also dedupe at the source.
    std::unordered_set<std::string> seen;
    seen.reserve(raw_.size());
    for (const auto& item : raw_) {
        if (filter_ && !filter_(item))
            continue;
        if (!seen.insert(item.id()).second)
            continue; // a duplicate id already made it in
        visible_.push_back(item);
    }
    // raw_ is canonical newest-first; flip the projection for time-ordered feeds
    // when the reverse preference is on, so the UI reads oldest-first. Pinned posts
    // stay pinned at the very top, so only the non-pinned tail is reversed.
    if (reversed_ && source_.is_time_ordered()) {
        auto tail = std::find_if(visible_.begin(), visible_.end(),
                                 [](const TimelineItem& it) { return !it.is_pinned(); });
        std::reverse(tail, visible_.end());
    }
    // Re-anchor a lost reading position to the same slot (clamped), so a vanished
    // row costs the user one row of drift rather than their whole place in the feed.
    if (prev_index >= 0 && !visible_.empty() && visible_index_of(selected_id_) < 0) {
        const int last = static_cast<int>(visible_.size()) - 1;
        selected_id_ = visible_[static_cast<size_t>(prev_index > last ? last : prev_index)].id();
    }
    // First time rows exist after a restart, settle a remembered position whose row
    // didn't survive to this session. Self-clearing, so it costs nothing after that.
    resolve_position_hint();
}

TimelineItem* TimelineController::find_raw(const std::string& id) {
    for (auto& item : raw_) {
        if (item.id() == id)
            return &item;
    }
    return nullptr;
}

void TimelineController::merge_fresh(std::vector<TimelineItem> fresh,
                                     std::optional<PageCursor> next) {
    // Dedupe by id against what we already have AND within this batch. Paginated
    // fetches can return the same post on two adjacent pages when new posts shift
    // the window mid-fetch, so `fresh` may carry internal duplicates; letting one
    // through shows the post twice and (because navigation is keyed by id) strands
    // the cursor on it.
    std::unordered_set<std::string> seen;
    seen.reserve(raw_.size() + fresh.size());
    for (const auto& it : raw_)
        seen.insert(it.id());
    // Two kinds of row have a stable identity distinct from their id(): a grouped
    // notification (its id = most-recent notification, which changes as actors
    // arrive) and a DM conversation row (its id = latest message, which changes as
    // messages arrive). Map each on-screen one to its row and update it in place, so
    // a refresh/stream doesn't show the same group/conversation twice.
    std::unordered_map<std::string, size_t> group_row, convo_row;
    for (size_t i = 0; i < raw_.size(); ++i) {
        if (const Notification* nn = raw_[i].notification(); nn && !nn->group_key.empty())
            group_row[nn->group_key] = i;
        else if (const Status* ss = raw_[i].status(); ss && !ss->conversation_id.empty())
            convo_row[ss->conversation_id] = i;
    }
    std::vector<TimelineItem> added;
    added.reserve(fresh.size());
    for (auto& it : fresh) {
        if (const Notification* nn = it.notification(); nn && !nn->group_key.empty()) {
            if (auto g = group_row.find(nn->group_key); g != group_row.end()) {
                raw_[g->second] = std::move(it); // authoritative server group -> replace in place
                continue;
            }
        } else if (const Status* ss = it.status(); ss && !ss->conversation_id.empty()) {
            if (auto c = convo_row.find(ss->conversation_id); c != convo_row.end()) {
                raw_[c->second] = std::move(it); // same conversation -> replace with its latest msg
                continue;
            }
        }
        if (seen.insert(it.id()).second)
            added.push_back(std::move(it));
    }

    if (raw_.empty()) {
        raw_ = std::move(added);
        scrollback_cursor_ = next;
    } else {
        // Only chime for rows that will actually be visible — e.g. a streamed
        // mention shouldn't ping when mentions are hidden from Notifications.
        int visible_new = 0;
        bool has_direct = false;
        for (const auto& it : added) {
            if (filter_ && !filter_(it))
                continue; // hidden (client filter / hide filter / hidden mentions)
            // A post that matched a server-side filter (even a "warn" one that's
            // still shown) shouldn't chime.
            if (const Status* s = it.status(); s && s->any_filter_matched())
                continue;
            ++visible_new;
            if (it.is_direct())
                has_direct = true; // a DM/direct mention -> the "messages" chime
        }
        // Prepend the genuinely new rows, preserving server order.
        added.insert(added.end(), std::make_move_iterator(raw_.begin()),
                     std::make_move_iterator(raw_.end()));
        raw_ = std::move(added);
        if (visible_new > 0 && on_received_new)
            on_received_new(visible_new, has_direct);
    }

    // Keep rows newest-first by timestamp (notifications included — their
    // sort_date is created_at). Threads keep their fetched conversation order.
    // Pinned posts (user timelines) float to the very top as a block, ahead of
    // everything else, so newly-arriving posts always land beneath them; within
    // each group the order stays newest-first (stable_sort keeps the pin order).
    if (source_.is_time_ordered())
        std::stable_sort(raw_.begin(), raw_.end(),
                         [](const TimelineItem& a, const TimelineItem& b) {
                             if (a.is_pinned() != b.is_pinned())
                                 return a.is_pinned();
                             return a.sort_date() > b.sort_date();
                         });
    rebuild_visible();
    persist();
}

void TimelineController::load_gap(const std::string& after_id) {
    if (loading_)
        return;
    auto git = std::find_if(gaps_.begin(), gaps_.end(),
                            [&](const store::CacheGap& g) { return g.after_id == after_id; });
    if (git == gaps_.end())
        return;
    loading_ = true;
    const PageCursor start = git->cursor;
    const int pages = max_refresh_pages_;
    std::unordered_set<std::string> known;
    known.reserve(raw_.size());
    for (const auto& it : raw_)
        known.insert(it.id());
    worker_->post([this, after_id, start, pages, known = std::move(known)]() mutable {
        std::vector<TimelineItem> fetched;
        std::optional<PageCursor> next = start;
        bool connected = false;
        for (int i = 0; i < pages && next && !connected; ++i) {
            TimelinePage p = account_->items(source_, fetch_limit_, *next);
            next = p.next_cursor;
            for (auto& it : p.items) {
                if (known.find(it.id()) != known.end()) {
                    connected = true; // reached the cached segment below the gap
                    break;
                }
                fetched.push_back(std::move(it));
            }
        }
        if (!next)
            connected = true; // ran out of feed -> nothing more to fill
        main_->post([this, after_id, fetched = std::move(fetched), next, connected]() mutable {
            std::unordered_set<std::string> existing;
            existing.reserve(raw_.size());
            for (const auto& it : raw_)
                existing.insert(it.id());
            std::vector<TimelineItem> fresh;
            for (auto& it : fetched)
                if (existing.insert(it.id()).second) // dedupe vs raw_ and within the batch
                    fresh.push_back(std::move(it));
            std::string new_after = after_id;
            auto pos = std::find_if(raw_.begin(), raw_.end(),
                                    [&](const TimelineItem& it) { return it.id() == after_id; });
            if (pos != raw_.end()) {
                ++pos; // stitch the fetched posts in just after the gap anchor
                if (!fresh.empty())
                    new_after = fresh.back().id();
                raw_.insert(pos, std::make_move_iterator(fresh.begin()),
                            std::make_move_iterator(fresh.end()));
            }
            auto g2 = std::find_if(gaps_.begin(), gaps_.end(),
                                   [&](const store::CacheGap& g) { return g.after_id == after_id; });
            if (g2 != gaps_.end()) {
                if (connected || !next)
                    gaps_.erase(g2); // gap closed
                else {
                    g2->after_id = new_after; // gap shrank; advance it
                    g2->cursor = *next;
                }
            }
            rebuild_visible();
            persist();
            loading_ = false;
            if (on_change)
                on_change();
        });
    });
}

void TimelineController::persist() {
    if (!source_.is_cacheable())
        return;
    // Snapshot on the main thread, write on the worker thread. The scrollback
    // cursor rides along so loading older posts resumes after a cache load.
    const int cap = cache_->max_entries();
    if (cap <= 0) { // caching disabled: don't write, and drop any existing file
        cache_->remove(cache_key()); // account-prefixed, like every other cache call
        return;
    }
    std::vector<TimelineItem> snapshot;
    std::optional<PageCursor> cursor;
    bool truncated = false;
    if (static_cast<int>(raw_.size()) <= cap) {
        snapshot = raw_;
        cursor = scrollback_cursor_;
    } else {
        truncated = true;
        // Cap to the deepest page boundary at or above the cap, so the stored
        // cursor exactly matches the stored rows -- works for opaque cursors too.
        int cut = -1;
        for (int i = std::min(cap, static_cast<int>(raw_.size())) - 1; i >= 0; --i) {
            auto m = page_marks_.find(raw_[static_cast<size_t>(i)].id());
            if (m != page_marks_.end()) {
                cut = i;
                cursor = m->second;
                break;
            }
        }
        if (cut >= 0) {
            snapshot.assign(raw_.begin(), raw_.begin() + (cut + 1));
        } else {
            snapshot.assign(raw_.begin(), raw_.begin() + cap);
            if (account_ && account_->platform() == Platform::Mastodon &&
                source_.paginates_by_item_id() && !snapshot.empty())
                cursor = PageCursor::max_id(snapshot.back().pagination_id());
        }
    }
    std::unordered_set<std::string> ids;
    for (const auto& it : snapshot)
        ids.insert(it.id());
    std::vector<store::CacheGap> gaps;
    for (const auto& g : gaps_)
        if (ids.count(g.after_id))
            gaps.push_back(g);
    std::vector<store::CacheGap> marks;
    for (const auto& [id, c] : page_marks_)
        if (ids.count(id))
            marks.push_back({id, c});
    const std::string key = cache_key();
    worker_->post([this, key, snapshot = std::move(snapshot), cursor, truncated, gaps, marks] {
        cache_->save(key, snapshot, cursor, truncated, gaps, marks);
    });
}

void TimelineController::seed_users(std::vector<User> users) {
    raw_.clear();
    raw_.reserve(users.size());
    for (auto& u : users)
        raw_.push_back(TimelineItem{std::move(u)});
    rebuild_visible();
    if (on_change)
        on_change();
}

void TimelineController::set_position_hint(const std::string& id, std::int64_t sort_date) {
    selected_id_ = id;
    restore_date_hint_ = sort_date;
}

void TimelineController::resolve_position_hint() {
    if (restore_date_hint_ == 0 || visible_.empty())
        return;
    if (visible_index_of(selected_id_) >= 0) { // the remembered row is right here
        restore_date_hint_ = 0;
        return;
    }
    // It isn't loaded (past the cache cap, or deleted). Land on the row posted
    // closest to it in time — the user resumes roughly where they left off, and
    // scrolling down from there continues into the posts they hadn't read.
    // Order-independent so it works for reversed feeds too.
    std::size_t best = visible_.size();
    std::int64_t best_delta = 0;
    for (std::size_t i = 0; i < visible_.size(); ++i) {
        const std::int64_t d = visible_[i].sort_date();
        if (d == 0)
            continue; // not time-ordered (user rows) — nothing to compare
        const std::int64_t delta = d > restore_date_hint_ ? d - restore_date_hint_
                                                          : restore_date_hint_ - d;
        if (best == visible_.size() || delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    if (best < visible_.size())
        selected_id_ = visible_[best].id();
    restore_date_hint_ = 0;
}

void TimelineController::load_cached() {
    if (source_.is_static()) // seeded in memory; nothing on disk to load
        return;
    // Synchronous on purpose: a fast local read that populates raw_ *before*
    // refresh() snapshots its known-id set, so the immediately-following refresh
    // does a warm gap-fill (fetch only the delta and stop at the first known
    // post) instead of a cold full refetch on every launch.
    if (!raw_.empty())
        return;
    store::LoadedTimeline cached = cache_->load(cache_key());
    if (cached.items.empty())
        return;
    raw_ = std::move(cached.items);
    // Drop any duplicate ids a pre-fix cache may still hold, so a stale double
    // post (and the navigation snag it caused) doesn't come back on launch.
    {
        std::unordered_set<std::string> seen;
        seen.reserve(raw_.size());
        raw_.erase(std::remove_if(raw_.begin(), raw_.end(),
                                  [&](const TimelineItem& it) {
                                      return !seen.insert(it.id()).second;
                                  }),
                   raw_.end());
    }
    // Restore scrollback so "load older" works after a cache load. If the cache
    // wasn't truncated, the persisted cursor is exact (works for every platform).
    // If it was truncated, the persisted cursor points below un-cached posts, so
    // re-derive for Mastodon (max_id) and otherwise drop it (Bluesky's opaque
    // cursor can't be rebuilt from the rows).
    if (cached.scrollback)
        scrollback_cursor_ = cached.scrollback; // boundary-matched, exact for any platform
    else if (account_ && account_->platform() == Platform::Mastodon &&
             source_.paginates_by_item_id() && !raw_.empty())
        scrollback_cursor_ = PageCursor::max_id(raw_.back().pagination_id());
    gaps_ = std::move(cached.gaps);
    for (const auto& m : cached.marks)
        page_marks_[m.after_id] = m.cursor;
    rebuild_visible(); // also settles the remembered position (see resolve_position_hint)
    if (on_change)
        on_change();
}

TimelineController::RefreshScan TimelineController::scan_refresh(
    const std::unordered_set<std::string>& known, bool was_empty, int max_pages, int fetch_limit,
    const std::function<TimelinePage(const PageCursor&)>& fetch) {
    RefreshScan out;
    PageCursor cursor = PageCursor::start();
    bool seen_unknown = false; // have we collected any fresh post yet this scan?
    for (int page = 0; page < max_pages; ++page) {
        TimelinePage p = fetch(cursor);
        const size_t page_size = p.items.size();
        const std::string last_id = page_size ? p.items.back().id() : std::string{};
        out.tail = p.next_cursor;
        if (page_size == 0)
            break;
        bool page_had_unknown = false;
        for (auto& it : p.items) {
            if (known.find(it.id()) != known.end()) {
                // A known id BELOW fresh posts means we've paged back to content we
                // already have -> stop. A known id ABOVE any fresh one is a post
                // sitting at the top that this scan didn't fetch — a realtime-
                // streamed post or a floated pinned post — with a gap beneath it;
                // skip past it and keep filling the gap rather than short-circuiting
                // the whole refresh on it.
                if (seen_unknown) {
                    out.hit_known = true;
                    break;
                }
                continue;
            }
            out.fresh.push_back(std::move(it));
            seen_unknown = true;
            page_had_unknown = true;
        }
        // On a cold load every page is a scrollback boundary; record its cursor.
        if (was_empty && out.tail)
            out.marks.push_back({last_id, *out.tail});
        // Stop when we've reconnected to known content, run out of pages, hit a
        // short (final) page, or a full page brought nothing new (steady state: the
        // top is entirely known, so there's no gap to fill).
        if (out.hit_known || !p.next_cursor || static_cast<int>(page_size) < fetch_limit ||
            !page_had_unknown)
            break;
        cursor = *p.next_cursor;
    }
    return out;
}

void TimelineController::refresh() {
    if (source_.is_static()) // seeded rows never refresh from the network
        return;
    if (loading_)
        return;
    loading_ = true;

    // Snapshot the ids we already have (from cache or a prior load) so the
    // worker can page forward through ALL new posts and stop as soon as it
    // reaches one we know — this fills the gap instead of grabbing only the
    // newest page. Bounded so a first load (nothing known) can't run away.
    const int kMaxRefreshPages = max_refresh_pages_;
    std::unordered_set<std::string> known;
    known.reserve(raw_.size());
    for (const auto& it : raw_)
        known.insert(it.id());
    const bool was_empty = raw_.empty();

    worker_->post([this, known = std::move(known), was_empty, kMaxRefreshPages]() mutable {
        RefreshScan scan = scan_refresh(
            known, was_empty, kMaxRefreshPages, fetch_limit_,
            [this](const PageCursor& c) { return account_->items(source_, fetch_limit_, c); });
        main_->post([this, fresh = std::move(scan.fresh), tail = scan.tail, was_empty,
                     hit_known = scan.hit_known, marks = std::move(scan.marks)]() mutable {
            for (auto& m : marks)
                page_marks_[m.first] = m.second;
            // The fresh posts didn't reach the cached posts -> there's a gap below
            // them; record it (after the oldest fresh post) so it can be filled.
            const bool disconnected = !was_empty && !hit_known && tail && !fresh.empty();
            const std::string gap_after = disconnected ? fresh.back().id() : std::string{};
            const std::optional<PageCursor> gap_cursor = tail;
            // On a first load (nothing known) seed the scrollback cursor; on a
            // refresh keep the existing one (it points below the current bottom).
            merge_fresh(std::move(fresh), was_empty ? tail : scrollback_cursor_);
            if (disconnected && gap_cursor)
                gaps_.push_back({gap_after, *gap_cursor});
            loading_ = false;
            if (on_change)
                on_change();
        });
    });
}

void TimelineController::load_older(bool automatic) {
    if (loading_ || !scrollback_cursor_)
        return;
    // A sparse feed keeps handing back pages we already have. Don't let navigation
    // re-trigger that walk on every keypress — wait until the list actually changes.
    if (automatic && auto_load_dry_ && raw_.size() == auto_load_mark_)
        return;
    loading_ = true;
    const PageCursor start = *scrollback_cursor_;
    const int pages = max_refresh_pages_;
    const std::size_t before = raw_.size();
    worker_->post([this, start, pages, automatic, before] {
        std::vector<TimelineItem> older;
        std::vector<std::pair<std::string, PageCursor>> marks;
        std::optional<PageCursor> next = start;
        for (int i = 0; i < pages && next; ++i) {
            TimelinePage page = account_->items(source_, fetch_limit_, *next);
            const size_t page_size = page.items.size();
            const std::string last_id = page_size ? page.items.back().id() : std::string{};
            next = page.next_cursor;
            if (page_size == 0)
                break;
            for (auto& it : page.items)
                older.push_back(std::move(it));
            if (next)
                marks.push_back({last_id, *next}); // cursor to fetch below this page
            if (!next || static_cast<int>(page_size) < fetch_limit_)
                break;
        }
        main_->post([this, older = std::move(older), next, marks = std::move(marks), automatic,
                     before]() mutable {
            std::unordered_set<std::string> existing;
            for (const auto& it : raw_)
                existing.insert(it.id());
            for (auto& it : older)
                if (existing.insert(it.id()).second) // dedupe vs raw_ and within the batch
                    raw_.push_back(std::move(it));
            for (auto& m : marks)
                page_marks_[m.first] = m.second;
            if (automatic) {
                // Nothing new came back: stop auto-paging until the list changes.
                auto_load_dry_ = raw_.size() == before;
                auto_load_mark_ = raw_.size();
            }
            scrollback_cursor_ = next;
            rebuild_visible();
            persist();
            loading_ = false;
            if (on_change)
                on_change();
        });
    });
}

void TimelineController::ingest_realtime(TimelineItem item) {
    // Reuse the refresh merge path: dedupe by id, prepend, re-sort newest-first,
    // chime the new-posts sound, and persist. The cursor is unchanged.
    std::vector<TimelineItem> one;
    one.push_back(std::move(item));
    merge_fresh(std::move(one), scrollback_cursor_);
    if (on_change)
        on_change();
}

void TimelineController::ingest_notification(Notification n) {
    // If the server grouped this and the group is already on screen, fold the new
    // actor into that row: bump the count, adopt the newest actor/time/id (the id
    // advances so a later refresh dedupes against it), re-sort, and chime once.
    if (!n.group_key.empty()) {
        for (auto& item : raw_) {
            const Notification* existing = item.notification();
            if (!existing || existing->group_key != n.group_key)
                continue;
            Notification merged = *existing;
            merged.notifications_count += 1;
            merged.account = n.account;    // newest actor is the named "A"
            merged.created_at = n.created_at;
            // Keep the existing row id (the stable group_key) so selection/scroll
            // survive and a later refresh dedupes against the same group.
            const bool visible = !filter_ || filter_(TimelineItem{merged});
            item.value = std::move(merged);
            if (source_.is_time_ordered())
                std::stable_sort(raw_.begin(), raw_.end(),
                                 [](const TimelineItem& a, const TimelineItem& b) {
                                     if (a.is_pinned() != b.is_pinned())
                                         return a.is_pinned();
                                     return a.sort_date() > b.sort_date();
                                 });
            rebuild_visible();
            persist();
            if (visible && on_received_new)
                on_received_new(1, false); // notifications use the notification chime
            if (on_change)
                on_change();
            return;
        }
    }
    ingest_realtime(TimelineItem{std::move(n)});
}

void TimelineController::clear() {
    raw_.clear();
    visible_.clear();
    scrollback_cursor_.reset();
    if (source_.is_cacheable()) {
        const std::string key = cache_key();
        worker_->post([this, key] { cache_->remove(key); });
    }
    if (on_change)
        on_change();
}

bool TimelineController::toggle_mute_conversation(int visible_index,
                                                  std::function<void(bool, bool)> done) {
    if (visible_index < 0 || visible_index >= static_cast<int>(visible_.size()))
        return false;
    const std::string id = visible_[static_cast<size_t>(visible_index)].id();
    TimelineItem* raw = find_raw(id);
    if (!raw)
        return false;
    Status* s = raw->mutable_actionable_status();
    if (!s)
        return false;

    const bool want = !s->muted;
    s->muted = want; // optimistic; reverted below if the server rejects it
    rebuild_visible();
    if (on_change)
        on_change();

    const Status target = *s;
    worker_->post([this, target, want, id, done = std::move(done)]() mutable {
        const bool ok =
            want ? account_->mute_conversation(target) : account_->unmute_conversation(target);
        main_->post([this, id, want, ok, done = std::move(done)] {
            if (!ok) {
                if (TimelineItem* r = find_raw(id))
                    if (Status* st = r->mutable_actionable_status())
                        st->muted = !want;
                rebuild_visible();
                if (on_change)
                    on_change();
                if (on_error)
                    on_error(want ? "Mute conversation failed" : "Unmute conversation failed");
            }
            if (done)
                done(ok, want);
        });
    });
    return want;
}

bool TimelineController::toggle_favorite(int visible_index,
                                         std::function<void(bool, bool)> done) {
    if (visible_index < 0 || visible_index >= static_cast<int>(visible_.size()))
        return false;
    const std::string id = visible_[static_cast<size_t>(visible_index)].id();
    TimelineItem* raw = find_raw(id);
    if (!raw)
        return false;
    Status* s = raw->mutable_actionable_status();
    if (!s)
        return false;

    const bool want = !s->favourited;
    s->favourited = want;
    s->favourites_count = std::max(0, s->favourites_count + (want ? 1 : -1));
    rebuild_visible();
    if (on_change)
        on_change();

    const Status target = *s;
    worker_->post([this, target, want, id, done = std::move(done)]() mutable {
        const bool ok = want ? account_->favorite(target) : account_->unfavorite(target);
        main_->post([this, id, want, ok, done = std::move(done)] {
            if (!ok) {
                if (TimelineItem* r = find_raw(id)) {
                    if (Status* st = r->mutable_actionable_status()) {
                        st->favourited = !want;
                        st->favourites_count = std::max(0, st->favourites_count + (want ? -1 : 1));
                    }
                }
                rebuild_visible();
                if (on_change)
                    on_change();
                if (on_error)
                    on_error(want ? "Favorite failed" : "Unfavorite failed");
            }
            if (done)
                done(ok, want);
        });
    });
    return want;
}

bool TimelineController::toggle_boost(int visible_index, std::function<void(bool, bool)> done) {
    if (visible_index < 0 || visible_index >= static_cast<int>(visible_.size()))
        return false;
    const std::string id = visible_[static_cast<size_t>(visible_index)].id();
    TimelineItem* raw = find_raw(id);
    if (!raw)
        return false;
    Status* s = raw->mutable_actionable_status();
    if (!s)
        return false;

    const bool want = !s->boosted;
    s->boosted = want;
    s->boosts_count = std::max(0, s->boosts_count + (want ? 1 : -1));
    rebuild_visible();
    if (on_change)
        on_change();

    const Status target = *s;
    worker_->post([this, target, want, id, done = std::move(done)]() mutable {
        const bool ok = want ? account_->boost(target) : account_->unboost(target);
        main_->post([this, id, want, ok, done = std::move(done)] {
            if (!ok) {
                if (TimelineItem* r = find_raw(id)) {
                    if (Status* st = r->mutable_actionable_status()) {
                        st->boosted = !want;
                        st->boosts_count = std::max(0, st->boosts_count + (want ? -1 : 1));
                    }
                }
                rebuild_visible();
                if (on_change)
                    on_change();
                if (on_error)
                    on_error(want ? "Boost failed" : "Unboost failed");
            }
            if (done)
                done(ok, want);
        });
    });
    return want;
}

int TimelineController::toggle_pin_post(int visible_index, std::function<void(bool, bool)> done) {
    if (visible_index < 0 || visible_index >= static_cast<int>(visible_.size()))
        return -1;
    const std::string id = visible_[static_cast<size_t>(visible_index)].id();
    TimelineItem* raw = find_raw(id);
    if (!raw)
        return -1;
    Status* s = raw->mutable_status(); // the pin lives on the outer post
    if (!s)
        return -1;
    // Only your OWN posts can be pinned to your profile.
    if (!account_ || s->account.id != account_->me().id)
        return -1;

    const bool want = !s->pinned;
    s->pinned = want;
    rebuild_visible();
    if (on_change)
        on_change();

    const Status target = *s;
    worker_->post([this, target, want, id, done = std::move(done)]() mutable {
        const bool ok = want ? account_->pin_post(target) : account_->unpin_post(target);
        main_->post([this, id, want, ok, done = std::move(done)] {
            if (!ok) {
                if (TimelineItem* r = find_raw(id))
                    if (Status* st = r->mutable_status())
                        st->pinned = !want;
                rebuild_visible();
                if (on_change)
                    on_change();
                if (on_error)
                    on_error(want ? "Pin failed" : "Unpin failed");
            }
            if (done)
                done(ok, want);
        });
    });
    return want ? 1 : 0;
}

void TimelineController::remove_status(const std::string& id) {
    if (id.empty())
        return;
    const size_t before = raw_.size();
    // `id` is a bare status id from the server, so compare against the row's own
    // status id — never TimelineItem::id(), which is kind-prefixed and so could
    // never match. (Missing this meant an unboost, which Mastodon reports as a
    // delete of the *wrapper*, left the boost row on screen until restart.)
    raw_.erase(std::remove_if(raw_.begin(), raw_.end(),
                              [&](const TimelineItem& it) {
                                  if (const Status* outer = it.status();
                                      outer && outer->id == id)
                                      return true; // the post itself, or a boost of it
                                  const Status* s = it.actionable_status();
                                  return s && s->id == id; // boosts / notifications
                              }),
               raw_.end());
    if (raw_.size() == before)
        return; // nothing matched
    rebuild_visible();
    persist();
    if (on_change)
        on_change();
}

void TimelineController::update_status(const Status& updated) {
    bool changed = false;
    auto try_replace = [&](Status* s) {
        if (s && s->id == updated.id) {
            // A streamed edit carries no conversation fields (they come from the
            // conversations feed / its own stream event). Carry them across, or the
            // row loses the key that identifies it and both the reading position and
            // one-row-per-conversation merging break.
            const std::string convo = s->conversation_id;
            const bool unread = s->conversation_unread;
            const bool was_pinned = s->pinned;
            *s = updated;
            if (s->conversation_id.empty()) {
                s->conversation_id = convo;
                s->conversation_unread = unread;
            }
            // A streamed/edited payload doesn't say whether the post is pinned; keep
            // what we knew, or an edit silently drops it out of the pinned block.
            s->pinned = s->pinned || was_pinned;
            changed = true;
        }
    };
    for (auto& item : raw_) {
        Status* s = item.mutable_status(); // the row's status (or a notification's post)
        try_replace(s);
        if (s && s->reblog)
            try_replace(s->reblog.get()); // an edited original shown here as a boost
    }
    if (!changed)
        return;
    rebuild_visible();
    persist();
    if (on_change)
        on_change();
}

void TimelineController::set_poll(const std::string& row_id, const Poll& poll) {
    if (TimelineItem* raw = find_raw(row_id))
        if (Status* s = raw->mutable_actionable_status())
            s->poll = poll;
    rebuild_visible();
    if (on_change)
        on_change();
    persist();
}

void TimelineController::edit_post(const std::string& id, const PostDraft& draft,
                                   std::function<void(bool)> done) {
    worker_->post([this, id, draft, done = std::move(done)] {
        std::optional<Status> updated = account_->edit_post(id, draft);
        main_->post([this, updated = std::move(updated), done]() mutable {
            const bool ok = updated.has_value();
            if (ok)
                // Same replacement rules as a streamed edit: carry the conversation
                // identity across (a bare edited Status has none, and losing it costs
                // the row its id) and refresh any boost wrapper showing this post.
                update_status(*updated);
            if (done)
                done(ok);
        });
    });
}

void TimelineController::post(const PostDraft& draft, std::function<void(bool)> done) {
    const bool is_reply = draft.reply_to_id.has_value();
    worker_->post([this, draft, done = std::move(done), is_reply] {
        std::optional<Status> created = account_->post(draft);
        main_->post([this, created = std::move(created), done, is_reply]() mutable {
            const bool ok = created.has_value();
            // Only Home optimistically shows your new post: it's the one feed a
            // brand-new top-level post always belongs in, whatever its visibility.
            // Inserting into "the current tab" (as we used to) wrongly dropped it
            // into Mentions/Notifications, a hashtag, a search, a thread, etc. when
            // you happened to compose from there. Elsewhere it still arrives via
            // streaming / the next refresh.
            if (ok && !is_reply && source_.kind == TimelineSource::Kind::Home) {
                // Only add the optimistic copy if streaming hasn't already delivered
                // this exact post (dedupe by id) - otherwise your own post appears
                // twice, which is very visible with reverse timelines.
                TimelineItem item{*created};
                if (!find_raw(item.id())) {
                    raw_.insert(raw_.begin(), std::move(item));
                    rebuild_visible();
                    if (on_change)
                        on_change();
                }
            }
            if (done)
                done(ok);
        });
    });
}

} // namespace fastsm
