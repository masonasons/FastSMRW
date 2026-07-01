#include "fastsm/session/core_session.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>

#include "fastsm/auth/bluesky_auth.hpp"
#include "fastsm/fastsm.hpp"
#include "fastsm/input/keymap.hpp"
#include "fastsm/auth/mastodon_auth.hpp"
#include "fastsm/platform/bluesky/bluesky_account.hpp"
#include "fastsm/platform/mastodon/mastodon_account.hpp"
#include "fastsm/presentation/reply_helper.hpp"
#include "fastsm/presentation/speech_settings.hpp"
#include "fastsm/presentation/status_presenter.hpp"
#include "fastsm/store/app_config.hpp"
#include "fastsm/update/update_checker.hpp"
#include "fastsm/util/date_parsing.hpp"

#include "fastsm/store/settings_json.hpp"

using nlohmann::json;

namespace fastsm {
namespace {

// The GitHub repo the in-app updater checks.
constexpr const char* kUpdateRepo = "masonasons/FastSMRW";

PostDraft draft_from_json(const json& d) {
    PostDraft draft;
    draft.text = d.value("text", std::string{});
    if (d.contains("reply_to_id"))
        draft.reply_to_id = d.value("reply_to_id", std::string{});
    if (d.contains("reply_to_url"))
        draft.reply_to_url = d.value("reply_to_url", std::string{});
    if (d.contains("quoted_status_id"))
        draft.quoted_status_id = d.value("quoted_status_id", std::string{});
    if (d.contains("spoiler_text"))
        draft.spoiler_text = d.value("spoiler_text", std::string{});
    if (d.contains("language"))
        draft.language = d.value("language", std::string{});
    if (d.contains("visibility"))
        draft.visibility = static_cast<Visibility>(d.value("visibility", 0));
    if (d.contains("scheduled_at"))
        draft.scheduled_at = d.value("scheduled_at", std::int64_t{0});
    if (auto p = d.find("poll"); p != d.end() && p->is_object()) {
        PollDraft poll;
        if (auto opts = p->find("options"); opts != p->end() && opts->is_array())
            for (const auto& o : *opts)
                poll.options.push_back(o.get<std::string>());
        poll.multiple = p->value("multiple", false);
        poll.expires_in_seconds = p->value("expires_in_seconds", 86400);
        draft.poll = std::move(poll);
    }
    return draft;
}

// Title-case label for a user action (batch announce).
std::string user_action_label(const std::string& action) {
    if (action == "follow")
        return "Follow";
    if (action == "unfollow")
        return "Unfollow";
    if (action == "mute")
        return "Mute";
    if (action == "unmute")
        return "Unmute";
    if (action == "block")
        return "Block";
    if (action == "unblock")
        return "Unblock";
    return "Action";
}

// Spoken confirmation for a relationship action.
std::string relationship_message(const std::string& action, const std::string& handle) {
    const std::string at = handle.empty() ? "user" : "@" + handle;
    if (action == "follow")
        return "Following " + at;
    if (action == "unfollow")
        return "Unfollowed " + at;
    if (action == "mute")
        return "Muted " + at;
    if (action == "unmute")
        return "Unmuted " + at;
    if (action == "block")
        return "Blocked " + at;
    if (action == "unblock")
        return "Unblocked " + at;
    if (action == "show_boosts")
        return "Showing boosts from " + at;
    if (action == "hide_boosts")
        return "Hiding boosts from " + at;
    return "Done";
}

json features_json(const PlatformFeatures& f) {
    return {{"visibility", f.visibility},     {"content_warning", f.content_warning},
            {"quote_posts", f.quote_posts},   {"polls", f.polls},
            {"editing", f.editing},           {"scheduling", f.scheduling}};
}

// Stable name for a timeline kind, for persisting the set of open timelines.
const char* kind_name(TimelineSource::Kind k) {
    using K = TimelineSource::Kind;
    switch (k) {
    case K::Home: return "home";
    case K::Notifications: return "notifications";
    case K::Mentions: return "mentions";
    case K::Local: return "local";
    case K::Federated: return "federated";
    case K::Bookmarks: return "bookmarks";
    case K::Favorites: return "favorites";
    case K::Thread: return "thread";
    case K::UserPosts: return "userPosts";
    case K::Followers: return "followers";
    case K::Following: return "following";
    case K::Hashtag: return "hashtag";
    case K::SearchPosts: return "searchPosts";
    case K::SearchPeople: return "searchPeople";
    case K::RemoteLocal: return "remoteLocal";
    case K::RemoteUser: return "remoteUser";
    }
    return "home";
}

std::optional<TimelineSource::Kind> kind_from_name(const std::string& s) {
    using K = TimelineSource::Kind;
    if (s == "home") return K::Home;
    if (s == "notifications") return K::Notifications;
    if (s == "mentions") return K::Mentions;
    if (s == "local") return K::Local;
    if (s == "federated") return K::Federated;
    if (s == "bookmarks") return K::Bookmarks;
    if (s == "favorites") return K::Favorites;
    if (s == "thread") return K::Thread;
    if (s == "userPosts") return K::UserPosts;
    if (s == "followers") return K::Followers;
    if (s == "following") return K::Following;
    if (s == "hashtag") return K::Hashtag;
    if (s == "searchPosts") return K::SearchPosts;
    if (s == "searchPeople") return K::SearchPeople;
    if (s == "remoteLocal") return K::RemoteLocal;
    if (s == "remoteUser") return K::RemoteUser;
    return std::nullopt;
}

json source_to_json(const TimelineSource& s) {
    return {{"kind", kind_name(s.kind)}, {"param", s.param}, {"title", s.title_text}};
}

std::optional<TimelineSource> source_from_json(const json& j) {
    auto k = kind_from_name(j.value("kind", std::string{}));
    if (!k)
        return std::nullopt;
    TimelineSource s;
    s.kind = *k;
    s.param = j.value("param", std::string{});
    s.title_text = j.value("title", std::string{});
    return s;
}

} // namespace

CoreSession::CoreSession(Paths paths, std::unique_ptr<net::IHttpClient> http,
                         std::function<void(const std::string&)> emit)
    : config_path_(paths.config_dir / "config.json"),
      bundled_keymaps_dir_(paths.bundled_keymaps), emit_(std::move(emit)), http_(std::move(http)),
      cache_(paths.config_dir / "cache"), accounts_(http_.get()) {
    sound_.set_bundled_packs_dir(paths.bundled_soundpacks);
    sound_.set_user_packs_dir(paths.config_dir / "soundpacks");
    auto_refresh_thread_ = std::thread([this] { auto_refresh_loop(); });
}

CoreSession::~CoreSession() {
    auto_refresh_running_.store(false);
    if (auto_refresh_thread_.joinable())
        auto_refresh_thread_.join();
    streams_.clear(); // join all streaming threads while http_/loop_ are still alive
}

void CoreSession::dispatch(const std::string& command_json) {
    json cmd;
    try {
        cmd = json::parse(command_json);
    } catch (...) {
        return;
    }
    loop_.post([this, cmd = std::move(cmd)] { handle(cmd); });
}

void CoreSession::handle(const json& cmd) {
    const std::string c = cmd.value("cmd", std::string{});
    if (c == "start")
        cmd_start();
    else if (c == "get_settings")
        cmd_get_settings();
    else if (c == "update_settings")
        cmd_update_settings(cmd);
    else if (c == "select_timeline")
        cmd_select_timeline(cmd);
    else if (c == "select_account")
        cmd_select_account(cmd);
    else if (c == "refresh")
        cmd_refresh();
    else if (c == "refresh_all")
        cmd_refresh_all();
    else if (c == "load_older")
        cmd_load_older();
    else if (c == "load_gap")
        cmd_load_gap(cmd);
    else if (c == "note_selection")
        cmd_note_selection(cmd);
    else if (c == "toggle_boost")
        cmd_toggle_boost(cmd);
    else if (c == "toggle_favorite")
        cmd_toggle_favorite(cmd);
    else if (c == "post")
        cmd_post(cmd);
    else if (c == "compose_context")
        cmd_compose_context(cmd);
    else if (c == "open_status")
        cmd_open_status(cmd);
    else if (c == "post_info")
        cmd_post_info(cmd);
    else if (c == "move")
        cmd_move(cmd);
    else if (c == "cycle_movement")
        cmd_cycle_movement(cmd);
    else if (c == "go_back")
        cmd_go_back();
    else if (c == "get_spawnable")
        cmd_get_spawnable();
    else if (c == "spawn_timeline")
        cmd_spawn_timeline(cmd);
    else if (c == "open_thread")
        cmd_open_thread(cmd);
    else if (c == "open_user_timeline")
        cmd_open_user_timeline(cmd);
    else if (c == "open_user_profile")
        cmd_open_user_profile(cmd);
    else if (c == "set_relationship")
        cmd_set_relationship(cmd);
    else if (c == "open_followers")
        cmd_open_followers(cmd);
    else if (c == "open_following")
        cmd_open_following(cmd);
    else if (c == "user_action")
        cmd_user_action(cmd);
    else if (c == "close_timeline")
        cmd_close_timeline();
    else if (c == "clear_timeline")
        cmd_clear_timeline();
    else if (c == "clear_all_timelines")
        cmd_clear_all_timelines();
    else if (c == "add_account")
        cmd_add_account(cmd);
    else if (c == "remove_account")
        cmd_remove_account(cmd);
    else if (c == "play_earcon")
        cmd_play_earcon(cmd);
    else if (c == "get_action_catalog")
        cmd_get_action_catalog();
    else if (c == "get_keymap")
        cmd_get_keymap(cmd);
    else if (c == "set_active_keymap")
        cmd_set_active_keymap(cmd);
    else if (c == "save_keymap")
        cmd_save_keymap(cmd);
    else if (c == "delete_keymap")
        cmd_delete_keymap(cmd);
    else if (c == "perform_action")
        cmd_perform_action(cmd);
    else if (c == "set_window_shown")
        cmd_set_window_shown(cmd);
    else if (c == "get_layer_keymap")
        cmd_get_layer_keymap();
    else if (c == "check_for_update")
        cmd_check_for_update(cmd);
    else if (c == "download_update")
        cmd_download_update(cmd);
}

// --- lifecycle / accounts ---

void CoreSession::cmd_start() {
    worker_.post([this] {
        store::AppConfig config = store::AppConfigStore(config_path_).load();
        accounts_.load(config); // Bluesky may re-establish a session here
        loop_.post([this, config] {
            settings_ = config.settings;
            apply_settings();
            load_positions(); // remembered reading positions (before timelines build)
            rebuild_timelines();
            emit_accounts();
            emit_timelines();
            emit_settings();
            if (accounts_.selected())
                sound_.play(sound::Earcon::Refresh); // "ready" chime — startup only
            else
                emit_announce("No account yet. Use Add Account (Ctrl+Shift+A).");
        });
    });
}

void CoreSession::cmd_add_account(const json& cmd) {
    const std::string platform = cmd.value("platform", std::string{});
    if (platform == "mastodon") {
        const std::string instance = cmd.value("instance", std::string{});
        emit_announce("Authorizing in your browser…");
        worker_.post([this, instance] {
            MastodonAuth auth(http_.get());
            auto open_browser = [this](const std::string& url) {
                emit({{"event", "open_url"}, {"url", url}});
            };
            MastodonLoginResult r = auth.login(instance, open_browser);
            loop_.post([this, r = std::move(r)]() mutable {
                if (r.ok) {
                    store::StoredCredential cred;
                    cred.mastodon = r.credentials;
                    auto account = std::make_unique<MastodonAccount>(r.credentials, r.me, http_.get());
                    const std::string key = account->account_key();
                    SocialAccount* acct_ptr = account.get();
                    accounts_.add(std::move(account), cred);
                    worker_.post([acct_ptr] { acct_ptr->load_configuration(); }); // real char limit
                    switch_account(key); // parks the old account, builds the new one
                    save_config();
                    emit_accounts();
                    emit_timelines();
                    emit_all_timelines();
                }
                emit({{"event", "auth_result"}, {"ok", r.ok}, {"error", r.error}});
            });
        });
    } else if (platform == "bluesky") {
        const std::string service = cmd.value("service", std::string{});
        const std::string handle = cmd.value("handle", std::string{});
        const std::string app_password = cmd.value("app_password", std::string{});
        emit_announce("Signing in…");
        worker_.post([this, service, handle, app_password] {
            BlueskyAuth auth(http_.get());
            BlueskyLoginResult r = auth.login(service, handle, app_password);
            loop_.post([this, r = std::move(r)]() mutable {
                if (r.ok) {
                    store::StoredCredential cred;
                    cred.bluesky = r.credentials;
                    auto account =
                        std::make_unique<BlueskyAccount>(r.credentials, r.session, r.me, http_.get());
                    const std::string key = account->account_key();
                    accounts_.add(std::move(account), cred);
                    switch_account(key); // parks the old account, builds the new one
                    save_config();
                    emit_accounts();
                    emit_timelines();
                    emit_all_timelines();
                }
                emit({{"event", "auth_result"}, {"ok", r.ok}, {"error", r.error}});
            });
        });
    }
}

void CoreSession::cmd_remove_account(const json& cmd) {
    const std::string removed = cmd.value("key", std::string{});
    const bool was_selected = removed == accounts_.selected_key();
    accounts_.remove(removed); // may auto-select the first remaining account
    save_config();
    parked_.erase(removed);
    if (was_selected) {
        timelines_.clear(); // drop the removed account's (displayed) timelines
        current_ = 0;
        const std::string now = accounts_.selected_key();
        if (auto it = parked_.find(now); it != parked_.end()) {
            timelines_ = std::move(it->second);
            parked_.erase(it);
        } else if (SocialAccount* a = accounts_.selected()) {
            timelines_ = build_timelines_for(a, a->default_timelines());
        }
    }
    save_open_timelines();
    update_streaming(); // start/stop streams for the new account set
    emit_accounts();
    emit_timelines();
    emit_all_timelines();
}

void CoreSession::cmd_select_account(const json& cmd) {
    auto accts = accounts_.accounts();
    if (accts.size() < 2)
        return;
    int idx = 0;
    for (size_t i = 0; i < accts.size(); ++i)
        if (accts[i]->account_key() == accounts_.selected_key())
            idx = static_cast<int>(i);
    const int n = static_cast<int>(accts.size());
    const std::string dir = cmd.value("dir", std::string{});
    const int target = dir == "prev" ? (idx - 1 + n) % n : (idx + 1) % n;
    switch_account(accts[static_cast<size_t>(target)]->account_key()); // swap, don't rebuild
    sound_.play(sound::Earcon::Navigate);
    emit_accounts();
    emit_timelines();
    emit_all_timelines(); // push the warm rows + remembered position for the new account
    save_config();        // remember which account is selected across restarts
}

// --- settings ---

void CoreSession::cmd_get_settings() { emit_settings(); }

void CoreSession::cmd_update_settings(const json& cmd) {
    settings_ = store::settings_from_json(cmd.value("settings", json::object()));
    apply_settings();
    save_config();
    emit_settings();
    emit_all_timelines(); // a speech-order change re-renders every row
}

// --- timelines ---

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
        sound_.play(sound::Earcon::Navigate);
    }
    if (target < 0 || target >= n || target == current_)
        return;
    current_ = target;
    emit_timelines();
    if (TimelineController* tc = current()) {
        std::string msg = tc->source().title();
        if (cmd.value("speak_position", false)) // invisible interface: add "n of count"
            msg += ", " + timeline_position_text(tc);
        emit_announce(msg);
    }
}

