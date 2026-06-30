#include "fastsm/session/core_session.hpp"

#include "fastsm/presentation/speech_settings.hpp"
#include "fastsm/presentation/status_presenter.hpp"
#include "fastsm/store/app_config.hpp"
#include "fastsm/util/date_parsing.hpp"

#include "../store/settings_serde.hpp" // settings_to_json (internal core header)

using nlohmann::json;

namespace fastsm {

CoreSession::CoreSession(Paths paths, std::unique_ptr<net::IHttpClient> http,
                         std::function<void(const std::string&)> emit)
    : config_path_(paths.config_dir / "config.json"),
      bundled_soundpacks_(paths.bundled_soundpacks), emit_(std::move(emit)),
      http_(std::move(http)), cache_(paths.config_dir / "cache"), accounts_(http_.get()) {}

CoreSession::~CoreSession() = default;

void CoreSession::dispatch(const std::string& command_json) {
    json cmd;
    try {
        cmd = json::parse(command_json);
    } catch (...) {
        return; // ignore malformed commands
    }
    loop_.post([this, cmd = std::move(cmd)] { handle(cmd); });
}

void CoreSession::handle(const json& cmd) {
    const std::string c = cmd.value("cmd", std::string{});
    if (c == "start")
        cmd_start();
    else if (c == "get_settings")
        cmd_get_settings();
    else if (c == "select_timeline")
        cmd_select_timeline(cmd);
    else if (c == "refresh")
        cmd_refresh();
}

void CoreSession::cmd_start() {
    // Load config (settings + accounts) on the worker thread (Bluesky may need a
    // network round-trip), then rebuild + emit on the core-loop thread.
    worker_.post([this] {
        store::AppConfig config = store::AppConfigStore(config_path_).load();
        accounts_.load(config);
        loop_.post([this, config] {
            settings_ = config.settings;
            apply_settings();
            rebuild_timelines();
            emit_accounts();
            emit_timelines();
            for (int i = 0; i < static_cast<int>(timelines_.size()); ++i)
                emit_timeline(i);
        });
    });
}

void CoreSession::cmd_get_settings() {
    emit({{"event", "settings"}, {"settings", store::settings_to_json(settings_)}});
}

void CoreSession::cmd_select_timeline(const json& cmd) {
    const int n = static_cast<int>(timelines_.size());
    if (n == 0)
        return;
    int target = current_;
    if (cmd.contains("index")) {
        target = cmd.value("index", current_);
    } else if (cmd.contains("number")) {
        target = cmd.value("number", 1) - 1;
    } else if (cmd.contains("dir")) {
        const std::string d = cmd.value("dir", std::string{});
        if (d == "next")
            target = (current_ + 1) % n;
        else if (d == "prev")
            target = (current_ - 1 + n) % n;
    }
    if (target < 0 || target >= n || target == current_)
        return;
    current_ = target;
    emit_timelines(); // current changed
    if (TimelineController* tc = current())
        emit_announce(tc->source().title());
}

void CoreSession::cmd_refresh() {
    if (TimelineController* tc = current())
        tc->refresh();
}

void CoreSession::rebuild_timelines() {
    timelines_.clear();
    current_ = 0;
    if (SocialAccount* account = accounts_.selected())
        for (const TimelineSource& src : account->default_timelines())
            timelines_.push_back(make_controller(src));
    for (auto& tc : timelines_) {
        tc->load_cached(); // synchronous: shows cache instantly
        tc->refresh();
    }
}

std::unique_ptr<TimelineController> CoreSession::make_controller(const TimelineSource& src) {
    auto tc = std::make_unique<TimelineController>(accounts_.selected(), src, &cache_, &worker_,
                                                   &loop_);
    tc->set_max_refresh_pages(settings_.fetch_pages);
    TimelineController* p = tc.get();
    tc->on_change = [this, p] {
        for (int i = 0; i < static_cast<int>(timelines_.size()); ++i)
            if (timelines_[i].get() == p) {
                emit_timeline(i);
                break;
            }
    };
    tc->on_error = [this](std::string e) { emit_announce(e); };
    return tc;
}

TimelineController* CoreSession::current() const {
    if (current_ < 0 || current_ >= static_cast<int>(timelines_.size()))
        return nullptr;
    return timelines_[static_cast<size_t>(current_)].get();
}

void CoreSession::apply_settings() {
    present::SpeechConfig::set_current(settings_.speech);
    cache_.set_max_entries(settings_.cache_limit);
    for (auto& tc : timelines_)
        tc->set_max_refresh_pages(settings_.fetch_pages);
}

void CoreSession::emit(const json& event) {
    if (emit_)
        emit_(event.dump());
}

void CoreSession::emit_accounts() {
    json accts = json::array();
    for (SocialAccount* a : accounts_.accounts()) {
        accts.push_back({
            {"key", a->account_key()},
            {"handle", a->me().acct},
            {"display_name", a->me().display_name},
            {"platform", a->platform() == Platform::Mastodon ? "mastodon" : "bluesky"},
        });
    }
    emit({{"event", "accounts_changed"}, {"accounts", accts}, {"selected", accounts_.selected_key()}});
}

void CoreSession::emit_timelines() {
    json tls = json::array();
    for (auto& tc : timelines_) {
        const TimelineSource& s = tc->source();
        tls.push_back({{"title", s.title()}, {"kind", s.cache_key()},
                       {"dismissable", s.is_dismissable()}});
    }
    emit({{"event", "timelines_changed"}, {"timelines", tls}, {"current", current_}});
}

void CoreSession::emit_timeline(int index) {
    if (index < 0 || index >= static_cast<int>(timelines_.size()))
        return;
    TimelineController* tc = timelines_[static_cast<size_t>(index)].get();
    const std::int64_t now = util::now_unix();
    json rows = json::array();
    for (const auto& item : tc->items())
        rows.push_back(row_json(item, now));
    emit({{"event", "timeline_updated"},
          {"index", index},
          {"selected_id", tc->selected_id()},
          {"rows", std::move(rows)}});
}

void CoreSession::emit_announce(const std::string& message) {
    emit({{"event", "announce"}, {"message", message}});
}

json CoreSession::row_json(const TimelineItem& item, std::int64_t now) const {
    json r;
    r["id"] = item.id();
    r["text"] = present::accessibility_label(item, now);
    if (const Status* s = item.actionable_status()) {
        r["favorited"] = s->favourited;
        r["boosted"] = s->boosted;
    }
    return r;
}

} // namespace fastsm
