#include "fastsm/session/core_session.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>

#include "fastsm/auth/bluesky_auth.hpp"
#include "fastsm/input/keymap.hpp"
#include "fastsm/auth/mastodon_auth.hpp"
#include "fastsm/platform/bluesky/bluesky_account.hpp"
#include "fastsm/platform/mastodon/mastodon_account.hpp"
#include "fastsm/presentation/reply_helper.hpp"
#include "fastsm/presentation/speech_settings.hpp"
#include "fastsm/presentation/status_presenter.hpp"
#include "fastsm/store/app_config.hpp"
#include "fastsm/util/date_parsing.hpp"

#include "fastsm/store/settings_json.hpp"

using nlohmann::json;

namespace fastsm {
namespace {

PostDraft draft_from_json(const json& d) {
    PostDraft draft;
    draft.text = d.value("text", std::string{});
    if (d.contains("reply_to_id"))
        draft.reply_to_id = d.value("reply_to_id", std::string{});
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

} // namespace

CoreSession::CoreSession(Paths paths, std::unique_ptr<net::IHttpClient> http,
                         std::function<void(const std::string&)> emit)
    : config_path_(paths.config_dir / "config.json"), emit_(std::move(emit)),
      http_(std::move(http)), cache_(paths.config_dir / "cache"), accounts_(http_.get()),
      stream_(http_.get(), &loop_) {
    sound_.set_bundled_packs_dir(paths.bundled_soundpacks);
    sound_.set_user_packs_dir(paths.config_dir / "soundpacks");
    auto_refresh_thread_ = std::thread([this] { auto_refresh_loop(); });
}

CoreSession::~CoreSession() {
    auto_refresh_running_.store(false);
    if (auto_refresh_thread_.joinable())
        auto_refresh_thread_.join();
    stream_.stop(); // join the streaming thread while http_/loop_ are still alive
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
}

// --- lifecycle / accounts ---

void CoreSession::cmd_start() {
    worker_.post([this] {
        store::AppConfig config = store::AppConfigStore(config_path_).load();
        accounts_.load(config); // Bluesky may re-establish a session here
        loop_.post([this, config] {
            settings_ = config.settings;
            apply_settings();
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
                    accounts_.add(std::move(account), cred);
                    accounts_.select(key);
                    save_config();
                    rebuild_timelines();
                    emit_accounts();
                    emit_timelines();
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
                    accounts_.select(key);
                    save_config();
                    rebuild_timelines();
                    emit_accounts();
                    emit_timelines();
                }
                emit({{"event", "auth_result"}, {"ok", r.ok}, {"error", r.error}});
            });
        });
    }
}

void CoreSession::cmd_remove_account(const json& cmd) {
    accounts_.remove(cmd.value("key", std::string{}));
    save_config();
    rebuild_timelines();
    emit_accounts();
    emit_timelines();
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
    accounts_.select(accts[static_cast<size_t>(target)]->account_key());
    sound_.play(sound::Earcon::Navigate);
    rebuild_timelines();
    emit_accounts();
    emit_timelines();
    save_config(); // remember which account is selected across restarts
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
    if (TimelineController* tc = current())
        emit_announce(tc->source().title());
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
    if (TimelineController* tc = current())
        tc->note_selection(cmd.value("id", std::string{}));
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
    timelines_.push_back(make_controller(src));
    current_ = static_cast<int>(timelines_.size()) - 1;
    TimelineController* p = timelines_.back().get();
    p->set_origin_key(origin);
    emit_timelines();
    p->load_cached();
    p->refresh();
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
    const int closed_index = current_;
    tc->on_change = nullptr;
    tc->on_error = nullptr;
    tc->on_received_new = nullptr;
    tc->clear();
    retired_.push_back(std::move(timelines_[static_cast<size_t>(closed_index)]));
    timelines_.erase(timelines_.begin() + closed_index);

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

std::vector<std::string> CoreSession::list_keymaps() const {
    std::vector<std::string> names{"default"};
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(keymaps_dir(), ec)) {
        std::error_code ec2;
        if (!entry.is_regular_file(ec2))
            continue;
        const auto& p = entry.path();
        if (p.extension() == ".keymap") {
            const std::string name = p.stem().string();
            if (name != "default")
                names.push_back(name);
        }
    }
    std::sort(names.begin() + 1, names.end()); // keep "default" first
    return names;
}

void CoreSession::cmd_get_action_catalog() {
    json arr = json::array();
    for (const auto& a : input::action_catalog())
        arr.push_back({{"id", a.id}, {"label", a.label}, {"default_key", a.default_key}});
    emit({{"event", "action_catalog"}, {"actions", arr}});
}