std::string CoreSession::timeline_position_text(const TimelineController* tc) const {
    const auto& items = tc->items();
    if (items.empty())
        return "empty";
    int idx = tc->visible_index_of(tc->selected_id());
    if (idx < 0)
        idx = 0;
    return std::to_string(idx + 1) + " of " + std::to_string(items.size());
}

void CoreSession::cmd_refresh() {
    if (TimelineController* tc = current())
        tc->refresh();
}

void CoreSession::cmd_refresh_all() {
    for (auto& tc : timelines_)
        tc->refresh();
}

void CoreSession::cmd_load_gap(const json& cmd) {
    if (TimelineController* tc = current())
        tc->load_gap(cmd.value("id", std::string{}));
}

void CoreSession::cmd_load_older() {
    if (TimelineController* tc = current())
        tc->load_older();
}

void CoreSession::cmd_note_selection(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const std::string id = cmd.value("id", std::string{});
    tc->note_selection(id);
    // Remember the reading position for this timeline across restarts.
    if (!id.empty() && positions_[tc->cache_key()] != id) {
        positions_[tc->cache_key()] = id;
        save_positions();
    }
}

void CoreSession::cmd_get_spawnable() {
    json tls = json::array();
    if (SocialAccount* account = accounts_.selected()) {
        for (const auto& src : account->spawnable_timelines()) {
            bool open = false;
            for (auto& tc : timelines_)
                if (tc->source().cache_key() == src.cache_key()) {
                    open = true;
                    break;
                }
            if (!open)
                tls.push_back({{"kind", src.cache_key()}, {"title", src.title()}});
        }
        // Parameterized timelines: an "input" label tells the UI to prompt for a value.
        if (account->platform() == Platform::Mastodon) {
            tls.push_back({{"kind", "hashtag"}, {"title", "Hashtag"}, {"input", "Hashtag"}});
            tls.push_back({{"kind", "search_posts"}, {"title", "Search Posts"}, {"input", "Search"}});
            tls.push_back(
                {{"kind", "search_people"}, {"title", "Search People"}, {"input", "Search"}});
            tls.push_back({{"kind", "remote_local"},
                           {"title", "Remote Instance Timeline"},
                           {"input", "Instance (e.g. mastodon.social)"}});
            tls.push_back({{"kind", "remote_user"},
                           {"title", "Remote User Timeline"},
                           {"input", "User (e.g. name@instance.social)"}});
        }
    }
    emit({{"event", "spawnable_timelines"}, {"timelines", tls}});
}

