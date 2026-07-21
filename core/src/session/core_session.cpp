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
#include "fastsm/presentation/alias_store.hpp"
#include "fastsm/presentation/reply_helper.hpp"
#include "fastsm/presentation/speech_settings.hpp"
#include "fastsm/presentation/status_presenter.hpp"
#include "fastsm/store/app_config.hpp"
#include "fastsm/update/update_checker.hpp"
#include "fastsm/util/base64.hpp"
#include "fastsm/util/date_parsing.hpp"
#include "fastsm/util/languages.hpp"
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

PostDraft draft_from_json(const json& d, bool mentions_at_end = false) {
    PostDraft draft;
    draft.text = d.value("text", std::string{});
    // Selected reply recipients (Mastodon): the UI sends only the handles the user
    // left checked, in participant order (the person being replied to first).
    if (auto m = d.find("mentions"); m != d.end() && m->is_array()) {
        std::vector<std::string> accts;
        for (const auto& a : *m)
            if (a.is_string())
                accts.push_back(a.get<std::string>());
        if (mentions_at_end && accts.size() > 1) {
            // Keep the person you're replying to (first) up front; append the rest
            // to the end of the post so the visible text starts with your reply.
            std::vector<std::string> rest(accts.begin() + 1, accts.end());
            draft.text = present::mention_prefix({accts.front()}) + draft.text;
            const std::string tail = present::mention_prefix(rest);
            if (!tail.empty()) {
                if (!draft.text.empty() && draft.text.back() != ' ' && draft.text.back() != '\n')
                    draft.text += " ";
                draft.text += tail;
            }
        } else {
            const std::string prefix = present::mention_prefix(accts);
            if (!prefix.empty())
                draft.text = prefix + draft.text;
        }
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
            {"media", f.media},               {"mute_conversations", f.mute_conversations}};
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
    case K::Trends: return "trends";
    case K::Conversations: return "conversations";
    case K::FavoritedBy: return "favoritedBy";
    case K::BoostedBy: return "boostedBy";
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
    if (s == "trends") return K::Trends;
    if (s == "conversations") return K::Conversations;
    if (s == "favoritedBy") return K::FavoritedBy;
    if (s == "boostedBy") return K::BoostedBy;
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
    else if (c == "get_account_settings")
        cmd_get_account_settings();
    else if (c == "set_account_settings")
        cmd_set_account_settings(cmd);
    else if (c == "select_timeline")
        cmd_select_timeline(cmd);
    else if (c == "select_account")
        cmd_select_account(cmd);
    else if (c == "refresh")
        cmd_refresh();
    else if (c == "refresh_all")
        cmd_refresh_all();
    else if (c == "load_older")
        cmd_load_older(cmd);
    else if (c == "load_gap")
        cmd_load_gap(cmd);
    else if (c == "note_selection")
        cmd_note_selection(cmd);
    else if (c == "toggle_boost")
        cmd_toggle_boost(cmd);
    else if (c == "toggle_favorite")
        cmd_toggle_favorite(cmd);
    else if (c == "toggle_bookmark")
        cmd_toggle_bookmark(cmd);
    else if (c == "report")
        cmd_report(cmd);
    else if (c == "open_profile_editor")
        cmd_open_profile_editor(cmd);
    else if (c == "update_profile")
        cmd_update_profile(cmd);
    else if (c == "toggle_pin_post")
        cmd_toggle_pin_post(cmd);
    else if (c == "toggle_mute_conversation")
        cmd_toggle_mute_conversation(cmd);
    else if (c == "delete_post")
        cmd_delete_post(cmd);
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
    else if (c == "open_favorited_by")
        cmd_open_status_actors(cmd, /*boosted=*/false);
    else if (c == "open_reblogged_by")
        cmd_open_status_actors(cmd, /*boosted=*/true);
    else if (c == "open_user_timeline")
        cmd_open_user_timeline(cmd);
    else if (c == "open_user_profile")
        cmd_open_user_profile(cmd);
    else if (c == "speak_user")
        cmd_speak_user(cmd);
    else if (c == "begin_alias")
        cmd_begin_alias(cmd);
    else if (c == "set_alias")
        cmd_set_alias(cmd);
    else if (c == "clear_alias")
        cmd_clear_alias(cmd);
    else if (c == "list_aliases")
        cmd_list_aliases();
    else if (c == "speak_reply")
        cmd_speak_reply(cmd);
    else if (c == "autocomplete_users")
        cmd_autocomplete_users(cmd);
    else if (c == "set_relationship")
        cmd_set_relationship(cmd);
    else if (c == "open_followers")
        cmd_open_followers(cmd);
    else if (c == "open_following")
        cmd_open_following(cmd);
    else if (c == "analyze_users")
        cmd_analyze_users(cmd);
    else if (c == "user_action")
        cmd_user_action(cmd);
    else if (c == "follow_toggle")
        cmd_follow_toggle(cmd);
    else if (c == "reorder_timeline")
        cmd_reorder_timeline(cmd);
    else if (c == "toggle_pin")
        cmd_toggle_pin();
    else if (c == "toggle_mute")
        cmd_toggle_mute();
    else if (c == "toggle_auto_read")
        cmd_toggle_auto_read();
    else if (c == "copy")
        cmd_copy(cmd);
    else if (c == "close_timeline")
        cmd_close_timeline();
    else if (c == "clear_timeline")
        cmd_clear_timeline();
    else if (c == "clear_all_timelines")
        cmd_clear_all_timelines();
    else if (c == "add_account")
        cmd_add_account(cmd);
    else if (c == "begin_mastodon_login")
        cmd_begin_mastodon_login(cmd);
    else if (c == "finish_mastodon_login")
        cmd_finish_mastodon_login(cmd);
    else if (c == "remove_account")
        cmd_remove_account(cmd);
    else if (c == "play_earcon")
        cmd_play_earcon(cmd);
    else if (c == "reset_audio")
        cmd_reset_audio();
    else if (c == "get_action_catalog")
        cmd_get_action_catalog();
    else if (c == "get_keymap")
        cmd_get_keymap(cmd);
    else if (c == "set_active_keymap")
        cmd_set_active_keymap(cmd);
    else if (c == "save_keymap")
        cmd_save_keymap(cmd);
    else if (c == "import_keymap")
        cmd_import_keymap(cmd);
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
    else if (c == "get_speech_catalog")
        cmd_get_speech_catalog();
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
    else if (c == "list_trending_hashtags")
        cmd_list_trending_hashtags();
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
            load_aliases(); // global user aliases (before timelines build so rows render aliased)
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

// The redirect URI Android registers and catches via a fastsm://oauth deep link.
static constexpr const char* kMastodonRedirect = "fastsm://oauth";

void CoreSession::cmd_begin_mastodon_login(const json& cmd) {
    const std::string instance = cmd.value("instance", std::string{});
    emit_announce("Authorizing in your browser…");
    worker_.post([this, instance] {
        MastodonAuth auth(http_.get());
        MastodonBeginResult begun = auth.begin_login(instance, kMastodonRedirect);
        loop_.post([this, begun = std::move(begun)]() mutable {
            if (begun.ok) {
                pending_mastodon_ = begun.credentials;
                emit({{"event", "open_url"}, {"url", begun.authorize_url}});
            } else {
                emit({{"event", "auth_result"}, {"ok", false}, {"error", begun.error}});
            }
        });
    });
}

void CoreSession::cmd_finish_mastodon_login(const json& cmd) {
    const std::string code = cmd.value("code", std::string{});
    const MastodonCredentials creds = pending_mastodon_; // captured on the loop thread
    if (creds.client_id.empty()) {
        emit({{"event", "auth_result"}, {"ok", false}, {"error", "No pending login"}});
        return;
    }
    emit_announce("Finishing login…");
    worker_.post([this, creds, code] {
        MastodonAuth auth(http_.get());
        MastodonLoginResult r = auth.finish_login(creds, code, kMastodonRedirect);
        loop_.post([this, r = std::move(r)]() mutable {
            if (r.ok) {
                store::StoredCredential cred;
                cred.mastodon = r.credentials;
                auto account = std::make_unique<MastodonAccount>(r.credentials, r.me, http_.get());
                const std::string key = account->account_key();
                SocialAccount* acct_ptr = account.get();
                accounts_.add(std::move(account), cred);
                worker_.post([acct_ptr] { acct_ptr->load_configuration(); });
                switch_account(key);
                save_config();
                emit_accounts();
                emit_timelines();
                emit_all_timelines();
                pending_mastodon_ = {};
            }
            emit({{"event", "auth_result"}, {"ok", r.ok}, {"error", r.error}});
        });
    });
}

void CoreSession::cmd_remove_account(const json& cmd) {
    const std::string removed = cmd.value("key", std::string{});
    const bool was_selected = removed == accounts_.selected_key();
    accounts_.remove(removed); // may auto-select the first remaining account
    settings_.account_soundpacks.erase(removed); // drop its per-account soundpack
    save_config();
    parked_.erase(removed);
    if (was_selected) {
        timelines_.clear(); // drop the removed account's (displayed) timelines
        current_ = 0;
        apply_active_soundpack(); // follow the newly-selected account's pack
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

std::string CoreSession::soundpack_for(const SocialAccount* account) const {
    if (account) {
        auto it = settings_.account_soundpacks.find(account->account_key());
        if (it != settings_.account_soundpacks.end() && !it->second.empty())
            return it->second;
    }
    return settings_.soundpack;
}

void CoreSession::apply_active_soundpack() {
    sound_.set_soundpack(soundpack_for(accounts_.selected()));
}

void CoreSession::cmd_get_account_settings() { emit_account_settings(); }

void CoreSession::cmd_set_account_settings(const json& cmd) {
    SocialAccount* a = accounts_.selected();
    if (!a)
        return;
    if (cmd.contains("soundpack")) {
        settings_.account_soundpacks[a->account_key()] = cmd.value("soundpack", std::string{});
        apply_active_soundpack(); // this IS the selected account, so it takes effect now
    }
    save_config();
    emit_settings(); // keep the main Settings snapshot fresh (it round-trips this map)
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
        // Visiting Notifications clears the unread badge (Bluesky updateSeen).
        if (tc->source().kind == TimelineSource::Kind::Notifications && tc->account()) {
            SocialAccount* a = tc->account();
            worker_.post([a] { a->mark_notifications_seen(); });
        }
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

void CoreSession::cmd_load_older(const json& cmd) {
    // "automatic" marks a load the UI triggered from navigation rather than the
    // user asking; those get gated so a sparse feed isn't paged back forever.
    if (TimelineController* tc = current())
        tc->load_older(cmd.value("automatic", false));
}

void CoreSession::cmd_note_selection(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const std::string id = cmd.value("id", std::string{});
    tc->note_selection(id);
    remember_position(tc, id);
}

// Persist a timeline's reading position across restarts. This is the single
// writer of positions_ -- called both from the GUI ListView's selection echo
// (cmd_note_selection) and from every core-driven cursor move (emit_select_row),
// so keyboard / invisible-interface / Go-Back navigation is remembered too, not
// just mouse/arrow selection in the visible window.
void CoreSession::remember_position(const TimelineController* tc, const std::string& id) {
    if (!tc || id.empty())
        return;
    Position& slot = positions_[tc->cache_key()];
    if (slot.id == id)
        return;
    slot.id = id;
    // Store when that row was posted too, so a later launch can land nearby even if
    // the row itself is no longer cached.
    slot.date = 0;
    for (const auto& item : tc->items())
        if (item.id() == id) {
            slot.date = item.sort_date();
            play_row_earcons(tc, item); // per-post navigation earcons for the row we landed on
            break;
        }
    save_positions();
}

// Short indicator sounds as the cursor lands on a post: one per attribute the post
// carries (pinned, poll, image/other media, mentions-you). Each is independently
// toggleable in settings; the mention cue is skipped in the mentions/notifications
// buffers where it would be redundant. Fires only on a genuine cursor move (its sole
// caller, remember_position, is guarded against re-selecting the same row).
void CoreSession::play_row_earcons(const TimelineController* tc, const TimelineItem& item) {
    const Status* s = item.actionable_status();
    if (!s)
        return;
    const std::string pack = soundpack_for(tc->account());
    if (settings_.earcon_pinned && s->pinned)
        sound_.play_named("pinned", pack);
    if (settings_.earcon_poll && s->poll)
        sound_.play_named("poll", pack);
    // An image takes priority over other media (matches FastSM), but each cue is
    // independently toggleable.
    bool has_image = false, has_other = false;
    for (const auto& m : s->media_attachments) {
        if (m.type == MediaAttachment::Kind::Image)
            has_image = true;
        else
            has_other = true;
    }
    if (has_image) {
        if (settings_.earcon_image)
            sound_.play_named("image", pack);
    } else if (has_other && settings_.earcon_media) {
        sound_.play_named("media", pack);
    }
    // A post that mentions you — redundant in the mentions/notifications buffers.
    if (settings_.earcon_mention && tc->account() && !tc->source().is_notification_timeline()) {
        const std::string& me = tc->account()->me().id;
        for (const auto& mn : s->mentions)
            if (mn.id == me) {
                sound_.play_named("mention", pack);
                break;
            }
    }
}

// Tell the UI to select a row after a core-driven navigation, remembering it as
// the reading position on the way out.
void CoreSession::emit_select_row(const TimelineController* tc, const std::string& id) {
    remember_position(tc, id);
    emit({{"event", "select_row"}, {"id", id}});
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
        // "Sent" — your own posts as a standard user timeline. Available on every
        // platform. Hidden once it's open (its cache key matches your own user
        // timeline), matching the built-in dedup above.
        {
            const std::string sent_key = TimelineSource::user_posts(account->me().id).cache_key();
            bool open = false;
            for (auto& tc : timelines_)
                if (tc->source().cache_key() == sent_key) {
                    open = true;
                    break;
                }
            if (!open)
                tls.push_back({{"kind", "sent"}, {"title", "Sent"}});
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
        } else if (account->platform() == Platform::Bluesky) {
            tls.push_back({{"kind", "hashtag"}, {"title", "Hashtag"}, {"input", "Hashtag"}});
            tls.push_back({{"kind", "search_posts"}, {"title", "Search Posts"}, {"input", "Search"}});
            tls.push_back(
                {{"kind", "search_people"}, {"title", "Search People"}, {"input", "Search"}});
            // The account's curation lists and custom feeds, offered from cache
            // (each refreshed below for next time). "param" carries the at-uri,
            // which the UI echoes back on spawn.
            const std::string akey = account->account_key();
            auto push_cached = [&](const std::map<std::string, std::vector<TimelineList>>& cache,
                                   const char* kind_str, const std::string& ck_prefix,
                                   const std::string& title_prefix) {
                auto it = cache.find(akey);
                if (it == cache.end())
                    return;
                for (const auto& l : it->second) {
                    const std::string ck = ck_prefix + l.id;
                    bool open = false;
                    for (auto& tc : timelines_)
                        if (tc->source().cache_key() == ck) {
                            open = true;
                            break;
                        }
                    if (!open)
                        tls.push_back({{"kind", kind_str},
                                       {"param", l.id},
                                       {"title", title_prefix + l.title}});
                }
            };
            push_cached(lists_by_account_, "list", "list:", "List: ");
            push_cached(feeds_by_account_, "feed", "feed:", "Feed: ");
            refresh_lists(account);
            refresh_feeds(account);
        }
    }
    emit({{"event", "spawnable_timelines"}, {"timelines", tls}});
}

void CoreSession::refresh_lists(SocialAccount* account) {
    if (!account)
        return;
    const std::string key = account->account_key();
    worker_.post([this, account, key] {
        auto lists = account->lists();
        loop_.post([this, key, lists = std::move(lists)]() mutable {
            lists_by_account_[key] = std::move(lists);
        });
    });
}

void CoreSession::refresh_feeds(SocialAccount* account) {
    if (!account)
        return;
    const std::string key = account->account_key();
    worker_.post([this, account, key] {
        auto feeds = account->saved_feeds();
        loop_.post([this, key, feeds = std::move(feeds)]() mutable {
            feeds_by_account_[key] = std::move(feeds);
        });
    });
}

void CoreSession::refresh_muted_words(SocialAccount* account) {
    if (!account)
        return;
    const std::string key = account->account_key();
    worker_.post([this, account, key] {
        auto words = account->muted_words();
        loop_.post([this, key, words = std::move(words)]() mutable {
            auto& slot = muted_words_by_account_[key];
            if (slot == words)
                return; // unchanged (e.g. empty -> empty): nothing to re-render
            slot = std::move(words);
            // Re-apply the filter to every open + parked timeline, then push the
            // refreshed visible lists so newly-muted posts disappear at once.
            for (auto& tc : timelines_)
                apply_timeline_settings(*tc);
            for (auto& [k, v] : parked_)
                for (auto& tc : v)
                    apply_timeline_settings(*tc);
            emit_all_timelines();
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
    // Chime that a new timeline opened (matches FastSM's "open" sound).
    sound_.play_named("open", soundpack_for(accounts_.selected()));
    TimelineController* p = timelines_.back().get();
    p->set_origin_key(origin);
    // Restore this timeline's remembered position (works for spawned kinds too).
    if (auto it = positions_.find(p->cache_key()); it != positions_.end())
        p->set_position_hint(it->second.id, it->second.date);
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
    // A Bluesky custom feed: "param" is the feed generator at-uri; look up its
    // title from the cached feed set for a nice "Feed: <name>" heading.
    if (kind == "feed") {
        const std::string id = cmd.value("param", std::string{});
        if (id.empty())
            return;
        std::string title = "Feed";
        if (SocialAccount* a = accounts_.selected())
            if (auto it = feeds_by_account_.find(a->account_key()); it != feeds_by_account_.end())
                for (const auto& f : it->second)
                    if (f.id == id) {
                        title = f.title;
                        break;
                    }
        spawn_source(TimelineSource::feed(id, "Feed: " + title));
        return;
    }
    // "Sent" is your own posts, shown as a standard (dismissable, pinnable) user
    // timeline. It reuses UserPosts with your account id, titled "Sent".
    if (kind == "sent") {
        spawn_source(TimelineSource::user_posts(accounts_.selected()->me().id, "Sent"));
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

void CoreSession::cmd_list_trending_hashtags() {
    SocialAccount* acct = accounts_.selected();
    if (!acct || !acct->features().follow_hashtags) {
        emit({{"event", "trending_hashtags"}, {"supported", false}, {"tags", json::array()}});
        return;
    }
    worker_.post([this, acct] {
        auto tags = acct->trending_hashtags();
        loop_.post([this, tags = std::move(tags)]() mutable {
            emit({{"event", "trending_hashtags"},
                  {"supported", true},
                  {"tags", followed_tags_to_json(tags)}});
        });
    });
}

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

void CoreSession::cmd_open_status_actors(const json& cmd, bool boosted) {
    TimelineController* tc = current();
    if (!tc || !accounts_.selected())
        return;
    const TimelineItem* item = find_item(tc, cmd.value("id", std::string{}));
    if (!item)
        return;
    const Status* s = item->actionable_status();
    if (!s)
        return;
    spawn_source(boosted ? TimelineSource::boosted_by(s->id, "Boosted by")
                         : TimelineSource::favorited_by(s->id, "Favorited by"));
}

std::vector<User> CoreSession::users_in_post(const TimelineItem& item) const {
    std::vector<User> users;
    if (const User* row_user = item.user()) { // a user-list row is just that user
        users.push_back(*row_user);
        return users;
    }
    // A notification's actor (the follower/requester/faver/booster). Follow and
    // follow-request notifications carry no post, so this is the only user on the
    // row — without it, "Open user timeline/profile" and "Speak user" do nothing.
    // Seeded first so it's the primary choice; the status pass below dedups by id.
    if (const Notification* n = item.notification(); n && !n->account.id.empty())
        users.push_back(n->account);
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
    if (id.empty() || action.empty()) {
        // Accepting/declining a follow request with an unresolved account id used to
        // fail silently, reading as "Enter does nothing". Surface it instead.
        if (action == "authorize_request" || action == "reject_request") {
            sound_.play(sound::Earcon::Error);
            emit_announce("Couldn't identify the follow request to act on.");
        }
        return;
    }
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

void CoreSession::cmd_report(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    if (!acct)
        return;
    ReportDraft draft;
    draft.account_id = cmd.value("account_id", std::string{});
    draft.comment = cmd.value("comment", std::string{});
    draft.category = cmd.value("category", std::string("other"));
    draft.forward = cmd.value("forward", false);
    // Reporting a specific post: resolve its author and include the post itself
    // (with its cid, which Bluesky strong refs require).
    if (const std::string row = cmd.value("id", std::string{}); !row.empty()) {
        if (TimelineController* tc = current())
            if (const TimelineItem* item = find_item(tc, row))
                if (const Status* s = item->actionable_status()) {
                    if (draft.account_id.empty())
                        draft.account_id = s->account.id;
                    draft.status_ids.push_back(s->id);
                    draft.status_cids.push_back(s->cid.value_or(std::string{}));
                }
    }
    if (auto it = cmd.find("status_ids"); it != cmd.end() && it->is_array())
        for (const auto& sid : *it)
            if (sid.is_string())
                draft.status_ids.push_back(sid.get<std::string>());
    if (draft.account_id.empty()) {
        sound_.play(sound::Earcon::Error);
        emit_announce("Couldn't identify who to report.");
        return;
    }
    worker_.post([this, acct, draft] {
        const bool ok = acct->report(draft);
        loop_.post([this, ok] {
            sound_.play(ok ? sound::Earcon::PostSent : sound::Earcon::Error);
            emit_announce(ok ? "Report submitted" : "Report failed");
        });
    });
}

void CoreSession::cmd_open_profile_editor(const json&) {
    SocialAccount* acct = accounts_.selected();
    if (!acct)
        return;
    // Bluesky profiles are just a display name + bio (+ avatar); "simple" tells the
    // UI to hide the Mastodon-only metadata fields, privacy, and account flags.
    const bool simple = acct->platform() != Platform::Mastodon;
    worker_.post([this, acct, simple] {
        auto src = acct->profile_source();
        loop_.post([this, src, simple] {
            if (!src) {
                sound_.play(sound::Earcon::Error);
                emit_announce("Couldn't load your profile.");
                return;
            }
            json fields = json::array();
            for (const auto& f : src->fields)
                fields.push_back({{"name", f.name}, {"value", f.value}});
            emit({{"event", "profile_editor"},
                  {"simple", simple},
                  {"display_name", src->display_name},
                  {"note", src->note},
                  {"locked", src->locked},
                  {"bot", src->bot},
                  {"discoverable", src->discoverable},
                  {"privacy", src->privacy},
                  {"sensitive", src->sensitive},
                  {"max_fields", src->max_fields},
                  {"fields", fields}});
        });
    });
}

void CoreSession::cmd_update_profile(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    if (!acct)
        return;
    ProfileSource p;
    p.display_name = cmd.value("display_name", std::string{});
    p.note = cmd.value("note", std::string{});
    p.locked = cmd.value("locked", false);
    p.bot = cmd.value("bot", false);
    p.discoverable = cmd.value("discoverable", false);
    p.privacy = cmd.value("privacy", std::string("public"));
    p.sensitive = cmd.value("sensitive", false);
    if (auto it = cmd.find("fields"); it != cmd.end() && it->is_array())
        for (const auto& row : *it)
            p.fields.push_back({row.value("name", std::string{}), row.value("value", std::string{})});
    worker_.post([this, acct, p] {
        const bool ok = acct->update_profile(p);
        loop_.post([this, ok] {
            sound_.play(ok ? sound::Earcon::PostSent : sound::Earcon::Error);
            emit_announce(ok ? "Profile updated" : "Profile update failed");
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

void CoreSession::follow_toggle_user(SocialAccount* acct, const std::string& id,
                                     const std::string& handle) {
    if (!acct || id.empty())
        return;
    worker_.post([this, acct, id, handle] {
        // Look up the current relationship so we know which way to toggle. If we
        // can't tell (no relationship support / a lookup failure), assume we're not
        // following yet and follow.
        std::optional<Relationship> rel = acct->relationship(id);
        const bool following = rel && (rel->following || rel->requested);
        const std::string action = following ? "unfollow" : "follow";
        const bool ok = following ? acct->unfollow(id) : acct->follow(id);
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

void CoreSession::cmd_follow_toggle(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    if (!acct)
        return;
    // A specific user was chosen (from the picker menu).
    if (cmd.contains("account_id")) {
        const std::string aid = cmd.value("account_id", std::string{});
        follow_toggle_user(acct, aid, cmd.value("acct", std::string{}));
        return;
    }
    // A handle was typed manually ("Type a handle…"): resolve it, then toggle.
    if (cmd.contains("handle")) {
        resolve_handle(cmd.value("handle", std::string{}), [this](const User& u) {
            if (SocialAccount* a = accounts_.selected())
                follow_toggle_user(a, u.id, u.acct.empty() ? u.username : u.acct);
        });
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
    // With "pick" the UI always wants the menu; otherwise a lone user toggles straight.
    if (users.size() == 1 && !cmd.value("pick", false)) {
        const User& u = users.front();
        follow_toggle_user(acct, u.id, u.acct.empty() ? u.username : u.acct);
        return;
    }
    emit_user_picker("follow_toggle", row_id, users); // let the UI pick which user
}

void CoreSession::resolve_handle(const std::string& handle,
                                 std::function<void(const User&)> then) {
    SocialAccount* acct = accounts_.selected();
    if (!acct || handle.empty())
        return;
    worker_.post([this, acct, handle, then = std::move(then)]() mutable {
        std::optional<User> u = acct->lookup_user(handle);
        loop_.post([this, u = std::move(u), then = std::move(then), handle]() mutable {
            if (!u || u->id.empty()) {
                sound_.play(sound::Earcon::Error);
                emit_announce("Couldn't find " + handle);
                return;
            }
            then(*u);
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
    // A handle was typed manually ("Type a handle…"): resolve it, then open.
    if (cmd.contains("handle")) {
        resolve_handle(cmd.value("handle", std::string{}), [this](const User& u) {
            spawn_source(
                TimelineSource::user_posts(u.id, "@" + (u.acct.empty() ? u.username : u.acct)));
        });
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
    // With "pick" the UI always wants the menu (so "Type a handle…" is reachable);
    // otherwise a lone user opens straight away.
    if (users.size() == 1 && !cmd.value("pick", false)) {
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
    // A handle was typed manually ("Type a handle…"): resolve it, then show it.
    if (cmd.contains("handle")) {
        resolve_handle(cmd.value("handle", std::string{}),
                       [this](const User& u) { emit_user_profile(u); });
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
    // With "pick" the UI always wants the menu; otherwise a lone user opens straight.
    if (users.size() == 1 && !cmd.value("pick", false)) {
        emit_user_profile(users.front());
        return;
    }
    emit_user_picker("profile", row_id, users);
}

// Ctrl+; : speak the focused post's user(s) using the user speech template. A lone
// user is spoken; several open a navigable timeline of the post's users.
void CoreSession::cmd_speak_user(const json& cmd) {
    TimelineController* tc = current();
    const std::string row = cmd.value("id", std::string{});
    const TimelineItem* item = tc ? find_item(tc, row) : nullptr;
    if (!item)
        return;
    const std::vector<User> users = users_in_post(*item);
    if (users.empty())
        return;
    if (users.size() == 1) {
        speak_user_info(users.front());
        return;
    }
    std::string title = "Users in post";
    if (const Status* s = item->actionable_status(); s && !s->account.acct.empty())
        title = "Users in @" + s->account.acct + "'s post";
    spawn_post_users(users, row, title);
}

void CoreSession::speak_user_info(const User& u) {
    SocialAccount* acct = accounts_.selected();
    const User user = u;
    // Enrich a sparse mention (no bio/counts) off-thread, then speak the row label
    // composed from the user speech template (Speech -> Configure Users).
    worker_.post([this, acct, user] {
        User full = user;
        if (acct && !user.id.empty())
            if (auto p = acct->fetch_profile(user.id))
                full = *p;
        std::string text = present::accessibility_label(full);
        loop_.post([this, text = std::move(text)] { emit_announce(text); });
    });
}

namespace {
std::string trim_alias(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
} // namespace

// Aliases are global and cross-account. begin_alias resolves the focused row's
// user(s); a lone user (or one picked from the menu) yields an alias_prompt the
// UI turns into a text dialog, and several users open the picker first.
void CoreSession::cmd_begin_alias(const json& cmd) {
    if (!accounts_.selected())
        return;
    if (cmd.contains("handle")) { // typed a handle from the picker's manual entry
        resolve_handle(cmd.value("handle", std::string{}),
                       [this](const User& u) { emit_alias_prompt(u); });
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
    if (cmd.contains("account_id")) { // a specific user was chosen from the picker
        const std::string aid = cmd.value("account_id", std::string{});
        for (const User& u : users)
            if (u.id == aid) {
                emit_alias_prompt(u);
                return;
            }
        return;
    }
    if (users.size() == 1 && !cmd.value("pick", false)) {
        emit_alias_prompt(users.front());
        return;
    }
    emit_user_picker("alias", row_id, users);
}

void CoreSession::emit_alias_prompt(const User& u) {
    const std::string key = present::Aliases::key_for(u);
    std::string current_alias;
    if (auto it = aliases_.find(key); it != aliases_.end())
        current_alias = it->second.alias;
    emit({{"event", "alias_prompt"},
          {"key", key},
          {"handle", u.acct.empty() ? u.username : u.acct},
          {"current", current_alias}});
}

void CoreSession::cmd_set_alias(const json& cmd) {
    const std::string key = cmd.value("key", std::string{});
    if (key.empty())
        return;
    const std::string handle = cmd.value("handle", std::string{});
    const std::string alias = trim_alias(cmd.value("alias", std::string{}));
    if (alias.empty()) { // empty value clears the alias (matches FastSM)
        cmd_clear_alias({{"key", key}, {"handle", handle}});
        return;
    }
    present::AliasEntry& e = aliases_[key];
    e.alias = alias;
    if (!handle.empty())
        e.handle = handle; // keep the display handle fresh
    save_aliases();
    present::Aliases::set_current(aliases_);
    emit_all_timelines(); // re-render every open row with the new alias
    emit_announce(handle.empty() ? "Alias set" : "Alias set for @" + handle);
}

void CoreSession::cmd_clear_alias(const json& cmd) {
    const std::string key = cmd.value("key", std::string{});
    if (key.empty())
        return;
    std::string handle = cmd.value("handle", std::string{});
    if (auto it = aliases_.find(key); it != aliases_.end() && handle.empty())
        handle = it->second.handle;
    if (aliases_.erase(key) == 0)
        return;
    save_aliases();
    present::Aliases::set_current(aliases_);
    emit_all_timelines();
    emit_announce(handle.empty() ? "Alias removed" : "Alias removed for @" + handle);
}

void CoreSession::cmd_list_aliases() {
    json arr = json::array();
    for (const auto& [key, e] : aliases_)
        arr.push_back({{"key", key}, {"handle", e.handle}, {"alias", e.alias}});
    emit({{"event", "aliases_list"}, {"aliases", std::move(arr)}});
}

void CoreSession::cmd_autocomplete_users(const json& cmd) {
    const std::string query = cmd.value("query", std::string{});
    SocialAccount* acct = accounts_.selected();
    // Echo the query back so the UI can drop a stale reply (the user kept typing
    // while this one was in flight). Empty query / no account -> no suggestions.
    if (!acct || query.empty()) {
        emit({{"event", "user_suggestions"}, {"query", query}, {"users", json::array()}});
        return;
    }
    worker_.post([this, acct, query] {
        std::vector<User> users = acct->search_accounts(query, 8);
        loop_.post([this, query, users = std::move(users)]() mutable {
            json arr = json::array();
            for (const auto& u : users) {
                const std::string handle = u.acct.empty() ? u.username : u.acct;
                std::string label = handle.empty() ? u.display_name : ("@" + handle);
                if (!u.display_name.empty() && u.display_name != handle)
                    label = u.display_name + " (@" + handle + ")";
                arr.push_back({{"id", u.id},
                               {"acct", handle},
                               {"display", u.display_name},
                               {"label", label}});
            }
            emit({{"event", "user_suggestions"}, {"query", query}, {"users", std::move(arr)}});
        });
    });
}

void CoreSession::spawn_post_users(const std::vector<User>& users, const std::string& status_id,
                                   const std::string& title) {
    const TimelineSource src = TimelineSource::post_users(status_id, title);
    for (size_t i = 0; i < timelines_.size(); ++i)
        if (timelines_[i]->source().cache_key() == src.cache_key()) {
            current_ = static_cast<int>(i); // already open -> focus it
            emit_timelines();
            return;
        }
    std::string origin;
    if (current_ >= 0 && current_ < static_cast<int>(timelines_.size()))
        origin = timelines_[static_cast<size_t>(current_)]->source().cache_key();
    timelines_.push_back(make_controller(accounts_.selected(), src));
    current_ = static_cast<int>(timelines_.size()) - 1;
    sound_.play_named("open", soundpack_for(accounts_.selected()));
    TimelineController* p = timelines_.back().get();
    p->set_origin_key(origin);
    emit_timelines();
    p->seed_users(users); // show the (possibly sparse) rows immediately
    // Enrich sparse mentions with full profiles (bio/counts) off-thread, then
    // re-seed — unless the tab was closed meanwhile.
    SocialAccount* acct = accounts_.selected();
    worker_.post([this, p, acct, users] {
        std::vector<User> full = users;
        if (acct)
            for (auto& u : full)
                if (!u.id.empty())
                    if (auto pr = acct->fetch_profile(u.id))
                        u = *pr;
        loop_.post([this, p, full = std::move(full)]() mutable {
            if (index_of(p) >= 0)
                p->seed_users(std::move(full));
        });
    });
}

void CoreSession::cmd_analyze_users(const json& cmd) {
    SocialAccount* acct = accounts_.selected();
    if (!acct) {
        emit_announce("No account selected.");
        return;
    }
    const std::string category = cmd.value("category", std::string{"not_following_back"});
    const std::string me_id = acct->me().id;
    if (me_id.empty()) {
        emit_announce("Couldn't determine your account.");
        return;
    }
    emit_announce("Analyzing your follow lists…");
    // Both complete lists are pulled on the worker thread (each may be many pages).
    worker_.post([this, acct, me_id, category] {
        FullRelationResult followers = acct->fetch_all_relations(me_id, /*following=*/false);
        FullRelationResult following = acct->fetch_all_relations(me_id, /*following=*/true);
        loop_.post([this, category, followers = std::move(followers),
                    following = std::move(following)]() mutable {
            using S = FullRelationResult::Status;
            // All-or-nothing: unless BOTH lists came back complete, show an error
            // and no list — never a partial/false result.
            if (followers.status != S::Ok || following.status != S::Ok) {
                const bool rate_limited =
                    followers.status == S::RateLimited || following.status == S::RateLimited;
                sound_.play(sound::Earcon::Error);
                emit_announce(rate_limited
                                  ? "Couldn't finish loading your follow lists — the server "
                                    "rate-limited the request. Please try again in a few minutes."
                                  : "Couldn't load your follow lists. Please try again.");
                return;
            }
            std::set<std::string> follower_ids, following_ids;
            for (const auto& u : followers.users)
                follower_ids.insert(u.id);
            for (const auto& u : following.users)
                following_ids.insert(u.id);

            std::vector<User> result;
            std::string label;
            if (category == "no_followback") {
                // People you follow who don't follow you back.
                for (const auto& u : following.users)
                    if (!follower_ids.count(u.id))
                        result.push_back(u);
                label = "Don't follow you back";
            } else if (category == "mutuals") {
                // Users you both follow.
                for (const auto& u : following.users)
                    if (follower_ids.count(u.id))
                        result.push_back(u);
                label = "Mutual follows";
            } else {
                // Default: people who follow you that you don't follow back.
                for (const auto& u : followers.users)
                    if (!following_ids.count(u.id))
                        result.push_back(u);
                label = "You don't follow back";
            }
            const std::string title = label + " (" + std::to_string(result.size()) + ")";
            spawn_analyzed_users(result, category, title);
        });
    });
}

void CoreSession::spawn_analyzed_users(const std::vector<User>& users, const std::string& category,
                                       const std::string& title) {
    const TimelineSource src = TimelineSource::analyzed_users(category, title);
    // Re-running an analysis that's already open re-seeds it with fresh results
    // (the lists are always fetched live) and focuses it, keeping its title current.
    for (size_t i = 0; i < timelines_.size(); ++i)
        if (timelines_[i]->source().cache_key() == src.cache_key()) {
            timelines_[i]->set_source_title(title);
            timelines_[i]->seed_users(users);
            current_ = static_cast<int>(i);
            emit_timelines();
            return;
        }
    std::string origin;
    if (current_ >= 0 && current_ < static_cast<int>(timelines_.size()))
        origin = timelines_[static_cast<size_t>(current_)]->source().cache_key();
    timelines_.push_back(make_controller(accounts_.selected(), src));
    current_ = static_cast<int>(timelines_.size()) - 1;
    sound_.play_named("open", soundpack_for(accounts_.selected()));
    TimelineController* p = timelines_.back().get();
    p->set_origin_key(origin);
    emit_timelines();
    p->seed_users(users);
}

bool CoreSession::jump_to_row(const std::string& row_id) {
    TimelineController* tc = current();
    if (!tc)
        return false;
    const int idx = tc->visible_index_of(row_id);
    if (idx < 0)
        return false;
    tc->note_selection(row_id);
    emit_select_row(tc, row_id);
    invisible_speak_index(idx);
    return true;
}

const Status* CoreSession::find_status_anywhere(const std::string& status_id) const {
    if (status_id.empty())
        return nullptr;
    for (const auto& tc : timelines_)
        for (const auto& item : tc->items())
            if (const Status* s = item.actionable_status())
                if (s->id == status_id)
                    return s;
    return nullptr;
}

// Ctrl+Shift+; : speak the post this reply is replying to. A second press on the
// same row within the double-press window jumps to that parent instead.
void CoreSession::cmd_speak_reply(const json& cmd) {
    TimelineController* tc = current();
    const std::string row = cmd.value("id", std::string{});
    const TimelineItem* item = tc ? find_item(tc, row) : nullptr;
    if (!item)
        return;
    const Status* s = item->actionable_status();
    if (!s || !s->in_reply_to_id || s->in_reply_to_id->empty()) {
        emit_announce("Not a reply.");
        return;
    }
    const std::string parent_id = *s->in_reply_to_id;

    // A front end can ask outright to jump (Android exposes speak / jump as two
    // TalkBack actions). Otherwise it's a jump only on a quick second press of the
    // same row (Windows / the invisible interface: press once to hear, twice to go).
    bool jump;
    if (cmd.contains("jump")) {
        jump = cmd.value("jump", false);
    } else {
        constexpr std::int64_t kDoublePressMs = 600;
        const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count();
        jump = row == last_speak_reply_row_ && (now_ms - last_speak_reply_ms_) <= kDoublePressMs;
        last_speak_reply_row_ = row;
        last_speak_reply_ms_ = now_ms;
    }

    if (jump) {
        // Jump to the parent if it's in the current timeline; else open the thread,
        // which contains it.
        if (jump_to_row("s:" + parent_id))
            return;
        cmd_open_thread({{"id", row}});
        return;
    }

    // Single press: speak the parent — a loaded copy if we have one, else fetch it.
    if (const Status* loaded = find_status_anywhere(parent_id)) {
        emit_announce(present::accessibility_label(TimelineItem{*loaded}, util::now_unix()));
        return;
    }
    SocialAccount* acct = accounts_.selected();
    worker_.post([this, acct, parent_id] {
        std::optional<Status> fetched;
        if (acct)
            fetched = acct->fetch_status(parent_id);
        loop_.post([this, fetched = std::move(fetched)] {
            if (fetched)
                emit_announce(
                    present::accessibility_label(TimelineItem{*fetched}, util::now_unix()));
            else
                emit_announce("Could not find the post this is replying to.");
        });
    });
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

void CoreSession::cmd_toggle_mute() {
    TimelineController* tc = current();
    if (!tc)
        return;
    const bool now_muted = !tc->muted();
    tc->set_muted(now_muted);
    save_open_timelines(); // mute state survives a restart
    // Chime before we go quiet / after we come back so the toggle is audible.
    sound_.play(now_muted ? sound::Earcon::Unfavorite : sound::Earcon::Favorite);
    emit_timelines(); // refresh the UI's muted flag
    emit_announce(tc->source().title() + (now_muted ? ", muted" : ", unmuted"));
}

void CoreSession::cmd_toggle_auto_read() {
    TimelineController* tc = current();
    if (!tc)
        return;
    const bool on = !tc->auto_read();
    tc->set_auto_read(on);
    save_open_timelines(); // auto-read state survives a restart
    sound_.play(on ? sound::Earcon::Favorite : sound::Earcon::Unfavorite);
    emit_timelines(); // refresh the UI's auto-read flag
    emit_announce(tc->source().title() + (on ? ", auto-read on" : ", auto-read off"));
}

void CoreSession::cmd_copy(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const TimelineItem* item = find_item(tc, cmd.value("id", std::string{}));
    if (!item) {
        sound_.play(sound::Earcon::Error);
        return;
    }
    const std::string text = present::copy_label(*item, util::now_unix());
    if (text.empty()) {
        sound_.play(sound::Earcon::Error);
        return;
    }
    // The front end writes its native clipboard; the string is composed here.
    emit({{"event", "copy_to_clipboard"}, {"text", text}});
    emit_announce("Copied");
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
    tc->on_new_items = nullptr;
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
    update_streaming(); // a streamable timeline may have closed -> drop its stream
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
    // Chime only once the server confirms (no sound on un-boost, matching before).
    tc->toggle_boost(idx, [this](bool ok, bool active) {
        if (!ok)
            sound_.play(sound::Earcon::Error);
        else if (active)
            sound_.play(sound::Earcon::Boost);
    });
}

void CoreSession::cmd_toggle_favorite(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const int idx = tc->visible_index_of(cmd.value("id", std::string{}));
    if (idx < 0)
        return;
    // Chime only once the server confirms the (un)favorite.
    tc->toggle_favorite(idx, [this](bool ok, bool active) {
        sound_.play(!ok ? sound::Earcon::Error
                        : (active ? sound::Earcon::Favorite : sound::Earcon::Unfavorite));
    });
}

void CoreSession::cmd_toggle_bookmark(const json& cmd) {
    TimelineController* tc = current();
    if (!tc || !tc->account())
        return;
    if (!tc->account()->features().bookmarks) {
        sound_.play(sound::Earcon::Error);
        emit_announce("Bookmarks aren't supported on this account.");
        return;
    }
    const int idx = tc->visible_index_of(cmd.value("id", std::string{}));
    if (idx < 0)
        return;
    // Chime only once the server confirms the (un)bookmark.
    tc->toggle_bookmark(idx, [this](bool ok, bool active) {
        if (!ok) {
            sound_.play(sound::Earcon::Error);
            return;
        }
        sound_.play(active ? sound::Earcon::Favorite : sound::Earcon::Unfavorite);
        emit_announce(active ? "Bookmarked" : "Bookmark removed");
    });
}

void CoreSession::cmd_toggle_mute_conversation(const json& cmd) {
    TimelineController* tc = current();
    if (!tc || !tc->account())
        return;
    if (!tc->account()->features().mute_conversations) {
        sound_.play(sound::Earcon::Error);
        emit_announce("Muting conversations isn't supported on this account.");
        return;
    }
    const int idx = tc->visible_index_of(cmd.value("id", std::string{}));
    if (idx < 0)
        return;
    tc->toggle_mute_conversation(idx, [this](bool ok, bool active) {
        if (!ok) {
            sound_.play(sound::Earcon::Error);
            return;
        }
        sound_.play(active ? sound::Earcon::Favorite : sound::Earcon::Unfavorite);
        emit_announce(active ? "Conversation muted" : "Conversation unmuted");
    });
}

void CoreSession::cmd_toggle_pin_post(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const int idx = tc->visible_index_of(cmd.value("id", std::string{}));
    if (idx < 0)
        return;
    const int r = tc->toggle_pin_post(idx, [this](bool ok, bool active) {
        if (!ok) {
            sound_.play(sound::Earcon::Error);
            return;
        }
        sound_.play(active ? sound::Earcon::Favorite : sound::Earcon::Unfavorite);
        emit_announce(active ? "Pinned to your profile" : "Unpinned from your profile");
    });
    if (r < 0) { // not your own post (or not pinnable) — synchronous rejection
        sound_.play(sound::Earcon::Error);
        emit_announce("You can only pin your own posts to your profile.");
    }
}

void CoreSession::cmd_delete_post(const json& cmd) {
    TimelineController* tc = current();
    if (!tc || !tc->account())
        return;
    SocialAccount* account = tc->account();
    const std::string row_id = cmd.value("id", std::string{});
    const TimelineItem* item = find_item(tc, row_id);
    if (!item)
        return;
    const Status* s = item->actionable_status();
    if (!s) {
        sound_.play(sound::Earcon::Error);
        return;
    }
    // Only your OWN posts can be deleted.
    if (s->account.id != account->me().id) {
        sound_.play(sound::Earcon::Error);
        emit_announce("You can only delete your own posts.");
        return;
    }
    const Status target = *s;
    const std::string post_id = s->id;
    worker_.post([this, account, target, post_id] {
        const bool ok = account->delete_post(target);
        loop_.post([this, ok, post_id] {
            if (!ok) {
                sound_.play(sound::Earcon::Error);
                emit_announce("Delete failed");
                return;
            }
            for (auto& tc2 : timelines_) // drop it from every open tab it appears in
                tc2->remove_status(post_id);
            sound_.play(sound::Earcon::Delete);
            emit_announce("Post deleted");
        });
    });
}

void CoreSession::cmd_post(const json& cmd) {
    TimelineController* tc = current();
    if (!tc)
        return;
    PostDraft draft =
        draft_from_json(cmd.value("draft", json::object()), settings_.reply_mentions_at_end);
    const std::string edit_id = cmd.value("edit_id", std::string{});
    // A reply gets its own chime (send_reply); new posts/quotes use send_post.
    const bool is_reply = draft.reply_to_id.has_value() && !draft.reply_to_id->empty();
    auto done = [this, is_reply](bool ok) {
        if (!ok)
            sound_.play(sound::Earcon::Error);
        else
            sound_.play(is_reply ? sound::Earcon::ReplySent : sound::Earcon::PostSent);
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
    // The language list (code + display name) so the UI can offer a picker; it's
    // static reference data that lives in the core, like all other strings.
    {
        json langs = json::array();
        for (const auto& [code, name] : util::languages())
            langs.push_back({{"code", code}, {"name", name}});
        ctx["languages"] = std::move(langs);
    }

    const Status* target = nullptr;
    const User* booster = nullptr;
    if (!id.empty())
        if (const TimelineItem* item = find_item(tc, id)) {
            target = item->actionable_status();
            // A boost carries the booster on the wrapper; offer them as a recipient.
            if (const Status* wrapper = item->status(); wrapper && wrapper->reblog)
                booster = &wrapper->account;
        }

    // A reply/quote/edit without a resolvable target used to fall through to the
    // plain "New Post" branch below — so the compose box opened with no
    // reply_to_id and the user's reply went out as a standalone post. Refuse
    // instead: losing the thread silently is worse than not opening the box.
    if ((mode == "reply" || mode == "quote" || mode == "edit") && !target) {
        emit_announce(mode == "reply"   ? "Can't reply — that post is no longer loaded."
                      : mode == "quote" ? "Can't quote — that post is no longer loaded."
                                        : "Can't edit — that post is no longer loaded.");
        return;
    }

    if (mode == "reply" && target) {
        ctx["title"] = "Reply";
        ctx["context_label"] = "Replying to " + target->account.best_name() + ": " + target->text;
        // Recipients as a togglable checklist (Mastodon). The UI mentions only the
        // ones left checked; the mention text is no longer baked into the body.
        if (account->platform() == Platform::Mastodon) {
            json parts = json::array();
            for (const auto& p : present::reply_participants(*target, account->me(), booster))
                parts.push_back(
                    {{"acct", p.acct}, {"display_name", p.display_name}, {"checked", p.checked}});
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
        // You can only edit your own posts. Opening an edit field on someone
        // else's post is deceptive — the changes can't be applied and are
        // silently discarded — so refuse it up front.
        if (target->account.id != account->me().id) {
            emit_announce("You can only edit your own posts.");
            return;
        }
        if (!account->features().editing) {
            emit_announce("Editing posts isn't supported on this account.");
            return;
        }
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
               {"has_url", !s->url.empty()},
               {"is_mine", s->account.id == tc->account()->me().id},
               {"muted", s->muted},
               {"favorites_count", s->favourites_count},
               {"boosts_count", s->boosts_count}};
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
    // One media_open event carries the kind; each front end decides how to render
    // (the Win32 app streams audio in-app and opens other kinds externally; a
    // native mobile app shows an in-app image/video viewer).
    emit({{"event", "media_open"}, {"url", url}, {"kind", kind}, {"title", title}});
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
    emit_select_row(tc, id);
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
        emit_select_row(tc, id);
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

void CoreSession::cmd_reset_audio() {
    // The front end saw the OS resume from sleep/hibernation; rebuild the audio
    // device so earcons keep sounding. Runs on the core loop, like every other
    // sound_ call, so there's no cross-thread access to the engine.
    log::write("reset_audio: reinitializing the sound engine after resume");
    sound_.reinitialize();
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

void CoreSession::write_keymap_file(const std::string& name, const std::string& contents) {
    std::error_code ec;
    std::filesystem::create_directories(keymaps_dir(), ec);
    const auto path = keymaps_dir() / (name + ".keymap");
    const auto tmp = keymaps_dir() / (name + ".keymap.tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out << contents;
    }
    std::filesystem::rename(tmp, path, ec); // atomic-ish replace; original stays intact on failure
    if (ec)
        std::filesystem::remove(tmp, ec);
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
    write_keymap_file(name, input::serialize_keymap(overrides, unbinds));
    emit_keymap(name);
}

void CoreSession::cmd_import_keymap(const json& cmd) {
    const std::string name = cmd.value("name", std::string{});
    const std::string text = cmd.value("text", std::string{});
    if (name.empty() || name == "default")
        return;
    int dropped = 0;
    std::set<std::string> unbinds;
    std::map<std::string, std::string> overrides =
        input::import_fastsm_keymap(text, &dropped, &unbinds);
    write_keymap_file(name, input::serialize_keymap(overrides, unbinds));
    settings_.invisible_keymap = name; // make the freshly-imported keymap active
    save_config();
    emit_settings(); // keep the UI's active-keymap name fresh before the keymap event
    emit_keymap(name);
    std::string msg = "Imported " + std::to_string(overrides.size()) + " shortcut" +
                      (overrides.size() == 1 ? "" : "s") + " into " + name;
    if (dropped > 0)
        msg += "; skipped " + std::to_string(dropped) + " with no FastSMRW equivalent";
    emit_announce(msg);
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
    emit_select_row(tc, id); // visible list follows if the window is shown
    invisible_speak_index(dest);
}

void CoreSession::invisible_move_unit(bool down) {
    TimelineController* tc = current();
    if (!tc)
        return;
    const auto& items = tc->items();
    if (items.empty() || movement_units_.empty()) {
        sound_.play(sound::Earcon::Boundary);
        return;
    }
    if (movement_unit_ < 0 || movement_unit_ >= static_cast<int>(movement_units_.size()))
        movement_unit_ = 0;
    int idx = tc->visible_index_of(tc->selected_id());
    if (idx < 0)
        idx = 0;
    const int dest = movement::destination(
        items, idx, movement_units_[static_cast<size_t>(movement_unit_)], down);
    if (dest < 0) {
        sound_.play(sound::Earcon::Boundary); // nowhere to jump for this unit
        return;
    }
    const std::string id = items[static_cast<size_t>(dest)].id();
    tc->note_selection(id);
    emit_select_row(tc, id);
    invisible_speak_index(dest);
    invisible_autoload(tc, dest); // may land on a gap / scrollback boundary
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
    emit_select_row(tc, id);
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
    if (near_edge) // automatic: won't keep re-paging a feed that has nothing more to give
        tc->load_older(/*automatic=*/true);
}

void CoreSession::cmd_set_window_shown(const json& cmd) {
    settings_.window_shown = cmd.value("shown", true);
    save_config(); // lightweight: just persist, no re-render
}

void CoreSession::cmd_check_for_update(const json& cmd) {
    const bool silent = cmd.value("silent", false);
    // A front end can force a branch (Android always checks version-tagged "stable"
    // releases, since it has no embedded build commit to compare against "latest").
    const std::string branch = cmd.value("branch", settings_.update_branch);
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
                  {"apk_url", info.apk_url},
                  {"dmg_url", info.dmg_url},
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
        return invisible_move_unit(true);
    if (a == "prev_item_jump")
        return invisible_move_unit(false);
    if (a == "next_movement")
        return cmd_cycle_movement({{"dir", "next"}});
    if (a == "prev_movement")
        return cmd_cycle_movement({{"dir", "prev"}});
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
    if (a == "MuteTimeline")
        return cmd_toggle_mute();
    if (a == "AutoRead")
        return cmd_toggle_auto_read();
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
    if (a == "BookmarkToggle")
        return cmd_toggle_bookmark({{"id", row}});
    if (a == "Copy")
        return cmd_copy({{"id", row}});
    if (a == "PinPost")
        return cmd_toggle_pin_post({{"id", row}});
    if (a == "MuteConversation")
        return cmd_toggle_mute_conversation({{"id", row}});
    if (a == "DeletePost")
        return cmd_delete_post({{"id", row}});
    if (a == "FollowHashtag")
        return cmd_follow_hashtag_prompt({{"id", row}});
    if (a == "ManageHashtags")
        return cmd_list_followed_hashtags();
    if (a == "UpdateProfile")
        return cmd_open_profile_editor({});
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
        // A grouped like/boost notification: Enter opens the list of everyone in the
        // group (all the people who favorited / boosted the post).
        if (const Notification* n = it->notification();
            n && n->notifications_count > 1 && n->status &&
            (n->type == Notification::Kind::Favourite || n->type == Notification::Kind::Reblog))
            return cmd_open_status_actors({{"id", row}}, n->type == Notification::Kind::Reblog);
        // The Conversations feed is a list of threads: Enter always opens the thread.
        if (tc->source().enter_opens_thread())
            return cmd_open_thread({{"id", row}});
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
        return cmd_open_user_timeline({{"id", row}, {"pick", true}});
    if (a == "UserProfile")
        return cmd_open_user_profile({{"id", row}, {"pick", true}});
    if (a == "SpeakUser")
        return cmd_speak_user({{"id", row}});
    if (a == "AddAlias")
        return cmd_begin_alias({{"id", row}});
    if (a == "SpeakReply")
        return cmd_speak_reply({{"id", row}});
    if (a == "FollowToggle")
        return cmd_follow_toggle({{"id", row}, {"pick", true}});
    if (a == "CloseTimeline")
        return cmd_close_timeline();
    if (a == "AccountSettings")
        return cmd_get_account_settings(); // emits account_settings -> UI opens the dialog
    // UI-only actions the app carries out (window/dialogs/find/stop speech).
    if (a == "ToggleWindow" || a == "Options" || a == "KeymapManager" ||
        a == "StopMedia" || a == "Find" || a == "FindNext" || a == "FindPrev" ||
        a == "EnterLayer" || a == "NewTimeline" || a == "Exit") {
        emit({{"event", "invisible_ui_action"}, {"action", a}});
        return;
    }
    // MuteToggle / BlockToggle need relationship round-trips; deferred to a later
    // phase. (FollowToggle is handled above via cmd_follow_toggle.)
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
            tc->set_position_hint(it->second.id, it->second.date);
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
        refresh_muted_words(account); // load keyword mutes (Bluesky) for the filter
        const std::string key = account->account_key();
        std::vector<TimelineSource> sources;
        std::vector<bool> pins;
        std::vector<bool> mutes;
        std::vector<bool> auto_reads;
        bool from_saved = false;
        if (auto it = saved.find(key); it != saved.end() && !it->second.empty()) {
            from_saved = true;
            for (const SavedTimeline& st : it->second) {
                sources.push_back(st.source);
                pins.push_back(st.pinned);
                mutes.push_back(st.muted);
                auto_reads.push_back(st.auto_read);
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
                    mutes.push_back(false);
                    auto_reads.push_back(false);
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
        for (size_t i = 0; i < v.size() && i < mutes.size(); ++i)
            v[i]->set_muted(mutes[i]);
        for (size_t i = 0; i < v.size() && i < auto_reads.size(); ++i)
            v[i]->set_auto_read(auto_reads[i]);
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
    apply_active_soundpack(); // foreground earcons now sound in the new account's pack
    current_ = 0;
    if (auto it = parked_.find(new_key); it != parked_.end()) {
        timelines_ = std::move(it->second); // unpark the warm timelines
        parked_.erase(it);
    } else if (SocialAccount* a = accounts_.selected()) {
        timelines_ = build_timelines_for(a, a->default_timelines());
    }
    refresh_muted_words(accounts_.selected()); // keep keyword mutes current for this account
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
    tc->on_received_new = [this, p](int n, bool has_direct) {
        if (n <= 0 || p->muted()) // muted tab: new items arrive silently
            return;
        const std::string pack = soundpack_for(p->account()); // sound in that account's pack
        // A direct message / direct mention gets the "messages" chime instead of
        // the usual mentions/notification sound (matches FastSM).
        if (has_direct && p->source().is_notification_timeline()) {
            sound_.play_named("messages", pack);
            return;
        }
        if (auto name = p->source().new_items_sound_name())
            sound_.play_named(*name, pack);
    };
    tc->on_new_items = [this, p](const std::vector<TimelineItem>& items) {
        if (!p->auto_read() || items.empty())
            return;
        // Above a handful at once, summarize rather than reading each (Python parity).
        constexpr size_t kReadThreshold = 4;
        if (items.size() >= kReadThreshold) {
            emit_announce(std::to_string(items.size()) + " new in " + p->source().title());
            return;
        }
        // items arrive newest-first; read them oldest-first so they flow in order.
        const std::int64_t now = util::now_unix();
        std::string speech;
        for (auto it = items.rbegin(); it != items.rend(); ++it) {
            const std::string s = present::autoread_label(*it, now);
            if (s.empty())
                continue;
            if (!speech.empty())
                speech += ". ";
            speech += s;
        }
        if (!speech.empty())
            emit_announce(speech);
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
    // Client-side keyword mutes (Bluesky muted words), lowercased.
    std::vector<std::string> muted;
    if (tc.account())
        if (auto it = muted_words_by_account_.find(tc.account()->account_key());
            it != muted_words_by_account_.end())
            muted = it->second;
    tc.set_filter([hide_mentions, has_client, client, me_id, muted](const TimelineItem& item) {
        if (const Status* s = item.status(); s && s->any_filter_hides())
            return false; // server-side hide
        if (hide_mentions) {
            const Notification* n = item.notification();
            if (n && n->type == Notification::Kind::Mention)
                return false;
        }
        if (has_client && !client_filter_should_show(client, item, me_id))
            return false;
        // Keyword mute: hide a post whose text or hashtags contain a muted word.
        if (!muted.empty())
            if (const Status* s = item.status()) {
                const Status& d = s->display_status();
                std::string hay = d.text;
                for (const auto& t : d.tags) {
                    hay += ' ';
                    hay += t;
                }
                for (char& c : hay)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (const auto& w : muted)
                    if (hay.find(w) != std::string::npos)
                        return false;
            }
        return true;
    });
}

void CoreSession::apply_settings() {
    sound_.set_enabled(settings_.sounds_enabled);
    sound_.set_volume(std::clamp(settings_.sound_volume, 0, 100) / 100.0f);
    apply_active_soundpack(); // the selected account's pack (or the global default)
    present::SpeechConfig::set_current(settings_.speech);
    present::TextConfig::set_current(settings_.text);
    cache_.set_max_entries(settings_.cache_limit);
    if (settings_.cache_limit <= 0)
        cache_.clear_all(); // caching turned off: wipe any existing cache files now
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

// The speech-field catalog: every field's stable key + human label, per
// category, so a bus front end (e.g. the Mac app) can label the reorderable
// "Speech Details" lists. Order + enabled state come from the settings event's
// speech arrays (which are normalized to include every field).
void CoreSession::cmd_get_speech_catalog() {
    using namespace fastsm::present;
    auto status_fields = {StatusSpeechField::BoostedBy, StatusSpeechField::Author,
                          StatusSpeechField::Handle, StatusSpeechField::ContentWarning,
                          StatusSpeechField::Text, StatusSpeechField::Quote,
                          StatusSpeechField::Media, StatusSpeechField::Poll,
                          StatusSpeechField::Time, StatusSpeechField::Stats,
                          StatusSpeechField::Favorited, StatusSpeechField::Boosted,
                          StatusSpeechField::Visibility, StatusSpeechField::Source};
    auto user_fields = {UserSpeechField::Author, UserSpeechField::Handle, UserSpeechField::Bot,
                        UserSpeechField::Locked, UserSpeechField::Bio, UserSpeechField::Followers,
                        UserSpeechField::Following, UserSpeechField::Posts};
    auto notif_fields = {NotificationSpeechField::Actor, NotificationSpeechField::Action,
                         NotificationSpeechField::Handle, NotificationSpeechField::Text,
                         NotificationSpeechField::Time};
    auto build = [](auto fields) {
        json arr = json::array();
        for (auto f : fields)
            arr.push_back({{"key", field_key(f)}, {"label", field_display_name(f)}});
        return arr;
    };
    emit({{"event", "speech_catalog"},
          {"status", build(status_fields)},
          {"user", build(user_fields)},
          {"notification", build(notif_fields)},
          // The auto-read and copy templates reuse the same field sets as their kind.
          {"autoread", build(status_fields)},
          {"copy_status", build(status_fields)},
          {"copy_user", build(user_fields)},
          {"copy_notification", build(notif_fields)}});
}

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

std::filesystem::path CoreSession::aliases_path() const {
    return config_path_.parent_path() / "aliases.json";
}

void CoreSession::load_aliases() {
    aliases_.clear();
    std::ifstream in(aliases_path(), std::ios::binary);
    if (in) {
        try {
            json root;
            in >> root;
            if (root.is_object())
                for (const auto& [key, j] : root.items())
                    if (j.is_object()) {
                        present::AliasEntry e;
                        e.alias = j.value("alias", std::string{});
                        e.handle = j.value("handle", std::string{});
                        if (!e.alias.empty())
                            aliases_[key] = std::move(e);
                    }
        } catch (...) {
        }
    }
    present::Aliases::set_current(aliases_); // publish to the presenters
}

void CoreSession::save_aliases() const {
    json root = json::object();
    for (const auto& [key, e] : aliases_)
        root[key] = {{"alias", e.alias}, {"handle", e.handle}};
    const std::string blob = root.dump(1);
    const std::filesystem::path path = aliases_path();
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
            for (const auto& [key, val] : root.items()) {
                if (val.is_string()) // pre-0.4.2 shape: just the id
                    positions_[key] = {val.get<std::string>(), 0};
                else if (val.is_object() && val.contains("id"))
                    positions_[key] = {val.value("id", std::string{}),
                                       val.value("date", std::int64_t{0})};
            }
    } catch (...) {
    }
}

void CoreSession::save_positions() const {
    // Tiny file (a handful of entries), written from the core-loop thread; separate
    // from the item cache so it never races the (worker-thread) cache writes.
    json root = json::object();
    for (const auto& [key, pos] : positions_)
        root[key] = {{"id", pos.id}, {"date", pos.date}};
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
                            out[key].push_back({*s, j.value("pinned", false),
                                                j.value("muted", false),
                                                j.value("auto_read", false)});
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
            if (tc->source().is_static()) // synthetic (e.g. post-users): can't be restored
                continue;
            json j = source_to_json(tc->source());
            if (tc->pinned())
                j["pinned"] = true;
            if (tc->muted())
                j["muted"] = true;
            if (tc->auto_read())
                j["auto_read"] = true;
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
    // One live SSE connection per (account, stream URL) covering every OPEN
    // timeline whose source can stream -- home/notifications (shared user stream),
    // local, federated, each hashtag, each list. Keyed by "account_key\nurl" so
    // Home and Notifications collapse onto one connection. Events route to the
    // matching open timeline (by cache_key) whether it's displayed or parked.
    std::set<std::string> live;
    for (SocialAccount* account : accounts_.accounts()) {
        const std::string key = account->account_key();
        auto* tls = timelines_for_account(key);
        if (!tls)
            continue;
        for (const auto& tc : *tls) {
            const TimelineSource src = tc->source();
            auto spec = account->stream_request_for(src);
            if (!spec)
                continue; // this timeline can't stream (bookmarks, searches, Bluesky, ...)
            const std::string skey = key + "\n" + spec->url;
            if (!live.insert(skey).second)
                continue; // already ensured this connection (e.g. Home after Notifications)
            auto& client = streams_[skey];
            if (client)
                continue; // already streaming
            // The source whose "update" events this connection carries. The user
            // stream (home + notifications + conversations) feeds Home; notification
            // and conversation events self-route in parse_stream_event.
            TimelineSource route =
                (src.kind == TimelineSource::Kind::Notifications ||
                 src.kind == TimelineSource::Kind::Conversations)
                    ? TimelineSource::home()
                    : src;
            log::write("update_streaming: starting stream " + skey);
            client = std::make_unique<StreamingClient>(http_.get(), &loop_);
            client->start(account, *spec, route, [this, key](StreamItem item) {
                auto* dest = timelines_for_account(key);
                if (!dest)
                    return;
                // An edited post: replace it in place wherever it appears.
                if (item.op == StreamItem::Op::Update) {
                    if (const Status* s = item.item.status())
                        for (auto& tc : *dest)
                            tc->update_status(*s);
                    return;
                }
                // A deleted post: drop it from every tab.
                if (item.op == StreamItem::Op::Delete) {
                    if (!item.removed_id.empty())
                        for (auto& tc : *dest)
                            tc->remove_status(item.removed_id);
                    return;
                }
                const std::string target = item.target.cache_key();
                // A streamed mention notification also feeds an open Mentions
                // timeline -- which stores the post itself, not the notification row.
                const Notification* n = item.item.notification();
                const bool is_mention =
                    n && n->type == Notification::Kind::Mention && n->status;
                // A streamed post also belongs in an open user timeline for its
                // author -- most visibly your own "Sent" tab, but also any user's
                // posts you're watching. /accounts/:id/statuses would list it, so
                // without this the tab only updates on a manual refresh or restart.
                const Status* posted = item.item.status();
                for (auto& tc : *dest) {
                    const TimelineSource& s = tc->source();
                    if (s.cache_key() == target) {
                        // Notifications fold into their server-side group ("A and N
                        // others"); everything else prepends as its own row.
                        if (n)
                            tc->ingest_notification(*n);
                        else
                            tc->ingest_realtime(TimelineItem{item.item});
                    } else if (is_mention && s.kind == TimelineSource::Kind::Mentions)
                        tc->ingest_realtime(TimelineItem{*n->status});
                    else if (posted && s.kind == TimelineSource::Kind::UserPosts &&
                             s.param == posted->account.id)
                        tc->ingest_realtime(TimelineItem{*posted});
                }
            });
        }
    }
    for (auto it = streams_.begin(); it != streams_.end();) { // drop unwanted / gone streams
        if (live.count(it->first)) {
            it = std::next(it);
        } else {
            log::write("update_streaming: dropping stream " + it->first);
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
    if (kind == "trends")
        return TimelineSource::trends();
    if (kind == "conversations")
        return TimelineSource::conversations();
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

void CoreSession::emit_account_settings() {
    SocialAccount* a = accounts_.selected();
    json packs = json::array();
    for (const auto& p : sound_.list_soundpacks())
        packs.push_back(p);
    emit({{"event", "account_settings"},
          {"account_key", a ? a->account_key() : std::string{}},
          {"acct", a ? a->me().acct : std::string{}},
          {"soundpack", soundpack_for(a)}, // the effective pack, for pre-selection
          {"soundpacks", std::move(packs)}});
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
                       {"muted", tc->muted()},
                       {"auto_read", tc->auto_read()},
                       {"user_list", s.is_user_list()},
                       {"enter_opens_thread", s.enter_opens_thread()}});
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
        r["bookmarked"] = s->bookmarked;
        if (s->favourites_count > 0)
            r["favorites_count"] = s->favourites_count; // gates "See who favorited"
        if (s->boosts_count > 0)
            r["boosts_count"] = s->boosts_count; // gates "See who boosted"
        if (s->muted)
            r["muted"] = true; // conversation muted -> Status menu shows a check
        if (!s->media_attachments.empty())
            r["has_media"] = true; // gates the "View media" action
        if (s->in_reply_to_id && !s->in_reply_to_id->empty())
            r["is_reply"] = true; // gates the "Speak referenced reply" actions
        if (SocialAccount* me = accounts_.selected())
            r["is_mine"] = s->account.id == me->me().id; // your own post -> deletable
    }
    // A grouped like/boost notification: Enter opens the list of everyone in it.
    if (const Notification* n = item.notification();
        n && n->notifications_count > 1 && n->status &&
        (n->type == Notification::Kind::Favourite || n->type == Notification::Kind::Reblog))
        r["group_actors"] = n->type == Notification::Kind::Reblog ? "reblogged_by" : "favorited_by";
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
