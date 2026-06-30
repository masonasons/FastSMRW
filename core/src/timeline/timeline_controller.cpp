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

    if (source_.is_time_ordered()) {
        std::stable_sort(raw_.begin(), raw_.end(),
                         [](const TimelineItem& a, const TimelineItem& b) {
                             return a.sort_date() > b.sort_date();
                         });
    }
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
    const std::string key = cache_key();
    worker_->post([this, key] {
        auto cached = cache_->load(key);
        main_->post([this, cached = std::move(cached)]() mutable {
            if (raw_.empty() && !cached.empty()) {
                raw_ = std::move(cached);
                rebuild_visible();
                if (on_change)
                    on_change();
            }
        });
    });
}

void TimelineController::refresh() {
    if (loading_)
        return;
    loading_ = true;
    worker_->post([this] {
        TimelinePage page = account_->items(source_, fetch_limit_, PageCursor::start());
        main_->post([this, page = std::move(page)]() mutable {
            merge_fresh(std::move(page.items), page.next_cursor);
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