void CoreSession::spawn_source(const TimelineSource& src) {
    // Remember where we came from, so closing this timeline returns there.
    std::string origin;
    if (current_ >= 0 && current_ < static_cast<int>(timelines_.size()))
        origin = timelines_[static_cast<size_t>(current_)]->source().cache_key();
    for (size_t i = 0; i < timelines_.size(); ++i) {
        if (timelines_[i]->source().cache_key() == src.cache_key()) {
            current_ = static_cast<int>(i); // already open -> focus it (keep its origin)
            emit_timelines();
            return;
        }
    }
    timelines_.push_back(make_controller(accounts_.selected(), src));
    current_ = static_cast<int>(timelines_.size()) - 1;
    TimelineController* p = timelines_.back().get();
    p->set_origin_key(origin);
    // Restore this timeline's remembered position (works for spawned kinds too).
    if (auto it = positions_.find(p->cache_key()); it != positions_.end())
        p->note_selection(it->second);
    emit_timelines();
    p->load_cached();
    p->refresh();
    save_open_timelines(); // remember this timeline is now open
    update_streaming();
}

void CoreSession::cmd_spawn_timeline(const json& cmd) {
    if (!accounts_.selected())
        return;
    const std::string kind = cmd.value("kind", std::string{});
    // Parameterized templates from the New Timeline dialog (a hashtag or a search).
    if (kind == "hashtag" || kind == "search_posts" || kind == "search_people") {
        std::string value = cmd.value("value", std::string{});
        const size_t b = value.find_first_not_of(" \t#"); // trim spaces + a leading '#'
        const size_t e = value.find_last_not_of(" \t");
        value = (b == std::string::npos) ? std::string{} : value.substr(b, e - b + 1);
        if (value.empty())
            return;
        if (kind == "hashtag") {
            for (char& ch : value)
                if (ch >= 'A' && ch <= 'Z')
                    ch = static_cast<char>(ch - 'A' + 'a'); // case-insensitive cache key
            spawn_source(TimelineSource::hashtag(value));
        } else if (kind == "search_posts") {
            spawn_source(TimelineSource::search_posts(value));
        } else {
            spawn_source(TimelineSource::search_people(value));
        }
        return;
    }
    // Remote timelines: a bare instance domain, or a "user@instance" handle.
    if (kind == "remote_local" || kind == "remote_user") {
        auto lc = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
        std::string value = cmd.value("value", std::string{});
        const size_t b = value.find_first_not_of(" \t");
        const size_t e = value.find_last_not_of(" \t");
        value = (b == std::string::npos) ? std::string{} : value.substr(b, e - b + 1);
        if (!value.empty() && value[0] == '@')
            value.erase(0, 1); // drop a leading '@'
        for (const std::string p : {"https://", "http://"})
            if (value.rfind(p, 0) == 0) {
                value.erase(0, p.size());
                break;
            }
        while (!value.empty() && value.back() == '/')
            value.pop_back();
        if (value.empty())
            return;
        if (kind == "remote_local") {
            for (char& ch : value)
                ch = lc(ch); // domain is case-insensitive
            spawn_source(TimelineSource::remote_local(value));
        } else {
            const size_t at = value.find('@');
            if (at == std::string::npos || at == 0 || at + 1 >= value.size())
                return; // need "user@instance"
            for (size_t i = at + 1; i < value.size(); ++i)
                value[i] = lc(value[i]); // lowercase the domain, keep the username
            spawn_source(TimelineSource::remote_user(value));
        }
        return;
    }
    if (auto src = source_from_kind(kind))
        spawn_source(*src);
}

void CoreSession::cmd_open_thread(const json& cmd) {
    TimelineController* tc = current();
    const std::string row_id = cmd.value("id", std::string{});
    if (!accounts_.selected() || !tc || row_id.empty())
        return;
    // Use the underlying post (for a boost, the original) as the thread root.
    std::string status_id = row_id;
    std::string title = "Thread";
    if (const TimelineItem* item = find_item(tc, row_id))
        if (const Status* s = item->actionable_status()) {
            status_id = s->id;
            const std::string& name =
                s->account.display_name.empty() ? s->account.acct : s->account.display_name;
            if (!name.empty())
                title = "Thread: " + name;
        }
    spawn_source(TimelineSource::thread(status_id, title));
}

std::vector<User> CoreSession::users_in_post(const TimelineItem& item) const {
    std::vector<User> users;
    if (const User* row_user = item.user()) { // a user-list row is just that user
        users.push_back(*row_user);
        return users;
    }
    const Status* outer = item.status();
    if (!outer)
        return users;
    const Platform plat = outer->account.platform;
    auto contains = [&](const std::string& id) {
        for (const auto& u : users)
            if (u.id == id)
                return true;
        return false;
    };
    auto add_user = [&](const User& u) {
        if (!u.id.empty() && !contains(u.id))
            users.push_back(u);
    };
    auto add_mention = [&](const Mention& m) {
        if (m.id.empty() || contains(m.id))
            return;
        User u;
        u.id = m.id;
        u.acct = m.acct;
        u.username = m.username;
        u.display_name = m.username; // a mention carries no display name
        u.url = m.url;
        u.platform = plat;
        users.push_back(u);
    };
    auto add_status = [&](const Status& s) {
        add_user(s.account);
        for (const auto& m : s.mentions)
            add_mention(m);
        if (s.quote) {
            add_user(s.quote->account);
            for (const auto& m : s.quote->mentions)
                add_mention(m);
        }
    };
    add_status(*outer);            // booster/quoter + its mentions
    if (outer->reblog)             // boosted author + its mentions
        add_status(*outer->reblog);
    return users;
}