void CoreSession::emit_keymap(const std::string& name) {
    input::ParsedKeymap custom;
    if (!name.empty() && name != "default") {
        std::ifstream in(keymaps_dir() / (name + ".keymap"), std::ios::binary);
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
    emit({{"event", "keymap"},
          {"name", name},
          {"mode", settings_.invisible_mode},
          {"bindings", bindings},
          {"keymaps", list_keymaps()}});
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
    if (dest == idx)
        sound_.play(sound::Earcon::Boundary); // hit an edge; still (re)speak for orientation
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
    if (a == "next_timeline")
        return cmd_select_timeline({{"dir", "next"}});
    if (a == "prev_timeline")
        return cmd_select_timeline({{"dir", "prev"}});
    if (a == "speak_item") {
        if (TimelineController* tc = current())
            invisible_speak_index(tc->visible_index_of(tc->selected_id()));
        return;
    }
    if (a == "undo_navigation")
        return cmd_go_back();
    if (a == "refresh")
        return cmd_refresh();
    if (a == "next_account" || a == "prev_account") {
        cmd_select_account({{"dir", a == "prev_account" ? "prev" : "next"}});
        if (SocialAccount* ac = accounts_.selected())
            emit_announce(ac->me().acct);
        return;
    }
    // Actions on the current row (reuse the same handlers the UI uses).
    TimelineController* tc = current();
    const std::string row = tc ? tc->selected_id() : std::string{};
    if (a == "boost_toggle")
        return cmd_toggle_boost({{"id", row}});
    if (a == "favorite_toggle")
        return cmd_toggle_favorite({{"id", row}});
    if (a == "reply")
        return cmd_compose_context({{"mode", "reply"}, {"id", row}});
    if (a == "quote")
        return cmd_compose_context({{"mode", "quote"}, {"id", row}});
    if (a == "edit")
        return cmd_compose_context({{"mode", "edit"}, {"id", row}});
    if (a == "post")
        return cmd_compose_context({{"mode", "new"}});
    if (a == "post_info")
        return cmd_post_info({{"id", row}});
    if (a == "open_url")
        return cmd_open_status({{"id", row}});
    if (a == "open_thread")
        return cmd_open_thread({{"id", row}});
    if (a == "open_user_timeline")
        return cmd_open_user_timeline({{"id", row}});
    if (a == "open_user_profile")
        return cmd_open_user_profile({{"id", row}});
    if (a == "close_timeline")
        return cmd_close_timeline();
    // UI-only actions the app carries out (window/dialogs/stop speech).
    if (a == "toggle_window" || a == "settings" || a == "keymap_manager" || a == "stop_audio") {
        emit({{"event", "invisible_ui_action"}, {"action", a}});
        return;
    }
    // follow_toggle / mute_toggle / block_toggle need relationship round-trips;
    // deferred to a later phase.
}

// --- helpers ---

void CoreSession::rebuild_timelines() {
    timelines_.clear();
    current_ = 0;
    if (SocialAccount* account = accounts_.selected())
        for (const TimelineSource& src : account->default_timelines())
            timelines_.push_back(make_controller(src));
    for (auto& tc : timelines_) {
        tc->load_cached();
        tc->refresh();
    }
    update_streaming();
}

std::unique_ptr<TimelineController> CoreSession::make_controller(const TimelineSource& src) {
    SocialAccount* acc = accounts_.selected();
    const int page = acc ? acc->max_page_size() : 40;
    auto tc = std::make_unique<TimelineController>(acc, src, &cache_, &worker_, &loop_, page);
    tc->set_max_refresh_pages(settings_.fetch_pages);
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

void CoreSession::apply_settings() {
    sound_.set_enabled(settings_.sounds_enabled);
    sound_.set_soundpack(settings_.soundpack);
    present::SpeechConfig::set_current(settings_.speech);
    cache_.set_max_entries(settings_.cache_limit);
    const bool show_mentions = settings_.show_mentions_in_notifications;
    for (auto& tc : timelines_) {
        tc->set_max_refresh_pages(settings_.fetch_pages);
        // Optionally hide mention notifications from the Notifications timeline
        // (e.g. when a separate Mentions timeline is open).
        if (tc->source().kind == TimelineSource::Kind::Notifications) {
            if (show_mentions)
                tc->set_filter(nullptr);
            else
                tc->set_filter([](const TimelineItem& item) {
                    const Notification* n = item.notification();
                    return !(n && n->type == Notification::Kind::Mention);
                });
        }
    }
    auto_refresh_seconds_.store(settings_.auto_refresh_seconds);
    update_streaming();
}

void CoreSession::save_config() {
    store::AppConfig config = accounts_.to_config();
    config.settings = settings_;
    const auto path = config_path_;
    worker_.post([config, path] { store::AppConfigStore(path).save(config); });
}

void CoreSession::update_streaming() {
    SocialAccount* account = accounts_.selected();
    if (!settings_.streaming_enabled || !account) {
        stream_.stop();
        return;
    }
    stream_.start(account, [this](StreamItem item) {
        for (auto& tc : timelines_)
            if (tc->source().kind == item.target) {
                tc->ingest_realtime(std::move(item.item));
                break;
            }
    });
}

void CoreSession::auto_refresh_loop() {
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
            loop_.post([this] {
                for (auto& tc : timelines_)
                    tc->refresh();
            });
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
    emit({{"event", "timelines_changed"}, {"timelines", tls}, {"current", current_}});
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
