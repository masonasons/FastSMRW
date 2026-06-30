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

void TimelineController::set_filter(std::function<bool(const TimelineItem&)> predicate) {
    filter_ = std::move(predicate);
    rebuild_visible();
    if (on_change)
        on_change();
}

void TimelineController::rebuild_visible() {
    visible_.clear();
    if (!filter_) {
        visible_ = raw_;
        return;
    }
    visible_.reserve(raw_.size());
    for (const auto& item : raw_) {
        if (filter_(item))
            visible_.push_back(item);
    }
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
    if (raw_.empty()) {
        raw_ = std::move(fresh);
        scrollback_cursor_ = next;
    } else {
        std::unordered_set<std::string> existing;
        existing.reserve(raw_.size());
        for (const auto& it : raw_)
            existing.insert(it.id());
        // Prepend the genuinely new rows, preserving server order.
        std::vector<TimelineItem> added;
        for (auto& it : fresh) {
            if (existing.find(it.id()) == existing.end())
                added.push_back(std::move(it));
        }
        const int new_count = static_cast<int>(added.size());
        added.insert(added.end(), std::make_move_iterator(raw_.begin()),
                     std::make_move_iterator(raw_.end()));
        raw_ = std::move(added);
        if (new_count > 0 && on_received_new)
            on_received_new(new_count);
    }

    // Always keep rows newest-first by timestamp — notifications included (their
    // sort_date is the notification's created_at) — so order never depends on an
    // unbroken server/cache chain.
    std::stable_sort(raw_.begin(), raw_.end(),
                     [](const TimelineItem& a, const TimelineItem& b) {
                         return a.sort_date() > b.sort_date();
                     });
    rebuild_visible();
    persist();
}

void TimelineController::persist() {
    if (!source_.is_cacheable())
        return;
    // Snapshot on the main thread, write on the worker thread.
    auto snapshot = raw_;
    const std::string key = cache_key();
    worker_->post([this, key, snapshot = std::move(snapshot)] { cache_->save(key, snapshot); });
}

void TimelineController::load_cached() {
    // Synchronous on purpose: a fast local read that populates raw_ *before*
    // refresh() snapshots its known-id set, so the immediately-following refresh
    // does a warm gap-fill (fetch only the delta and stop at the first known
    // post) instead of a cold full refetch on every launch.
    if (!raw_.empty())
        return;
    auto cached = cache_->load(cache_key());
    if (cached.empty())
        return;
    raw_ = std::move(cached);
    rebuild_visible();
    if (on_change)
        on_change();
}

void TimelineController::refresh() {
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
        std::vector<TimelineItem> fresh;
        std::optional<PageCursor> tail; // cursor just past the fetched region
        PageCursor cursor = PageCursor::start();
        for (int page = 0; page < kMaxRefreshPages; ++page) {
            TimelinePage p = account_->items(source_, fetch_limit_, cursor);
            tail = p.next_cursor;
            if (p.items.empty())
                break;
            bool hit_known = false;
            for (auto& it : p.items) {
                if (known.find(it.id()) != known.end()) {
                    hit_known = true; // reached posts we already have
                    break;
                }
                fresh.push_back(std::move(it));
            }
            if (hit_known || !p.next_cursor || static_cast<int>(p.items.size()) < fetch_limit_)
                break;
            cursor = *p.next_cursor;
        }
        main_->post([this, fresh = std::move(fresh), tail, was_empty]() mutable {
            // On a first load (nothing known) seed the scrollback cursor; on a
            // refresh keep the existing one (it points below the current bottom).
            merge_fresh(std::move(fresh), was_empty ? tail : scrollback_cursor_);
            loading_ = false;
            if (on_change)
                on_change();
        });
    });
}

void TimelineController::load_older() {
    if (loading_ || !scrollback_cursor_)
        return;
    loading_ = true;
    const PageCursor cursor = *scrollback_cursor_;
    worker_->post([this, cursor] {
        TimelinePage page = account_->items(source_, fetch_limit_, cursor);
        main_->post([this, page = std::move(page)]() mutable {
            std::unordered_set<std::string> existing;
            for (const auto& it : raw_)
                existing.insert(it.id());
            for (auto& it : page.items) {
                if (existing.find(it.id()) == existing.end())
                    raw_.push_back(std::move(it));
            }
            scrollback_cursor_ = page.next_cursor;
            rebuild_visible();
            persist();
            loading_ = false;
            if (on_change)
                on_change();
        });
    });
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

bool TimelineController::toggle_favorite(int visible_index) {
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
    worker_->post([this, target, want, id] {
        const bool ok = want ? account_->favorite(target) : account_->unfavorite(target);
        if (!ok) {
            main_->post([this, id, want] {
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
            });
        }
    });
    return want;
}

bool TimelineController::toggle_boost(int visible_index) {
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
    worker_->post([this, target, want, id] {
        const bool ok = want ? account_->boost(target) : account_->unboost(target);
        if (!ok) {
            main_->post([this, id, want] {
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
            });
        }
    });
    return want;
}

void TimelineController::edit_post(const std::string& id, const PostDraft& draft,
                                   std::function<void(bool)> done) {
    worker_->post([this, id, draft, done = std::move(done)] {
        std::optional<Status> updated = account_->edit_post(id, draft);
        main_->post([this, updated = std::move(updated), done]() mutable {
            const bool ok = updated.has_value();
            if (ok) {
                for (auto& item : raw_) {
                    if (Status* s = item.mutable_status()) {
                        if (s->id == updated->id)
                            *s = *updated;
                    }
                }
                rebuild_visible();
                persist();
                if (on_change)
                    on_change();
            }
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
            if (ok && !is_reply) {
                raw_.insert(raw_.begin(), TimelineItem{std::move(*created)});
                rebuild_visible();
                if (on_change)
                    on_change();
            }
            if (done)
                done(ok);
        });
    });
}

} // namespace fastsm