void CoreSession::emit_user_profile(const User& u) {
    SocialAccount* acct = accounts_.selected();
    const User user = u;
    const std::string handle = u.acct.empty() ? u.username : u.acct;
    const bool can_hide_boosts = acct && acct->features().hide_boosts;
    // Off-thread: enrich a sparse row (Bluesky) via fetch_profile, compose the
    // text, and fetch the relationship, then emit. Profiling a mention still
    // works (everything is keyed by id).
    worker_.post([this, acct, user, handle, can_hide_boosts] {
        User full = user;
        if (acct && !user.id.empty())
            if (auto p = acct->fetch_profile(user.id))
                full = *p;
        const std::string text = present::user_profile(full);
        std::optional<Relationship> rel;
        if (acct && !user.id.empty())
            rel = acct->relationship(user.id);
        loop_.post([this, full, text, handle, rel, can_hide_boosts] {
            json e = {{"event", "user_profile"},
                      {"text", text},
                      {"account_id", full.id},
                      {"acct", handle},
                      {"url", full.url},
                      {"has_relationship", rel.has_value()},
                      {"can_hide_boosts", can_hide_boosts}};
            if (rel) {
                e["following"] = rel->following;
                e["muting"] = rel->muting;
                e["blocking"] = rel->blocking;
                e["requested"] = rel->requested;
                e["showing_reblogs"] = rel->showing_reblogs;
            }
            emit(e);
        });
    });
}

void CoreSession::cmd_set_relationship(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    if (!acct)
        return;
    const std::string id = cmd.value("account_id", std::string{});
    const std::string action = cmd.value("action", std::string{});
    const std::string handle = cmd.value("acct", std::string{});
    if (id.empty() || action.empty())
        return;
    worker_.post([this, acct, id, action, handle] {
        bool ok = false;
        if (action == "follow")
            ok = acct->follow(id);
        else if (action == "unfollow")
            ok = acct->unfollow(id);
        else if (action == "mute")
            ok = acct->mute(id);
        else if (action == "unmute")
            ok = acct->unmute(id);
        else if (action == "block")
            ok = acct->block(id);
        else if (action == "unblock")
            ok = acct->unblock(id);
        else if (action == "show_boosts")
            ok = acct->set_show_boosts(id, true);
        else if (action == "hide_boosts")
            ok = acct->set_show_boosts(id, false);
        loop_.post([this, ok, action, handle] {
            if (!ok) {
                sound_.play(sound::Earcon::Error);
                emit_announce("Action failed");
                return;
            }
            emit_announce(relationship_message(action, handle));
        });
    });
}

void CoreSession::open_user_list(const json& cmd, bool following) {
    if (!accounts_.selected())
        return;
    std::string id = cmd.value("account_id", std::string{});
    std::string handle = cmd.value("acct", std::string{});
    if (id.empty()) { // resolve from the selected row (a user row or a post author)
        TimelineController* tc = current();
        const TimelineItem* item = tc ? find_item(tc, cmd.value("id", std::string{})) : nullptr;
        if (item) {
            const std::vector<User> users = users_in_post(*item);
            if (!users.empty()) {
                id = users.front().id;
                handle = users.front().acct.empty() ? users.front().username : users.front().acct;
            }
        }
    }
    if (id.empty())
        return;
    if (following)
        spawn_source(TimelineSource::following(id, "Following: @" + handle));
    else
        spawn_source(TimelineSource::followers(id, "Followers: @" + handle));
}

void CoreSession::cmd_open_followers(const json& cmd) { open_user_list(cmd, false); }
void CoreSession::cmd_open_following(const json& cmd) { open_user_list(cmd, true); }

void CoreSession::cmd_user_action(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    TimelineController* tc = current();
    const std::string action = cmd.value("action", std::string{});
    if (!acct || !tc || action.empty() || !cmd.contains("ids") || !cmd["ids"].is_array())
        return;
    // Resolve the selected user-row ids to account ids.
    std::vector<std::string> account_ids;
    for (const auto& rid : cmd["ids"]) {
        if (!rid.is_string())
            continue;
        if (const TimelineItem* item = find_item(tc, rid.get<std::string>()))
            if (const User* u = item->user())
                account_ids.push_back(u->id);
    }
    if (account_ids.empty())
        return;
    worker_.post([this, acct, account_ids, action] {
        int failures = 0;
        for (const auto& id : account_ids) {
            bool ok = false;
            if (action == "follow")
                ok = acct->follow(id);
            else if (action == "unfollow")
                ok = acct->unfollow(id);
            else if (action == "mute")
                ok = acct->mute(id);
            else if (action == "unmute")
                ok = acct->unmute(id);
            else if (action == "block")
                ok = acct->block(id);
            else if (action == "unblock")
                ok = acct->unblock(id);
            if (!ok)
                ++failures;
        }
        const int total = static_cast<int>(account_ids.size());
        loop_.post([this, action, failures, total] {
            const std::string noun = total == 1 ? " user" : " users";
            if (failures > 0) {
                sound_.play(sound::Earcon::Error);
                emit_announce(user_action_label(action) + " failed for " +
                              std::to_string(failures) + " of " + std::to_string(total) + noun);
            } else {
                emit_announce(user_action_label(action) + ": " + std::to_string(total) + noun);
            }
        });
    });
}

void CoreSession::emit_user_picker(const std::string& purpose, const std::string& row_id,
                                   const std::vector<User>& users) {
    json arr = json::array();
    for (const auto& u : users)
        arr.push_back({{"id", u.id}, {"acct", u.acct.empty() ? u.username : u.acct}});
    emit({{"event", "user_picker"},
          {"purpose", purpose},
          {"id", row_id},
          {"users", std::move(arr)}});
}

void CoreSession::cmd_open_user_timeline(const json& cmd) {
    if (!accounts_.selected())
        return;
    // A specific user was chosen (from the picker menu or the profile dialog).
    if (cmd.contains("account_id")) {
        const std::string aid = cmd.value("account_id", std::string{});
        if (!aid.empty())
            spawn_source(TimelineSource::user_posts(aid, "@" + cmd.value("acct", std::string{})));
        return;
    }
    TimelineController* tc = current();
    const std::string row_id = cmd.value("id", std::string{});
    const TimelineItem* item = tc ? find_item(tc, row_id) : nullptr;
    if (!item)
        return;
    const std::vector<User> users = users_in_post(*item);
    if (users.empty())
        return;
    if (users.size() == 1) {
        const User& u = users.front();
        spawn_source(
            TimelineSource::user_posts(u.id, "@" + (u.acct.empty() ? u.username : u.acct)));
        return;
    }
    emit_user_picker("timeline", row_id, users); // let the UI pick which user
}

void CoreSession::cmd_open_user_profile(const json& cmd) {
    if (!accounts_.selected())
        return;
    TimelineController* tc = current();
    const std::string row_id = cmd.value("id", std::string{});
    const TimelineItem* item = tc ? find_item(tc, row_id) : nullptr;
    if (!item)
        return;
    const std::vector<User> users = users_in_post(*item);
    if (users.empty())
        return;
    // A specific user was chosen from the picker menu.
    if (cmd.contains("account_id")) {
        const std::string aid = cmd.value("account_id", std::string{});
        for (const User& u : users)
            if (u.id == aid) {
                emit_user_profile(u);
                return;
            }
        return;
    }
    if (users.size() == 1) {
        emit_user_profile(users.front());
        return;
    }
    emit_user_picker("profile", row_id, users);
}

