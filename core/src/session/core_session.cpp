#include "fastsm/session/core_session.hpp"

#include <algorithm>
#include <cctype>
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
#include "fastsm/util/base64.hpp"
#include "fastsm/util/date_parsing.hpp"
#include "fastsm/util/log.hpp"

#include "fastsm/store/settings_json.hpp"

using nlohmann::json;

namespace fastsm {
namespace {

// The GitHub repo the in-app updater checks.
constexpr const char* kUpdateRepo = "masonasons/FastSMRW";

json client_filter_to_json(const ClientFilter& f) {
    return {{"original", f.original},         {"replies", f.replies},
            {"replies_to_me", f.replies_to_me}, {"threads", f.threads},
            {"boosts", f.boosts},             {"quotes", f.quotes},
            {"media", f.media},               {"no_media", f.no_media},
            {"my_posts", f.my_posts},         {"my_replies", f.my_replies},
            {"text", f.text}};
}

ClientFilter client_filter_from_json(const json& j) {
    ClientFilter f;
    f.original = j.value("original", true);
    f.replies = j.value("replies", true);
    f.replies_to_me = j.value("replies_to_me", true);
    f.threads = j.value("threads", true);
    f.boosts = j.value("boosts", true);
    f.quotes = j.value("quotes", true);
    f.media = j.value("media", true);
    f.no_media = j.value("no_media", true);
    f.my_posts = j.value("my_posts", true);
    f.my_replies = j.value("my_replies", true);
    f.text = j.value("text", std::string{});
    return f;
}

json server_filter_to_json(const ServerFilter& f) {
    json ctx = json::array();
    for (const auto& c : f.context)
        ctx.push_back(c);
    json kws = json::array();
    for (const auto& k : f.keywords)
        kws.push_back({{"id", k.id}, {"keyword", k.keyword}, {"whole_word", k.whole_word}});
    json j = {{"id", f.id},   {"title", f.title}, {"action", f.action},
              {"context", ctx}, {"keywords", kws}};
    if (f.expires_at)
        j["expires_at"] = *f.expires_at;
    return j;
}

ServerFilter server_filter_from_json(const json& j) {
    ServerFilter f;
    f.id = j.value("id", std::string{});
    f.title = j.value("title", std::string{});
    f.action = j.value("action", std::string("warn"));
    f.expires_in = j.value("expires_in", 0);
    if (auto it = j.find("context"); it != j.end() && it->is_array())
        for (const auto& c : *it)
            if (c.is_string())
                f.context.push_back(c.get<std::string>());
    if (auto it = j.find("keywords"); it != j.end() && it->is_array())
        for (const auto& k : *it) {
            ServerFilterKeyword kw;
            kw.id = k.value("id", std::string{});
            kw.keyword = k.value("keyword", std::string{});
            kw.whole_word = k.value("whole_word", true);
            if (!kw.keyword.empty())
                f.keywords.push_back(std::move(kw));
        }
    return f;
}

PostDraft draft_from_json(const json& d) {
    PostDraft draft;
    draft.text = d.value("text", std::string{});
    // Selected reply recipients (Mastodon): prepend "@a @b " to the text. The UI
    // sends only the handles the user left checked in the recipient list.
    if (auto m = d.find("mentions"); m != d.end() && m->is_array()) {
        std::vector<std::string> accts;
        for (const auto& a : *m)
            if (a.is_string())
                accts.push_back(a.get<std::string>());
        const std::string prefix = present::mention_prefix(accts);
        if (!prefix.empty())
            draft.text = prefix + draft.text;
    }
    if (d.contains("reply_to_id"))
        draft.reply_to_id = d.value("reply_to_id", std::string{});
    if (d.contains("reply_to_url"))
        draft.reply_to_url = d.value("reply_to_url", std::string{});
    if (d.contains("quoted_status_id"))
        draft.quoted_status_id = d.value("quoted_status_id", std::string{});
    if (d.contains("quoted_status_cid"))
        draft.quoted_status_cid = d.value("quoted_status_cid", std::string{});
    if (d.contains("quoted_status_url"))
        draft.quoted_status_url = d.value("quoted_status_url", std::string{});
    if (d.contains("spoiler_text"))
        draft.spoiler_text = d.value("spoiler_text", std::string{});
    if (d.contains("language"))
        draft.language = d.value("language", std::string{});
    if (d.contains("visibility"))
        draft.visibility = static_cast<Visibility>(d.value("visibility", 0));
    if (d.contains("scheduled_at"))
        draft.scheduled_at = d.value("scheduled_at", std::int64_t{0});
    // Media attachments (bytes arrive base64-encoded from the UI, which read the
    // files). Alt text rides along as `alt`.
    if (auto at = d.find("attachments"); at != d.end() && at->is_array()) {
        for (const auto& a : *at) {
            MediaUpload m;
            m.filename = a.value("filename", std::string{});
            m.mime = a.value("mime", std::string{});
            m.alt = a.value("alt", std::string{});
            m.bytes = util::base64_decode(a.value("data", std::string{}));
            if (!m.bytes.empty())
                draft.attachments.push_back(std::move(m));
        }
    }
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
    if (action == "authorize_request")
        return "Accept";
    if (action == "reject_request")
        return "Reject";
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
    if (action == "authorize_request")
        return "Accepted " + at;
    if (action == "reject_request")
        return "Rejected " + at;
    return "Done";
}

json features_json(const PlatformFeatures& f) {
    return {{"visibility", f.visibility},     {"content_warning", f.content_warning},
            {"quote_posts", f.quote_posts},   {"polls", f.polls},
            {"editing", f.editing},           {"scheduling", f.scheduling},
            {"media", f.media}};
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
    case K::List: return "list";
    case K::Mutes: return "mutes";
    case K::Blocks: return "blocks";
    case K::FollowRequests: return "followRequests";
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
    if (s == "list") return K::List;
    if (s == "mutes") return K::Mutes;
    if (s == "blocks") return K::Blocks;
    if (s == "followRequests") return K::FollowRequests;
    return std::nullopt;
}

json lists_to_json(const std::vector<TimelineList>& lists) {
    json arr = json::array();
    for (const auto& l : lists)
        arr.push_back({{"id", l.id},
                       {"title", l.title},
                       {"replies_policy", l.replies_policy},
                       {"exclusive", l.exclusive}});
    return arr;
}

json followed_tags_to_json(const std::vector<FollowedTag>& tags) {
    json arr = json::array();
    for (const auto& t : tags)
        arr.push_back({{"name", t.name}, {"url", t.url}, {"following", t.following}});
    return arr;
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
    log::init(paths.config_dir / "fastsm.log");
    log::write(std::string("session start, version ") + fastsm::version());
    auto_refresh_thread_ = std::thread([this] { auto_refresh_loop(); });
}

CoreSession::~CoreSession() {
    log::write("session ending, stopping all streams (app exit)");
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
    else if (c == "toggle_pin_post")
        cmd_toggle_pin_post(cmd);
    else if (c == "post")
        cmd_post(cmd);
    else if (c == "compose_context")
        cmd_compose_context(cmd);
    else if (c == "open_status")
        cmd_open_status(cmd);
    else if (c == "open_post_links")
        cmd_open_post_links(cmd);
    else if (c == "post_info")
        cmd_post_info(cmd);
    else if (c == "vote_poll")
        cmd_vote_poll(cmd);
    else if (c == "play_media")
        cmd_play_media(cmd);
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
    else if (c == "reorder_timeline")
        cmd_reorder_timeline(cmd);
    else if (c == "toggle_pin")
        cmd_toggle_pin();
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
    else if (c == "get_client_filter")
        cmd_get_client_filter();
    else if (c == "set_client_filter")
        cmd_set_client_filter(cmd);
    else if (c == "clear_client_filter")
        cmd_clear_client_filter();
    else if (c == "list_server_filters")
        cmd_list_server_filters();
    else if (c == "save_server_filter")
        cmd_save_server_filter(cmd);
    else if (c == "delete_server_filter")
        cmd_delete_server_filter(cmd);
    else if (c == "get_user_lists")
        cmd_get_user_lists(cmd);
    else if (c == "set_user_list")
        cmd_set_user_list(cmd);
    else if (c == "list_lists")
        cmd_list_lists();
    else if (c == "create_list")
        cmd_create_list(cmd);
    else if (c == "rename_list")
        cmd_rename_list(cmd);
    else if (c == "delete_list")
        cmd_delete_list(cmd);
    else if (c == "follow_hashtag_prompt")
        cmd_follow_hashtag_prompt(cmd);
    else if (c == "follow_hashtag")
        cmd_follow_hashtag(cmd);
    else if (c == "unfollow_hashtag")
        cmd_unfollow_hashtag(cmd);
    else if (c == "list_followed_hashtags")
        cmd_list_followed_hashtags();
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
            load_client_filters(); // per-timeline client filters (before timelines build)
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

void CoreSession::cmd_reorder_timeline(const json& cmd) {
    const int n = static_cast<int>(timelines_.size());
    if (n < 2)
        return;
    const std::string dir = cmd.value("dir", std::string{});
    int target = current_;
    if (dir == "up")
        target = current_ - 1;
    else if (dir == "down")
        target = current_ + 1;
    else
        return;
    if (target < 0 || target >= n) { // already at the top/bottom edge
        sound_.play(sound::Earcon::Boundary);
        return;
    }
    std::swap(timelines_[static_cast<size_t>(current_)], timelines_[static_cast<size_t>(target)]);
    current_ = target;
    save_open_timelines(); // the new order survives a restart
    sound_.play(sound::Earcon::Navigate);
    emit_timelines();
    if (TimelineController* tc = current())
        emit_announce(tc->source().title() + ", " + std::to_string(current_ + 1) + " of " +
                      std::to_string(n));
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
            // The account's lists, offered from cache (refreshed below for next time).
            // Each carries its list id in "param", which the UI echoes back on spawn.
            if (auto it = lists_by_account_.find(account->account_key());
                it != lists_by_account_.end()) {
                for (const auto& l : it->second) {
                    const std::string ck = "list:" + l.id;
                    bool open = false;
                    for (auto& tc : timelines_)
                        if (tc->source().cache_key() == ck) {
                            open = true;
                            break;
                        }
                    if (!open)
                        tls.push_back(
                            {{"kind", "list"}, {"param", l.id}, {"title", "List: " + l.title}});
                }
            }
            refresh_lists(account); // keep the list cache warm for the next Ctrl+T
        }
    }
    emit({{"event", "spawnable_timelines"}, {"timelines", tls}});
}

void CoreSession::refresh_lists(SocialAccount* account) {
    if (!account || account->platform() != Platform::Mastodon)
        return;
    const std::string key = account->account_key();
    worker_.post([this, account, key] {
        auto lists = account->lists();
        loop_.post([this, key, lists = std::move(lists)]() mutable {
            lists_by_account_[key] = std::move(lists);
        });
    });
}

void CoreSession::emit_lists() {
    SocialAccount* acct = accounts_.selected();
    if (!acct || acct->platform() != Platform::Mastodon) {
        emit({{"event", "lists"}, {"supported", false}, {"lists", json::array()}});
        return;
    }
    worker_.post([this, acct] {
        auto lists = acct->lists();
        const std::string key = acct->account_key();
        loop_.post([this, key, lists = std::move(lists)]() mutable {
            lists_by_account_[key] = lists; // keep the Ctrl+T cache fresh too
            emit({{"event", "lists"}, {"supported", true}, {"lists", lists_to_json(lists)}});
        });
    });
}

void CoreSession::cmd_list_lists() { emit_lists(); }

// Run a list mutation on the worker, then re-fetch the lists, refresh the cache,
// emit the updated set, and announce the outcome.
void CoreSession::run_list_mutation(SocialAccount* acct, std::function<bool()> op,
                                    std::string ok_msg, std::string fail_msg) {
    const std::string key = acct->account_key();
    worker_.post([this, acct, key, op = std::move(op), ok_msg = std::move(ok_msg),
                  fail_msg = std::move(fail_msg)]() mutable {
        const bool ok = op();
        auto lists = acct->lists();
        loop_.post([this, key, ok, lists = std::move(lists), ok_msg = std::move(ok_msg),
                    fail_msg = std::move(fail_msg)]() mutable {
            lists_by_account_[key] = lists;
            emit({{"event", "lists"}, {"supported", true}, {"lists", lists_to_json(lists)}});
            emit_announce(ok ? ok_msg : fail_msg);
        });
    });
}

void CoreSession::cmd_create_list(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    const std::string title = cmd.value("title", std::string{});
    if (!acct || acct->platform() != Platform::Mastodon || title.empty())
        return;
    const std::string rp = cmd.value("replies_policy", std::string("list"));
    const bool excl = cmd.value("exclusive", false);
    run_list_mutation(
        acct, [acct, title, rp, excl] { return acct->create_list(title, rp, excl); },
        "List created.", "Couldn't create the list.");
}

void CoreSession::cmd_rename_list(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    const std::string id = cmd.value("id", std::string{});
    const std::string title = cmd.value("title", std::string{});
    if (!acct || acct->platform() != Platform::Mastodon || id.empty() || title.empty())
        return;
    const std::string rp = cmd.value("replies_policy", std::string("list"));
    const bool excl = cmd.value("exclusive", false);
    run_list_mutation(
        acct, [acct, id, title, rp, excl] { return acct->update_list(id, title, rp, excl); },
        "List updated.", "Couldn't update the list.");
}

void CoreSession::cmd_delete_list(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    const std::string id = cmd.value("id", std::string{});
    if (!acct || acct->platform() != Platform::Mastodon || id.empty())
        return;
    // If the deleted list is open as a timeline, closing is left to the user; the
    // timeline just stops receiving new items.
    run_list_mutation(acct, [acct, id] { return acct->delete_list(id); }, "List deleted.",
                      "Couldn't delete the list.");
}

void CoreSession::cmd_get_user_lists(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    const std::string account_id = cmd.value("account_id", std::string{});
    const std::string acctname = cmd.value("acct", std::string{});
    if (!acct || acct->platform() != Platform::Mastodon || account_id.empty()) {
        emit({{"event", "user_lists"}, {"supported", false}, {"lists", json::array()}});
        return;
    }
    worker_.post([this, acct, account_id, acctname] {
        auto all = acct->lists();
        auto member = acct->account_lists(account_id);
        loop_.post([this, account_id, acctname, all = std::move(all),
                    member = std::move(member)]() mutable {
            json arr = json::array();
            for (const auto& l : all) {
                bool in = false;
                for (const auto& m : member)
                    if (m.id == l.id) {
                        in = true;
                        break;
                    }
                arr.push_back({{"id", l.id}, {"title", l.title}, {"member", in}});
            }
            emit({{"event", "user_lists"},
                  {"supported", true},
                  {"account_id", account_id},
                  {"acct", acctname},
                  {"lists", arr}});
        });
    });
}

void CoreSession::cmd_set_user_list(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    const std::string list_id = cmd.value("list_id", std::string{});
    const std::string account_id = cmd.value("account_id", std::string{});
    const bool add = cmd.value("add", false);
    if (!acct || acct->platform() != Platform::Mastodon || list_id.empty() || account_id.empty())
        return;
    worker_.post([this, acct, list_id, account_id, add] {
        const bool ok = acct->set_list_membership(list_id, account_id, add);
        loop_.post([this, ok, add] {
            if (!ok)
                emit_announce(add
                                  ? "Couldn't add to the list; you may need to follow them first."
                                  : "Couldn't remove from the list.");
        });
    });
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
    // A Mastodon list: the entry carried its id in "param"; look up its title
    // from the cached list set for a nice "List: <name>" heading.
    if (kind == "list") {
        const std::string id = cmd.value("param", std::string{});
        if (id.empty())
            return;
        std::string title = "List";
        if (SocialAccount* a = accounts_.selected())
            if (auto it = lists_by_account_.find(a->account_key()); it != lists_by_account_.end())
                for (const auto& l : it->second)
                    if (l.id == id) {
                        title = l.title;
                        break;
                    }
        spawn_source(TimelineSource::list(id, "List: " + title));
        return;
    }
    if (kind == "mutes" || kind == "blocks" || kind == "follow_requests") {
        if (accounts_.selected()->platform() != Platform::Mastodon) {
            emit_announce("Muted, blocked and follow-request lists are only available for "
                          "Mastodon accounts.");
            return;
        }
    }
    if (auto src = source_from_kind(kind))
        spawn_source(*src);
}

// --- Followed hashtags (Mastodon) ---

namespace {
std::string without_hash(const std::string& s) {
    return (!s.empty() && s.front() == '#') ? s.substr(1) : s;
}
} // namespace

void CoreSession::cmd_follow_hashtag_prompt(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    if (!acct)
        return;
    if (!acct->features().follow_hashtags) {
        emit_announce("Following hashtags is only supported on Mastodon.");
        return;
    }
    // Pre-fill the prompt with the hashtags in the focused post (deduped, order
    // preserved); blank if the post has none.
    json tags = json::array();
    TimelineController* tc = current();
    const std::string row_id = cmd.value("id", std::string{});
    if (tc && !row_id.empty())
        if (const TimelineItem* item = find_item(tc, row_id))
            if (const Status* s = item->actionable_status()) {
                std::set<std::string> seen;
                for (const auto& t : s->tags)
                    if (!t.empty() && seen.insert(t).second)
                        tags.push_back(t);
            }
    emit({{"event", "hashtag_prompt"}, {"tags", std::move(tags)}});
}

void CoreSession::cmd_follow_hashtag(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    const std::string name = without_hash(cmd.value("name", std::string{}));
    if (!acct || !acct->features().follow_hashtags || name.empty())
        return;
    worker_.post([this, acct, name] {
        const bool ok = acct->follow_hashtag(name);
        loop_.post([this, ok, name] {
            sound_.play(ok ? sound::Earcon::Favorite : sound::Earcon::Error);
            emit_announce(ok ? ("Now following #" + name) : "Couldn't follow that hashtag.");
        });
    });
}

void CoreSession::cmd_unfollow_hashtag(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    const std::string name = without_hash(cmd.value("name", std::string{}));
    if (!acct || !acct->features().follow_hashtags || name.empty())
        return;
    worker_.post([this, acct, name] {
        const bool ok = acct->unfollow_hashtag(name);
        auto tags = ok ? acct->followed_hashtags() : std::vector<FollowedTag>{};
        loop_.post([this, ok, name, tags = std::move(tags)]() mutable {
            sound_.play(ok ? sound::Earcon::Unfavorite : sound::Earcon::Error);
            emit_announce(ok ? ("Unfollowed #" + name) : "Couldn't unfollow that hashtag.");
            if (ok) // refresh an open manager dialog
                emit({{"event", "followed_hashtags"}, {"tags", followed_tags_to_json(tags)}});
        });
    });
}

void CoreSession::cmd_list_followed_hashtags() { emit_followed_hashtags(); }

void CoreSession::emit_followed_hashtags() {
    SocialAccount* acct = accounts_.selected();
    if (!acct || !acct->features().follow_hashtags) {
        emit({{"event", "followed_hashtags"}, {"supported", false}, {"tags", json::array()}});
        return;
    }
    worker_.post([this, acct] {
        auto tags = acct->followed_hashtags();
        loop_.post([this, tags = std::move(tags)]() mutable {
            emit({{"event", "followed_hashtags"},
                  {"supported", true},
                  {"tags", followed_tags_to_json(tags)}});
        });
    });
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
    const bool can_use_lists = acct && acct->platform() == Platform::Mastodon;
    // Off-thread: enrich a sparse row (Bluesky) via fetch_profile, compose the
    // text, and fetch the relationship, then emit. Profiling a mention still
    // works (everything is keyed by id).
    worker_.post([this, acct, user, handle, can_hide_boosts, can_use_lists] {
        User full = user;
        if (acct && !user.id.empty())
            if (auto p = acct->fetch_profile(user.id))
                full = *p;
        const std::string text = present::user_profile(full);
        std::optional<Relationship> rel;
        if (acct && !user.id.empty())
            rel = acct->relationship(user.id);
        loop_.post([this, full, text, handle, rel, can_hide_boosts, can_use_lists] {
            json e = {{"event", "user_profile"},
                      {"text", text},
                      {"account_id", full.id},
                      {"acct", handle},
                      {"url", full.url},
                      {"has_relationship", rel.has_value()},
                      {"can_hide_boosts", can_hide_boosts},
                      {"can_use_lists", can_use_lists}};
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
        else if (action == "authorize_request")
            ok = acct->authorize_follow_request(id);
        else if (action == "reject_request")
            ok = acct->reject_follow_request(id);
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
            else if (action == "authorize_request")
                ok = acct->authorize_follow_request(id);
            else if (action == "reject_request")
                ok = acct->reject_follow_request(id);
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

void CoreSession::cmd_toggle_pin() {
    TimelineController* tc = current();
    if (!tc)
        return;
    const bool now_pinned = !tc->pinned();
    tc->set_pinned(now_pinned);
    save_open_timelines(); // pin state survives a restart
    sound_.play(now_pinned ? sound::Earcon::Favorite : sound::Earcon::Unfavorite);
    emit_timelines(); // refresh the UI's dismissable/pinned flags
    emit_announce(tc->source().title() + (now_pinned ? ", pinned" : ", unpinned"));
}

void CoreSession::cmd_close_timeline() {
    TimelineController* tc = current();
    // A pinned tab is locked; only inherently-dismissable, un-pinned tabs close.
    if (!tc || !tc->source().is_dismissable() || tc->pinned())
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

void CoreSession::cmd_toggle_pin_post(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const int idx = tc->visible_index_of(cmd.value("id", std::string{}));
    if (idx < 0)
        return;
    const int r = tc->toggle_pin_post(idx);
    if (r < 0) { // not your own post (or not pinnable)
        sound_.play(sound::Earcon::Error);
        emit_announce("You can only pin your own posts to your profile.");
        return;
    }
    sound_.play(r ? sound::Earcon::Favorite : sound::Earcon::Unfavorite);
    emit_announce(r ? "Pinned to your profile" : "Unpinned from your profile");
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
        // Recipients as a togglable checklist (Mastodon). The UI mentions only the
        // ones left checked; the mention text is no longer baked into the body.
        if (account->platform() == Platform::Mastodon) {
            json parts = json::array();
            for (const auto& p : present::reply_participants(*target, account->me()))
                parts.push_back({{"acct", p.acct}, {"display_name", p.display_name}});
            ctx["reply_participants"] = parts;
        }
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
        if (target->cid) // Bluesky needs the quoted post's cid to build the embed
            ctx["quoted_status_cid"] = *target->cid;
        if (target->instance_url && !target->url.empty()) // remote: resolve to a local id
            ctx["quoted_status_url"] = target->url;
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

void CoreSession::cmd_open_post_links(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const TimelineItem* item = find_item(tc, cmd.value("id", std::string{}));
    if (!item)
        return;
    const Status* s = item->actionable_status();
    if (!s)
        return;
    const std::vector<present::PostLink> links = present::post_links(*s);
    if (links.empty()) {
        emit_announce("No links in this post.");
        return;
    }
    if (links.size() == 1) {
        emit({{"event", "open_url"}, {"url", links[0].url}});
        return;
    }
    // Multiple: let the UI present a chooser (works with the window hidden too).
    json arr = json::array();
    for (const auto& l : links)
        arr.push_back({{"title", l.title}, {"url", l.url}});
    emit({{"event", "url_picker"}, {"links", arr}});
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
    json ev = {{"event", "post_info"},
               {"id", cmd.value("id", std::string{})},
               {"text", present::post_info(*s, util::now_unix())},
               {"features", features_json(tc->account()->features())},
               {"has_url", !s->url.empty()}};
    // A poll the viewer can still vote in (not yet voted, not closed): let the UI
    // offer the voting controls. Otherwise the results are already in the text.
    if (s->poll && !s->poll->voted && !s->poll->expired) {
        json opts = json::array();
        for (const auto& o : s->poll->options)
            opts.push_back(o.title);
        ev["poll"] = {{"multiple", s->poll->multiple}, {"options", std::move(opts)}};
    }
    emit(ev);
}

void CoreSession::cmd_vote_poll(const json& cmd) {
    TimelineController* tc = current();
    if (!tc || !tc->account())
        return;
    const std::string row_id = cmd.value("id", std::string{});
    const TimelineItem* item = find_item(tc, row_id);
    if (!item)
        return;
    const Status* s = item->actionable_status();
    if (!s || !s->poll)
        return;
    const std::string poll_id = s->poll->id;
    std::vector<int> choices;
    if (cmd.contains("choices") && cmd["choices"].is_array())
        for (const auto& c : cmd["choices"])
            if (c.is_number_integer())
                choices.push_back(c.get<int>());
    if (poll_id.empty() || choices.empty())
        return;
    SocialAccount* acct = tc->account();
    worker_.post([this, acct, poll_id, choices, row_id] {
        std::optional<Poll> updated = acct->vote_poll(poll_id, choices);
        loop_.post([this, updated = std::move(updated), row_id]() mutable {
            if (!updated) {
                sound_.play(sound::Earcon::Error);
                emit_announce("Vote failed");
                return;
            }
            sound_.play(sound::Earcon::PostSent);
            emit_announce("Vote recorded");
            if (TimelineController* tc2 = current(); tc2 && find_item(tc2, row_id)) {
                tc2->set_poll(row_id, *updated);
                cmd_post_info({{"id", row_id}}); // reopen showing the results
            }
        });
    });
}

namespace {
std::string media_kind_str(MediaAttachment::Kind k) {
    switch (k) {
    case MediaAttachment::Kind::Image:
        return "image";
    case MediaAttachment::Kind::Video:
        return "video";
    case MediaAttachment::Kind::Audio:
        return "audio";
    case MediaAttachment::Kind::Gifv:
        return "gifv";
    default:
        return "media";
    }
}
// Player/menu label: the alt text if any, prefixed with the capitalized kind.
std::string media_label(const MediaAttachment& m) {
    std::string kind = media_kind_str(m.type);
    if (!kind.empty())
        kind[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(kind[0])));
    return m.description.empty() ? kind : (kind + ": " + m.description);
}
} // namespace

void CoreSession::play_one_media(const std::string& url, const std::string& kind,
                                 const std::string& title) {
    if (url.empty()) {
        sound_.play(sound::Earcon::Error);
        emit_announce("No media to play");
        return;
    }
    // Audio streams in-app (seek/volume); video/gif/image go to the system player.
    if (kind == "audio")
        emit({{"event", "media_open"}, {"url", url}, {"title", title}});
    else
        emit({{"event", "open_url"}, {"url", url}});
}

void CoreSession::cmd_play_media(const json& cmd) {
    // A specific attachment chosen from the picker plays directly.
    if (cmd.contains("url")) {
        play_one_media(cmd.value("url", std::string{}), cmd.value("kind", std::string{}),
                       cmd.value("title", std::string{}));
        return;
    }
    TimelineController* tc = current();
    if (!tc)
        return;
    const std::string row = cmd.value("id", std::string{});
    const TimelineItem* item = find_item(tc, row);
    const Status* s = item ? item->actionable_status() : nullptr;
    std::vector<const MediaAttachment*> media;
    if (s)
        for (const auto& m : s->media_attachments)
            if (!m.url.empty())
                media.push_back(&m);
    if (media.empty()) {
        sound_.play(sound::Earcon::Error);
        emit_announce("No media to play");
        return;
    }
    if (media.size() == 1) {
        play_one_media(media[0]->url, media_kind_str(media[0]->type), media_label(*media[0]));
        return;
    }
    // Multiple attachments: let the user pick which one to play.
    json items = json::array();
    for (const auto* m : media)
        items.push_back({{"title", media_label(*m)},
                         {"url", m->url},
                         {"kind", media_kind_str(m->type)}});
    emit({{"event", "media_picker"}, {"id", row}, {"items", std::move(items)}});
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
    // Broadcast settings so the UI updates its cached active-keymap name BEFORE the
    // keymap event arrives; otherwise ev_keymap's active-name gate drops the rebind
    // and the switch only takes effect on next launch.
    emit_settings();
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
    // Pull in more posts as we approach a gap or the scrollback edge, exactly like
    // the window does when you scroll - otherwise the invisible interface gets
    // stuck at the edge of what's loaded (a gap, or the bottom of the buffer).
    invisible_autoload(tc, dest);
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
    invisible_autoload(tc, dest); // jumping to an edge may sit on a gap / scrollback boundary
}

void CoreSession::invisible_autoload(TimelineController* tc, int visible_index) {
    if (!tc)
        return;
    const auto& items = tc->items();
    const int count = static_cast<int>(items.size());
    if (count == 0)
        return;
    // A tracked middle gap within a few rows -> fill it (reversed feeds keep older
    // posts above, so scan both directions around the cursor). Mirrors the window's
    // maybe_load_older so invisible navigation loads content the same way.
    if (!tc->gaps().empty()) {
        std::set<std::string> gap_after;
        for (const auto& g : tc->gaps())
            gap_after.insert(g.after_id);
        for (int d = 0; d <= 5; ++d) {
            for (int g : {visible_index + d, visible_index - d}) {
                if (g < 0 || g >= count)
                    continue;
                if (gap_after.count(items[static_cast<size_t>(g)].id())) {
                    tc->load_gap(items[static_cast<size_t>(g)].id());
                    return;
                }
            }
        }
    }
    // Near the scrollback edge (the bottom normally, the top when reversed) -> load older.
    const bool near_edge = tc->reversed() ? (visible_index <= 9) : (visible_index >= count - 10);
    if (near_edge)
        tc->load_older();
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
                  {"installer_url", info.installer_url},
                  {"error", info.error}});
        });
    });
}

void CoreSession::cmd_download_update(const json& cmd) {
    const std::string url = cmd.value("url", std::string{});
    if (url.empty())
        return;
    const bool installer = cmd.value("installer", false);
    net::IHttpClient* http = http_.get();
    worker_.post([this, http, url, installer] {
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
            const char* name = installer ? "FastSMRW-Setup.exe" : "FastSMRW-update.zip";
            const auto p = std::filesystem::temp_directory_path(ec) / name;
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if (out) {
                out.write(res.body.data(), static_cast<std::streamsize>(res.body.size()));
                out.close();
                path = p.string();
            } else {
                error = "Couldn't save the downloaded update.";
            }
        }
        loop_.post([this, path, error, installer] {
            if (!error.empty())
                emit({{"event", "update_error"}, {"error", error}});
            else
                emit({{"event", "update_ready"}, {"path", path}, {"installer", installer}});
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
    if (a == "MoveTimelineUp")
        return cmd_reorder_timeline({{"dir", "up"}});
    if (a == "MoveTimelineDown")
        return cmd_reorder_timeline({{"dir", "down"}});
    if (a == "TogglePin")
        return cmd_toggle_pin();
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
    if (a == "PinPost")
        return cmd_toggle_pin_post({{"id", row}});
    if (a == "FollowHashtag")
        return cmd_follow_hashtag_prompt({{"id", row}});
    if (a == "ManageHashtags")
        return cmd_list_followed_hashtags();
    if (a == "Enter") { // the configurable default action, like pressing Enter in the window
        const TimelineItem* it = tc ? find_item(tc, row) : nullptr;
        if (!it)
            return;
        if (it->is_user()) {
            const std::string& ua = settings_.enter_user_action;
            if (ua == "profile")
                return cmd_open_user_profile({{"id", row}});
            if (ua == "timeline")
                return cmd_open_user_timeline({{"id", row}});
            emit({{"event", "invisible_ui_action"}, {"action", "UserActions"}}); // the batch menu
            return;
        }
        const std::string& pa = settings_.enter_post_action;
        if (pa == "thread")
            return cmd_open_thread({{"id", row}});
        if (pa == "reply")
            return cmd_compose_context({{"mode", "reply"}, {"id", row}});
        if (pa == "links")
            return cmd_open_post_links({{"id", row}});
        return cmd_post_info({{"id", row}}); // default: view the post
    }
    if (a == "SecondaryAction") { // the configurable secondary interact (Shift+Enter); post-only
        const TimelineItem* it = tc ? find_item(tc, row) : nullptr;
        if (!it || it->is_user())
            return;
        const std::string& sa = settings_.secondary_post_action;
        if (sa == "post_info")
            return cmd_post_info({{"id", row}});
        if (sa == "thread")
            return cmd_open_thread({{"id", row}});
        if (sa == "reply")
            return cmd_compose_context({{"mode", "reply"}, {"id", row}});
        if (sa == "links")
            return cmd_open_post_links({{"id", row}});
        return cmd_play_media({{"id", row}}); // default: play media
    }
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
        return cmd_open_post_links({{"id", row}});
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
        a == "StopMedia" || a == "Find" || a == "FindNext" || a == "FindPrev" ||
        a == "EnterLayer" || a == "NewTimeline") {
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
    bool migrated = false;
    for (SocialAccount* account : accounts_.accounts()) {
        worker_.post([account] { account->load_configuration(); }); // refresh char limit
        refresh_lists(account); // warm each account's list cache for Ctrl+T
        const std::string key = account->account_key();
        std::vector<TimelineSource> sources;
        std::vector<bool> pins;
        bool from_saved = false;
        if (auto it = saved.find(key); it != saved.end() && !it->second.empty()) {
            from_saved = true;
            for (const SavedTimeline& st : it->second) {
                sources.push_back(st.source);
                pins.push_back(st.pinned);
            }
            // Migration: make sure the account's standing (non-dismissable) default
            // timelines are present even if it was saved before those defaults
            // existed -- e.g. a Bluesky account that predates the Notifications
            // timeline. Only non-dismissable defaults are added back: the user
            // can't have closed those, so a missing one just means an older save.
            for (const TimelineSource& def : account->default_timelines()) {
                if (def.is_dismissable())
                    continue;
                const std::string ck = def.cache_key();
                const bool have = std::any_of(sources.begin(), sources.end(),
                                              [&](const TimelineSource& s) { return s.cache_key() == ck; });
                if (!have) {
                    sources.push_back(def);
                    pins.push_back(false);
                    migrated = true;
                }
            }
        } else {
            sources = account->default_timelines();
        }
        std::string kinds;
        for (const auto& s : sources)
            kinds += (kinds.empty() ? "" : ",") + std::string(kind_name(s.kind)) +
                     (s.param.empty() ? "" : ":" + s.param);
        log::write("rebuild: " + key + " -> " + std::to_string(sources.size()) + " timelines" +
                   (from_saved ? " (restored)" : " (defaults)") + " [" + kinds + "]");
        auto v = build_timelines_for(account, sources);
        for (size_t i = 0; i < v.size() && i < pins.size(); ++i)
            v[i]->set_pinned(pins[i]);
        if (key == accounts_.selected_key())
            timelines_ = std::move(v);
        else
            parked_[key] = std::move(v);
    }
    if (migrated)
        save_open_timelines(); // persist the newly-added standing timelines
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
    log::write("switch_account: " + old + " -> " + new_key);
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
    tc.set_reversed(settings_.reverse_timelines); // newest-at-bottom for time-ordered feeds
    // Compose the display filter (raw rows are untouched; only visible_ changes):
    //  1. server-side "hide" filters (Mastodon) always apply, everywhere;
    //  2. optionally hide mention notifications from the Notifications timeline;
    //  3. the per-timeline client-side filter, if one is saved.
    const bool hide_mentions = tc.source().kind == TimelineSource::Kind::Notifications &&
                               !settings_.show_mentions_in_notifications;
    ClientFilter client;
    if (auto it = client_filters_.find(tc.cache_key()); it != client_filters_.end())
        client = it->second;
    const bool has_client = client.is_active();
    const std::string me_id = tc.account() ? tc.account()->me().id : std::string{};
    tc.set_filter([hide_mentions, has_client, client, me_id](const TimelineItem& item) {
        if (const Status* s = item.status(); s && s->any_filter_hides())
            return false; // server-side hide
        if (hide_mentions) {
            const Notification* n = item.notification();
            if (n && n->type == Notification::Kind::Mention)
                return false;
        }
        if (has_client && !client_filter_should_show(client, item, me_id))
            return false;
        return true;
    });
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

// --- filters ---

void CoreSession::emit_client_filter() {
    TimelineController* tc = current();
    ClientFilter f;
    if (tc)
        if (auto it = client_filters_.find(tc->cache_key()); it != client_filters_.end())
            f = it->second;
    emit({{"event", "client_filter"},
          {"available", tc != nullptr},
          {"filter", client_filter_to_json(f)}});
}

void CoreSession::cmd_get_client_filter() { emit_client_filter(); }

void CoreSession::cmd_set_client_filter(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    ClientFilter f = client_filter_from_json(cmd.value("filter", json::object()));
    const std::string key = tc->cache_key();
    if (f.is_active())
        client_filters_[key] = f;
    else
        client_filters_.erase(key); // an all-pass filter is the same as none
    save_client_filters();
    apply_timeline_settings(*tc); // set_filter re-applies + emits the visible list
}

void CoreSession::cmd_clear_client_filter() {
    TimelineController* tc = current();
    if (!tc)
        return;
    client_filters_.erase(tc->cache_key());
    save_client_filters();
    apply_timeline_settings(*tc);
}

void CoreSession::cmd_list_server_filters() {
    SocialAccount* acct = accounts_.selected();
    if (!acct || !acct->supports_server_filters()) {
        emit({{"event", "server_filters"}, {"supported", false}, {"filters", json::array()}});
        return;
    }
    worker_.post([this, acct] {
        auto filters = acct->list_server_filters();
        loop_.post([this, filters = std::move(filters)]() mutable {
            json arr = json::array();
            for (const auto& f : filters)
                arr.push_back(server_filter_to_json(f));
            emit({{"event", "server_filters"}, {"supported", true}, {"filters", arr}});
        });
    });
}

void CoreSession::cmd_save_server_filter(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    if (!acct || !acct->supports_server_filters())
        return;
    ServerFilter f = server_filter_from_json(cmd.value("filter", json::object()));
    const bool is_update = !f.id.empty();
    worker_.post([this, acct, f, is_update] {
        const bool ok = is_update ? acct->update_server_filter(f) : acct->create_server_filter(f);
        auto filters = acct->list_server_filters();
        loop_.post([this, ok, is_update, filters = std::move(filters)]() mutable {
            json arr = json::array();
            for (const auto& x : filters)
                arr.push_back(server_filter_to_json(x));
            emit({{"event", "server_filters"}, {"supported", true}, {"filters", arr}});
            emit_announce(ok ? (is_update ? "Filter updated." : "Filter created.")
                             : "Couldn't save the filter.");
        });
    });
}

void CoreSession::cmd_delete_server_filter(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    if (!acct || !acct->supports_server_filters())
        return;
    const std::string id = cmd.value("id", std::string{});
    if (id.empty())
        return;
    worker_.post([this, acct, id] {
        const bool ok = acct->delete_server_filter(id);
        auto filters = acct->list_server_filters();
        loop_.post([this, ok, filters = std::move(filters)]() mutable {
            json arr = json::array();
            for (const auto& x : filters)
                arr.push_back(server_filter_to_json(x));
            emit({{"event", "server_filters"}, {"supported", true}, {"filters", arr}});
            emit_announce(ok ? "Filter deleted." : "Couldn't delete the filter.");
        });
    });
}

std::filesystem::path CoreSession::client_filters_path() const {
    return config_path_.parent_path() / "client_filters.json";
}

void CoreSession::load_client_filters() {
    client_filters_.clear();
    std::ifstream in(client_filters_path(), std::ios::binary);
    if (!in)
        return;
    try {
        json root;
        in >> root;
        if (root.is_object())
            for (const auto& [key, j] : root.items())
                if (j.is_object())
                    client_filters_[key] = client_filter_from_json(j);
    } catch (...) {
    }
}

void CoreSession::save_client_filters() const {
    json root = json::object();
    for (const auto& [key, f] : client_filters_)
        root[key] = client_filter_to_json(f);
    const std::string blob = root.dump();
    const std::filesystem::path path = client_filters_path();
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

std::map<std::string, std::vector<CoreSession::SavedTimeline>>
CoreSession::load_open_timelines() const {
    std::map<std::string, std::vector<SavedTimeline>> out;
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
                            out[key].push_back({*s, j.value("pinned", false)});
    } catch (...) {
    }
    return out;
}

void CoreSession::save_open_timelines() const {
    json root = json::object();
    auto add = [&](const std::string& key,
                   const std::vector<std::unique_ptr<TimelineController>>& v) {
        json arr = json::array();
        for (const auto& tc : v) {
            json j = source_to_json(tc->source());
            if (tc->pinned())
                j["pinned"] = true;
            arr.push_back(std::move(j));
        }
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
    if (ec) {
        std::filesystem::remove(tmp, ec);
        log::write("save_open_timelines: FAILED to write " + path.string());
    } else {
        log::write("save_open_timelines: wrote " + std::to_string(root.size()) + " account(s)");
    }
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
        if (!streams_.empty())
            log::write("update_streaming: streaming disabled, stopping all " +
                       std::to_string(streams_.size()) + " stream(s)");
        streams_.clear(); // stop + join every stream
        return;
    }
    log::write("update_streaming: enabled, " + std::to_string(accounts_.accounts().size()) +
               " account(s), " + std::to_string(streams_.size()) + " existing stream(s)");
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
        log::write("update_streaming: starting stream for " + key);
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
    for (auto it = streams_.begin(); it != streams_.end();) { // drop streams for gone accounts
        if (live.count(it->first)) {
            it = std::next(it);
        } else {
            log::write("update_streaming: dropping stream for gone account " + it->first);
            it = streams_.erase(it);
        }
    }
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
    if (kind == "mutes")
        return TimelineSource::mutes();
    if (kind == "blocks")
        return TimelineSource::blocks();
    if (kind == "follow_requests")
        return TimelineSource::follow_requests();
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
                       {"dismissable", s.is_dismissable() && !tc->pinned()},
                       {"pinned", tc->pinned()},
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
          {"reversed", tc->reversed()}, // UI flips its load-older direction + default row
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
    // A follow-request notification: surface the requester so Enter can accept/reject.
    if (const Notification* n = item.notification();
        n && n->type == Notification::Kind::FollowRequest) {
        r["follow_request"] = true;
        r["account_id"] = n->account.id;
        r["acct"] = n->account.acct.empty() ? n->account.username : n->account.acct;
    }
    return r;
}

} // namespace fastsm