void CoreSession::cmd_close_timeline() {
    TimelineController* tc = current();
    if (!tc || !tc->source().is_dismissable())
        return;
    const std::string origin = tc->origin_key();
    const std::string closed_key = tc->cache_key();
    const int closed_index = current_;
    tc->on_change = nullptr;
    tc->on_error = nullptr;
    tc->on_received_new = nullptr;
    tc->clear(); // drops this timeline's cache file (if it was cacheable)
    retired_.push_back(std::move(timelines_[static_cast<size_t>(closed_index)]));
    timelines_.erase(timelines_.begin() + closed_index);
    // Forget the closed timeline's remembered reading position.
    if (positions_.erase(closed_key) > 0)
        save_positions();
    save_open_timelines(); // remember it's no longer open

    // Return to the timeline we came from, if it's still open; else a neighbor.
    int next = -1;
    if (!origin.empty())
        for (size_t i = 0; i < timelines_.size(); ++i)
            if (timelines_[i]->source().cache_key() == origin) {
                next = static_cast<int>(i);
                break;
            }
    if (next < 0) {
        next = closed_index; // the item now at the closed index, clamped
        if (next >= static_cast<int>(timelines_.size()))
            next = static_cast<int>(timelines_.size()) - 1;
        if (next < 0)
            next = 0;
    }
    current_ = next;
    sound_.play(sound::Earcon::Close);
    emit_timelines();
    emit_all_timelines();
}

void CoreSession::cmd_clear_timeline() {
    if (TimelineController* tc = current()) {
        tc->clear();
        sound_.play(sound::Earcon::Delete);
    }
}

void CoreSession::cmd_clear_all_timelines() {
    if (timelines_.empty())
        return;
    for (auto& tc : timelines_)
        tc->clear(); // each fires on_change -> emit_timeline
    sound_.play(sound::Earcon::Delete);
}

// --- posting / actions ---

void CoreSession::cmd_toggle_boost(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const int idx = tc->visible_index_of(cmd.value("id", std::string{}));
    if (idx < 0)
        return;
    if (tc->toggle_boost(idx))
        sound_.play(sound::Earcon::Boost);
}

void CoreSession::cmd_toggle_favorite(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const int idx = tc->visible_index_of(cmd.value("id", std::string{}));
    if (idx < 0)
        return;
    const bool now = tc->toggle_favorite(idx);
    sound_.play(now ? sound::Earcon::Favorite : sound::Earcon::Unfavorite);
}

void CoreSession::cmd_post(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    PostDraft draft = draft_from_json(cmd.value("draft", json::object()));
    const std::string edit_id = cmd.value("edit_id", std::string{});
    auto done = [this](bool ok) {
        sound_.play(ok ? sound::Earcon::PostSent : sound::Earcon::Error);
        emit({{"event", "post_result"}, {"ok", ok}});
    };
    if (!edit_id.empty())
        tc->edit_post(edit_id, draft, done);
    else
        tc->post(draft, done);
}

void CoreSession::cmd_compose_context(const json& cmd) {
    TimelineController* tc = current();
    if (!tc || !tc->account())
        return;
    SocialAccount* account = tc->account();
    const std::string mode = cmd.value("mode", std::string("new"));
    const std::string id = cmd.value("id", std::string{});

    json ctx;
    ctx["event"] = "compose_context";
    ctx["mode"] = mode;
    ctx["max_chars"] = account->max_chars();
    ctx["enter_to_send"] = settings_.enter_to_send;
    ctx["features"] = features_json(account->features());
    ctx["platform"] = account->platform() == Platform::Mastodon ? "mastodon" : "bluesky";

    const Status* target = nullptr;
    if (!id.empty())
        if (const TimelineItem* item = find_item(tc, id))
            target = item->actionable_status();

    if (mode == "reply" && target) {
        ctx["title"] = "Reply";
        ctx["context_label"] = "Replying to " + target->account.best_name() + ": " + target->text;
        if (account->platform() == Platform::Mastodon)
            ctx["prefill_text"] = present::mention_prefix(*target, account->me());
        if (target->visibility)
            ctx["default_visibility"] = static_cast<int>(*target->visibility);
        if (target->spoiler_text)
            ctx["prefill_cw"] = *target->spoiler_text;
        ctx["reply_to_id"] = target->id;
        // A remote post: pass its canonical URL so the reply resolves to a local copy.
        if (target->instance_url && !target->url.empty())
            ctx["reply_to_url"] = target->url;
    } else if (mode == "quote" && target) {
        ctx["title"] = "Quote Post";
        ctx["context_label"] = "Quoting " + target->account.best_name() + ": " + target->text;
        ctx["quoted_status_id"] = target->id;
    } else if (mode == "edit" && target) {
        ctx["title"] = "Edit Post";
        ctx["prefill_text"] = target->text;
        if (target->spoiler_text)
            ctx["prefill_cw"] = *target->spoiler_text;
        ctx["edit_id"] = target->id;
    } else {
        ctx["title"] = "New Post";
    }
    emit(ctx);
}

void CoreSession::cmd_open_status(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    if (const TimelineItem* item = find_item(tc, cmd.value("id", std::string{})))
        if (const Status* s = item->actionable_status())
            if (!s->url.empty())
                emit({{"event", "open_url"}, {"url", s->url}});
}

void CoreSession::cmd_post_info(const json& cmd) {
    TimelineController* tc = current();
    if (!tc || !tc->account())
        return;
    const TimelineItem* item = find_item(tc, cmd.value("id", std::string{}));
    if (!item)
        return;
    const Status* s = item->actionable_status();
    if (!s)
        return;
    emit({{"event", "post_info"},
          {"id", cmd.value("id", std::string{})},
          {"text", present::post_info(*s, util::now_unix())},
          {"features", features_json(tc->account()->features())},
          {"has_url", !s->url.empty()}});
}

void CoreSession::cmd_move(const json& cmd) {
    TimelineController* tc = current();
    if (!tc || movement_units_.empty())
        return;
    const int idx = tc->visible_index_of(cmd.value("from_id", std::string{}));
    if (idx < 0)
        return;
    if (movement_unit_ < 0 || movement_unit_ >= static_cast<int>(movement_units_.size()))
        movement_unit_ = 0;
    const bool down = cmd.value("dir", std::string("next")) != "prev";
    const int dest =
        movement::destination(tc->items(), idx, movement_units_[static_cast<size_t>(movement_unit_)],
                              down);
    if (dest < 0) {
        sound_.play(sound::Earcon::Boundary); // nowhere to jump for this unit
        return;
    }
    const std::string id = tc->items()[static_cast<size_t>(dest)].id();
    tc->note_selection(id); // records the jump for Go Back
    emit({{"event", "select_row"}, {"id", id}});
}

void CoreSession::cmd_cycle_movement(const json& cmd) {
    const int n = static_cast<int>(movement_units_.size());
    if (n == 0) {
        emit_announce("No movement units configured");
        return;
    }
    const int delta = cmd.value("dir", std::string("next")) == "prev" ? -1 : 1;
    movement_unit_ = ((movement_unit_ + delta) % n + n) % n;
    emit_announce("Move by " + movement_units_[static_cast<size_t>(movement_unit_)].title());
}

void CoreSession::cmd_go_back() {
    TimelineController* tc = current();
    if (!tc)
        return;
    const std::string id = tc->undo_navigation();
    if (!id.empty())
        emit({{"event", "select_row"}, {"id", id}});
    else
        sound_.play(sound::Earcon::Boundary);
}

void CoreSession::cmd_play_earcon(const json& cmd) {
    const std::string name = cmd.value("name", std::string{});
    if (name == "boundary")
        sound_.play(sound::Earcon::Boundary);
    else if (name == "navigate")
        sound_.play(sound::Earcon::Navigate);
    else if (name == "close")
        sound_.play(sound::Earcon::Close);
    else if (name == "error")
        sound_.play(sound::Earcon::Error);
    else
        sound_.play_named(name);
}

// --- invisible interface ---

std::filesystem::path CoreSession::keymaps_dir() const {
    return config_path_.parent_path() / "keymaps";
}

bool CoreSession::is_user_keymap(const std::string& name) const {
    if (name.empty() || name == "default")
        return false;
    std::error_code ec;
    return std::filesystem::exists(keymaps_dir() / (name + ".keymap"), ec);
}

std::optional<std::filesystem::path> CoreSession::keymap_file(const std::string& name) const {
    if (name.empty() || name == "default")
        return std::nullopt;
    std::error_code ec;
    const auto user = keymaps_dir() / (name + ".keymap");
    if (std::filesystem::exists(user, ec))
        return user; // user copy wins (shadows a built-in of the same name)
    if (!bundled_keymaps_dir_.empty()) {
        const auto builtin = bundled_keymaps_dir_ / (name + ".keymap");
        if (std::filesystem::exists(builtin, ec))
            return builtin;
    }
    return std::nullopt;
}

std::vector<std::string> CoreSession::list_keymaps() const {
    std::set<std::string> names; // sorted + deduped (user shadows built-in of same name)
    auto scan = [&](const std::filesystem::path& dir) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            std::error_code ec2;
            if (entry.is_regular_file(ec2) && entry.path().extension() == ".keymap") {
                const std::string name = entry.path().stem().string();
                if (name != "default")
                    names.insert(name);
            }
        }
    };
    scan(keymaps_dir()); // user (editable)
    if (!bundled_keymaps_dir_.empty())
        scan(bundled_keymaps_dir_); // built-in (read-only)
    std::vector<std::string> out{"default"};
    out.insert(out.end(), names.begin(), names.end());
    return out;
}

void CoreSession::cmd_get_action_catalog() {
    json arr = json::array();
    for (const auto& a : input::action_catalog())
        arr.push_back({{"id", a.id}, {"label", a.label}, {"default_key", a.default_key}});
    emit({{"event", "action_catalog"}, {"actions", arr}});
}

void CoreSession::emit_keymap(const std::string& name) {
    input::ParsedKeymap custom;
    if (auto path = keymap_file(name)) { // user copy, else built-in
        std::ifstream in(*path, std::ios::binary);
        if (in) {
            std::string text((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            custom = input::parse_keymap(text);
        }
    }
    const input::KeyBindings eff = input::resolve_bindings(custom);
    json bindings = json::object();
    for (const auto& [key, action] : eff)
        bindings[key] = action;
    // Raw custom layer (action -> key overrides + unbound actions), so the
    // Keyboard Manager can show each binding's source and edit only overrides.
    json overrides = json::object();
    for (const auto& [key, action] : custom.bindings)
        overrides[action] = key;
    json unbinds = json::array();
    for (const auto& a : custom.unbinds)
        unbinds.push_back(a);
    // Which listed keymaps are read-only (default + built-ins shipped with the app,
    // i.e. everything without an editable user file).
    const std::vector<std::string> all = list_keymaps();
    json builtins = json::array();
    for (const auto& n : all)
        if (!is_user_keymap(n))
            builtins.push_back(n);
    emit({{"event", "keymap"},
          {"name", name},
          {"mode", settings_.invisible_mode},
          {"bindings", bindings},
          {"overrides", overrides},
          {"unbinds", unbinds},
          {"keymaps", all},
          {"builtins", builtins},
          {"editable", is_user_keymap(name)}});
}

void CoreSession::cmd_get_keymap(const json& cmd) {
    emit_keymap(cmd.value("name", settings_.invisible_keymap));
}

void CoreSession::cmd_set_active_keymap(const json& cmd) {
    settings_.invisible_keymap = cmd.value("name", std::string("default"));
    save_config();
    emit_keymap(settings_.invisible_keymap);
}

void CoreSession::cmd_save_keymap(const json& cmd) {
    const std::string name = cmd.value("name", std::string{});
    if (name.empty() || name == "default")
        return; // default is read-only
    std::map<std::string, std::string> overrides;
    const json overrides_json = cmd.value("overrides", json::object()); // avoid dangling .items()
    for (const auto& [action, key] : overrides_json.items())
        overrides[action] = key.get<std::string>();
    std::set<std::string> unbinds;
    for (const auto& u : cmd.value("unbinds", json::array()))
        unbinds.insert(u.get<std::string>());
    std::error_code ec;
    std::filesystem::create_directories(keymaps_dir(), ec);
    std::ofstream out(keymaps_dir() / (name + ".keymap"), std::ios::binary | std::ios::trunc);
    if (out)
        out << input::serialize_keymap(overrides, unbinds);
    emit_keymap(name);
}

void CoreSession::cmd_delete_keymap(const json& cmd) {
    const std::string name = cmd.value("name", std::string{});
    if (name.empty() || name == "default")
        return;
    std::error_code ec;
    std::filesystem::remove(keymaps_dir() / (name + ".keymap"), ec);
    if (settings_.invisible_keymap == name) {
        settings_.invisible_keymap = "default";
        save_config();
    }
    emit_keymap(settings_.invisible_keymap);
}

void CoreSession::invisible_speak_index(int visible_index) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const auto& items = tc->items();
    if (visible_index < 0 || visible_index >= static_cast<int>(items.size()))
        return;
    emit_announce(present::accessibility_label(items[static_cast<size_t>(visible_index)],
                                               util::now_unix()));
}

void CoreSession::invisible_step(int delta) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const auto& items = tc->items();
    if (items.empty()) {
        sound_.play(sound::Earcon::Boundary);
        return;
    }
    int idx = tc->visible_index_of(tc->selected_id());
    if (idx < 0)
        idx = 0;
    int dest = idx + delta;
    if (dest < 0)
        dest = 0;
    if (dest > static_cast<int>(items.size()) - 1)
        dest = static_cast<int>(items.size()) - 1;
    if (dest == idx) {
        sound_.play(sound::Earcon::Boundary); // hit an edge
        if (!settings_.invisible_repeat_at_edge)
            return; // boundary tone only; don't re-speak the item you're already on
    }
    const std::string id = items[static_cast<size_t>(dest)].id();
    tc->note_selection(id);
    emit({{"event", "select_row"}, {"id", id}}); // visible list follows if the window is shown
    invisible_speak_index(dest);
}

void CoreSession::invisible_goto_edge(bool top) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const auto& items = tc->items();
    if (items.empty()) {
        sound_.play(sound::Earcon::Boundary);
        return;
    }
    const int dest = top ? 0 : static_cast<int>(items.size()) - 1;
    const std::string id = items[static_cast<size_t>(dest)].id();
    tc->note_selection(id);
    emit({{"event", "select_row"}, {"id", id}});
    invisible_speak_index(dest);
}

void CoreSession::cmd_set_window_shown(const json& cmd) {
    settings_.window_shown = cmd.value("shown", true);
    save_config(); // lightweight: just persist, no re-render
}

void CoreSession::cmd_check_for_update(const json& cmd) {
    const bool silent = cmd.value("silent", false);
    const std::string branch = settings_.update_branch;
    net::IHttpClient* http = http_.get();
    const std::string cur_ver = fastsm::version();
    const std::string cur_commit = fastsm::build_commit();
    worker_.post([this, silent, branch, http, cur_ver, cur_commit] {
        const update::UpdateInfo info =
            update::check_for_update(*http, branch, cur_ver, cur_commit, kUpdateRepo);
        loop_.post([this, silent, info] {
            emit({{"event", "update_status"},
                  {"silent", silent},
                  {"available", info.available},
                  {"branch", info.branch},
                  {"version", info.version},
                  {"notes", info.notes},
                  {"download_url", info.download_url},
                  {"error", info.error}});
        });
    });
}

void CoreSession::cmd_download_update(const json& cmd) {
    const std::string url = cmd.value("url", std::string{});
    if (url.empty())
        return;
    net::IHttpClient* http = http_.get();
    worker_.post([this, http, url] {
        net::HttpRequest req;
        req.method = "GET";
        req.url = url;
        const net::HttpResponse res = http->send(req);
        std::string path, error;
        if (!res.ok() || res.body.empty()) {
            error = res.error.empty() ? ("Download failed (" + std::to_string(res.status) + ")")
                                      : res.error;
        } else {
            std::error_code ec;
            const auto p = std::filesystem::temp_directory_path(ec) / "FastSMRW-update.zip";
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if (out) {
                out.write(res.body.data(), static_cast<std::streamsize>(res.body.size()));
                out.close();
                path = p.string();
            } else {
                error = "Couldn't save the downloaded update.";
            }
        }
        loop_.post([this, path, error] {
            if (!error.empty())
                emit({{"event", "update_error"}, {"error", error}});
            else
                emit({{"event", "update_ready"}, {"path", path}});
        });
    });
}

void CoreSession::cmd_get_layer_keymap() {
    json bindings = json::object();
    for (const auto& [key, action] : input::layer_keymap())
        bindings[key] = action;
    emit({{"event", "layer_keymap"},
          {"activation", settings_.invisible_layer_key},
          {"bindings", bindings},
          {"enter_message", input::layer_enter_message()},
          {"help_message", input::layer_help_text()}});
}

void CoreSession::cmd_perform_action(const json& cmd) {
    const std::string a = cmd.value("action", std::string{});
    if (a.empty())
        return;
    // Navigation / timeline / account switching (works with the window hidden).
    if (a == "next_item")
        return invisible_step(1);
    if (a == "prev_item")
        return invisible_step(-1);
    if (a == "next_item_jump")
        return invisible_step(20);
    if (a == "prev_item_jump")
        return invisible_step(-20);
    if (a == "top_item")
        return invisible_goto_edge(true);
    if (a == "bottom_item")
        return invisible_goto_edge(false);
    if (a == "next_tl")
        return cmd_select_timeline({{"dir", "next"}, {"speak_position", true}});
    if (a == "prev_tl")
        return cmd_select_timeline({{"dir", "prev"}, {"speak_position", true}});
    if (a == "speak_item") {
        if (TimelineController* tc = current())
            invisible_speak_index(tc->visible_index_of(tc->selected_id()));
        return;
    }
    if (a == "UndoNavigation")
        return cmd_go_back();
    if (a == "refresh")
        return cmd_refresh();
    if (a == "NextAccount" || a == "PrevAccount") {
        cmd_select_account({{"dir", a == "PrevAccount" ? "prev" : "next"}});
        if (SocialAccount* ac = accounts_.selected())
            emit_announce(ac->me().acct);
        return;
    }
    // Actions on the current row (reuse the same handlers the UI uses).
    TimelineController* tc = current();
    const std::string row = tc ? tc->selected_id() : std::string{};
    if (a == "BoostToggle")
        return cmd_toggle_boost({{"id", row}});
    if (a == "LikeToggle")
        return cmd_toggle_favorite({{"id", row}});
    if (a == "Reply")
        return cmd_compose_context({{"mode", "reply"}, {"id", row}});
    if (a == "Quote")
        return cmd_compose_context({{"mode", "quote"}, {"id", row}});
    if (a == "Edit")
        return cmd_compose_context({{"mode", "edit"}, {"id", row}});
    if (a == "Post")
        return cmd_compose_context({{"mode", "new"}});
    if (a == "View")
        return cmd_post_info({{"id", row}});
    if (a == "Url")
        return cmd_open_status({{"id", row}});
    if (a == "open_thread")
        return cmd_open_thread({{"id", row}});
    if (a == "UserTimeline")
        return cmd_open_user_timeline({{"id", row}});
    if (a == "UserProfile")
        return cmd_open_user_profile({{"id", row}});
    if (a == "CloseTimeline")
        return cmd_close_timeline();
    // UI-only actions the app carries out (window/dialogs/find/stop speech).
    if (a == "ToggleWindow" || a == "Options" || a == "KeymapManager" || a == "StopAudio" ||
        a == "Find" || a == "FindNext" || a == "FindPrev" || a == "EnterLayer") {
        emit({{"event", "invisible_ui_action"}, {"action", a}});
        return;
    }
    // FollowToggle / MuteToggle / BlockToggle need relationship round-trips;
    // deferred to a later phase.
}

// --- helpers ---

std::vector<std::unique_ptr<TimelineController>>
CoreSession::build_timelines_for(SocialAccount* account, const std::vector<TimelineSource>& sources) {
    std::vector<std::unique_ptr<TimelineController>> v;
    if (!account)
        return v;
    for (const TimelineSource& src : sources)
        v.push_back(make_controller(account, src));
    for (auto& tc : v) {
        // Restore the remembered reading position before the cache load emits, so
        // the UI lands where the user left off (kept across the following refresh).
        if (auto it = positions_.find(tc->cache_key()); it != positions_.end())
            tc->note_selection(it->second);
        tc->load_cached();
        tc->refresh();
    }
    return v;
}

void CoreSession::rebuild_timelines() {
    // Build (and keep warm) timelines for EVERY account: the selected one is
    // displayed, the rest are parked and refreshed in the background. Each account
    // reopens the exact set of timelines it had (spawned ones included); a first
    // run with nothing saved falls back to that account's defaults.
    timelines_.clear();
    parked_.clear();
    current_ = 0;
    const auto saved = load_open_timelines();
    for (SocialAccount* account : accounts_.accounts()) {
        worker_.post([account] { account->load_configuration(); }); // refresh char limit
        const std::string key = account->account_key();
        std::vector<TimelineSource> sources;
        if (auto it = saved.find(key); it != saved.end() && !it->second.empty())
            sources = it->second;
        else
            sources = account->default_timelines();
        auto v = build_timelines_for(account, sources);
        if (key == accounts_.selected_key())
            timelines_ = std::move(v);
        else
            parked_[key] = std::move(v);
    }
    update_streaming();
}

void CoreSession::switch_account(const std::string& new_key) {
    const std::string old = accounts_.selected_key();
    if (new_key.empty())
        return;
    // Adding the first account auto-selects it (so new_key == old already), but
    // its timelines haven't been built yet -- fall through and build them. Only
    // short-circuit a genuine no-op switch to the already-shown account.
    if (new_key == old && !timelines_.empty())
        return;
    if (!timelines_.empty())
        parked_[old] = std::move(timelines_); // park the account we're leaving
    timelines_.clear();
    accounts_.select(new_key);
    current_ = 0;
    if (auto it = parked_.find(new_key); it != parked_.end()) {
        timelines_ = std::move(it->second); // unpark the warm timelines
        parked_.erase(it);
    } else if (SocialAccount* a = accounts_.selected()) {
        timelines_ = build_timelines_for(a, a->default_timelines());
    }
    for (auto& tc : timelines_)
        tc->refresh(); // freshen the account we just switched to
    update_streaming();
}

void CoreSession::refresh_all_accounts() {
    for (auto& tc : timelines_)
        tc->refresh();
    for (auto& [key, v] : parked_)
        for (auto& tc : v)
            tc->refresh();
}

std::unique_ptr<TimelineController> CoreSession::make_controller(SocialAccount* account,
                                                                 const TimelineSource& src) {
    const int page = account ? account->max_page_size() : 40;
    auto tc = std::make_unique<TimelineController>(account, src, &cache_, &worker_, &loop_, page);
    apply_timeline_settings(*tc); // refresh depth + Notifications mentions filter
    TimelineController* p = tc.get();
    tc->on_change = [this, p] {
        const int i = index_of(p);
        if (i >= 0)
            emit_timeline(i);
    };
    tc->on_error = [this](std::string e) { emit_announce(e); };
    tc->on_received_new = [this, p](int n) {
        if (n > 0)
            if (auto name = p->source().new_items_sound_name())
                sound_.play_named(*name);
    };
    return tc;
}

TimelineController* CoreSession::current() const {
    if (current_ < 0 || current_ >= static_cast<int>(timelines_.size()))
        return nullptr;
    return timelines_[static_cast<size_t>(current_)].get();
}

int CoreSession::index_of(const TimelineController* tc) const {
    for (int i = 0; i < static_cast<int>(timelines_.size()); ++i)
        if (timelines_[static_cast<size_t>(i)].get() == tc)
            return i;
    return -1;
}

const TimelineItem* CoreSession::find_item(const TimelineController* tc, const std::string& id) const {
    for (const auto& item : tc->items())
        if (item.id() == id)
            return &item;
    return nullptr;
}

void CoreSession::apply_timeline_settings(TimelineController& tc) {
    tc.set_max_refresh_pages(settings_.fetch_pages);
    // Optionally hide mention notifications from the Notifications timeline (e.g.
    // when a separate Mentions timeline is open).
    if (tc.source().kind == TimelineSource::Kind::Notifications) {
        if (settings_.show_mentions_in_notifications)
            tc.set_filter(nullptr);
        else
            tc.set_filter([](const TimelineItem& item) {
                const Notification* n = item.notification();
                return !(n && n->type == Notification::Kind::Mention);
            });
    }
}

void CoreSession::apply_settings() {
    sound_.set_enabled(settings_.sounds_enabled);
    sound_.set_soundpack(settings_.soundpack);
    present::SpeechConfig::set_current(settings_.speech);
    present::TextConfig::set_current(settings_.text);
    cache_.set_max_entries(settings_.cache_limit);
    // Re-apply per-timeline settings to every open timeline, displayed or parked,
    // so a change (e.g. toggling mentions in Notifications) takes effect at once.
    for (auto& tc : timelines_)
        apply_timeline_settings(*tc);
    for (auto& [key, v] : parked_)
        for (auto& tc : v)
            apply_timeline_settings(*tc);
    auto_refresh_seconds_.store(settings_.auto_refresh_seconds);
    update_streaming();
}

void CoreSession::save_config() {
    store::AppConfig config = accounts_.to_config();
    config.settings = settings_;
    const auto path = config_path_;
    worker_.post([config, path] { store::AppConfigStore(path).save(config); });
}

std::filesystem::path CoreSession::positions_path() const {
    return config_path_.parent_path() / "positions.json";
}

void CoreSession::load_positions() {
    positions_.clear();
    std::ifstream in(positions_path(), std::ios::binary);
    if (!in)
        return;
    try {
        json root;
        in >> root;
        if (root.is_object())
            for (const auto& [key, id] : root.items())
                if (id.is_string())
                    positions_[key] = id.get<std::string>();
    } catch (...) {
    }
}

void CoreSession::save_positions() const {
    // Tiny file (a handful of entries), written from the core-loop thread; separate
    // from the item cache so it never races the (worker-thread) cache writes.
    json root = json::object();
    for (const auto& [key, id] : positions_)
        root[key] = id;
    const std::string blob = root.dump();
    const std::filesystem::path path = positions_path();
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
        std::filesystem::remove(tmp, ec);
}

std::filesystem::path CoreSession::open_timelines_path() const {
    return config_path_.parent_path() / "open_timelines.json";
}

std::map<std::string, std::vector<TimelineSource>> CoreSession::load_open_timelines() const {
    std::map<std::string, std::vector<TimelineSource>> out;
    std::ifstream in(open_timelines_path(), std::ios::binary);
    if (!in)
        return out;
    try {
        json root;
        in >> root;
        if (root.is_object())
            for (const auto& [key, arr] : root.items())
                if (arr.is_array())
                    for (const auto& j : arr)
                        if (auto s = source_from_json(j))
                            out[key].push_back(*s);
    } catch (...) {
    }
    return out;
}

void CoreSession::save_open_timelines() const {
    json root = json::object();
    auto add = [&](const std::string& key,
                   const std::vector<std::unique_ptr<TimelineController>>& v) {
        json arr = json::array();
        for (const auto& tc : v)
            arr.push_back(source_to_json(tc->source()));
        root[key] = arr;
    };
    if (!accounts_.selected_key().empty())
        add(accounts_.selected_key(), timelines_);
    for (const auto& [key, v] : parked_)
        add(key, v);

    const std::string blob = root.dump();
    const std::filesystem::path path = open_timelines_path();
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
        std::filesystem::remove(tmp, ec);
}

std::vector<std::unique_ptr<TimelineController>>*
CoreSession::timelines_for_account(const std::string& key) {
    if (key == accounts_.selected_key())
        return &timelines_;
    auto it = parked_.find(key);
    return it != parked_.end() ? &it->second : nullptr;
}

void CoreSession::update_streaming() {
    if (!settings_.streaming_enabled) {
        streams_.clear(); // stop + join every stream
        return;
    }
    // Ensure one live stream per streaming-capable account (started once, left
    // running across account switches). The callback routes each event to the
    // owning account's timelines by key, whether displayed or parked.
    std::set<std::string> live;
    for (SocialAccount* account : accounts_.accounts()) {
        const std::string key = account->account_key();
        live.insert(key);
        if (!account->user_stream_request())
            continue; // this platform/account doesn't stream (e.g. Bluesky)
        auto& client = streams_[key];
        if (client)
            continue; // already streaming
        client = std::make_unique<StreamingClient>(http_.get(), &loop_);
        client->start(account, [this, key](StreamItem item) {
            if (auto* tls = timelines_for_account(key))
                for (auto& tc : *tls)
                    if (tc->source().kind == item.target) {
                        tc->ingest_realtime(std::move(item.item));
                        break;
                    }
        });
    }
    for (auto it = streams_.begin(); it != streams_.end();) // drop streams for gone accounts
        it = live.count(it->first) ? std::next(it) : streams_.erase(it);
}

void CoreSession::auto_refresh_loop() {
    // The user's auto-refresh interval polls EVERY account (displayed + parked),
    // so background accounts stay as fresh as the one on screen. Refreshes are
    // posted to the core loop and the worker runs them serially, so even many
    // accounts never spike the CPU.
    int elapsed = 0;
    while (auto_refresh_running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!auto_refresh_running_.load())
            break;
        const int interval = auto_refresh_seconds_.load();
        if (interval <= 0) {
            elapsed = 0;
            continue;
        }
        if (++elapsed >= interval) {
            elapsed = 0;
            loop_.post([this] { refresh_all_accounts(); });
        }
    }
}

std::optional<TimelineSource> CoreSession::source_from_kind(const std::string& kind) {
    if (kind == "home")
        return TimelineSource::home();
    if (kind == "notifications")
        return TimelineSource::notifications();
    if (kind == "mentions")
        return TimelineSource::mentions();
    if (kind == "local")
        return TimelineSource::local();
    if (kind == "federated")
        return TimelineSource::federated();
    if (kind == "bookmarks")
        return TimelineSource::bookmarks();
    if (kind == "favourites")
        return TimelineSource::favorites();
    return std::nullopt;
}

// --- event builders ---

void CoreSession::emit(const json& event) {
    if (emit_)
        emit_(event.dump());
}

void CoreSession::emit_settings() {
    json packs = json::array();
    for (const auto& p : sound_.list_soundpacks())
        packs.push_back(p);
    emit({{"event", "settings"},
          {"settings", store::settings_to_json(settings_)},
          {"soundpacks", packs}});
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
        tls.push_back({{"title", s.title()},
                       {"kind", s.cache_key()},
                       {"dismissable", s.is_dismissable()},
                       {"user_list", s.is_user_list()}});
    }
    emit({{"event", "timelines_changed"},
          {"timelines", tls},
          {"current", current_},
          {"account", accounts_.selected_key()}}); // so the UI won't carry a position across accounts
}

void CoreSession::emit_timeline(int index) {
    if (index < 0 || index >= static_cast<int>(timelines_.size()))
        return;
    TimelineController* tc = timelines_[static_cast<size_t>(index)].get();
    const std::int64_t now = util::now_unix();
    std::set<std::string> gap_after;
    for (const auto& g : tc->gaps())
        gap_after.insert(g.after_id);
    json rows = json::array();
    for (const auto& item : tc->items()) {
        json r = row_json(item, now);
        if (gap_after.count(item.id()))
            r["gap_after"] = true; // unloaded posts follow this row
        rows.push_back(std::move(r));
    }
    emit({{"event", "timeline_updated"},
          {"index", index},
          {"selected_id", tc->selected_id()},
          {"rows", std::move(rows)}});
}

void CoreSession::emit_all_timelines() {
    for (int i = 0; i < static_cast<int>(timelines_.size()); ++i)
        emit_timeline(i);
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
