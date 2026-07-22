#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "fastsm/net/http_client.hpp"
#include "fastsm/platform/mastodon/mastodon_credentials.hpp"
#include "fastsm/presentation/alias_store.hpp"
#include "fastsm/runtime/worker_queue.hpp"
#include "fastsm/sound/sound_manager.hpp"
#include "fastsm/store/account_store.hpp"
#include "fastsm/store/app_settings.hpp"
#include "fastsm/store/timeline_cache.hpp"
#include "fastsm/timeline/client_filter.hpp"
#include "fastsm/timeline/movement.hpp"
#include "fastsm/timeline/streaming_client.hpp"
#include "fastsm/timeline/timeline_controller.hpp"

// CoreSession is the engine's orchestration layer, owned by the C ABI. It runs
// all state on its own "core loop" thread and does blocking I/O on a worker
// thread; commands come in via dispatch() (JSON) and results go out via the emit
// sink (JSON). Front ends never touch C++ types — they exchange JSON only. This
// is where the old Win32 AppController logic lives so every front end shares it.

namespace fastsm {

class CoreSession {
public:
    struct Paths {
        std::filesystem::path config_dir;         // holds config.json + cache/ + soundpacks/
        std::filesystem::path bundled_soundpacks; // packs shipped with the app
        std::filesystem::path bundled_keymaps;    // read-only keymaps shipped with the app
    };

    CoreSession(Paths paths, std::unique_ptr<net::IHttpClient> http,
                std::function<void(const std::string&)> emit);
    ~CoreSession();

    CoreSession(const CoreSession&) = delete;
    CoreSession& operator=(const CoreSession&) = delete;

    // Submit a command (JSON). Non-blocking: queued onto the core-loop thread.
    void dispatch(const std::string& command_json);

private:
    void handle(const nlohmann::json& cmd); // runs on the core-loop thread

    // Command handlers.
    void cmd_start();
    void cmd_get_settings();
    void cmd_update_settings(const nlohmann::json& cmd);
    void cmd_get_account_settings();               // -> account_settings event
    void cmd_set_account_settings(const nlohmann::json& cmd); // {soundpack}
    void cmd_select_timeline(const nlohmann::json& cmd);
    void cmd_select_account(const nlohmann::json& cmd);
    void cmd_refresh();
    void cmd_refresh_all();
    void cmd_load_older(const nlohmann::json& cmd);
    void cmd_load_gap(const nlohmann::json& cmd);
    void cmd_note_selection(const nlohmann::json& cmd);
    // Persist a timeline's reading position (single writer of positions_), and the
    // core-navigation select_row emit that also remembers the position on the way out.
    void remember_position(const TimelineController* tc, const std::string& id);
    void play_row_earcons(const TimelineController* tc, const TimelineItem& item);
    void emit_select_row(const TimelineController* tc, const std::string& id);
    void cmd_toggle_boost(const nlohmann::json& cmd);
    void cmd_toggle_favorite(const nlohmann::json& cmd);
    void cmd_toggle_bookmark(const nlohmann::json& cmd);
    void cmd_report(const nlohmann::json& cmd); // report a post/user to the server's moderators
    void cmd_open_profile_editor(const nlohmann::json& cmd); // fetch your profile -> profile_editor
    void cmd_update_profile(const nlohmann::json& cmd);      // submit display name + bio changes
    void cmd_toggle_pin_post(const nlohmann::json& cmd); // pin/unpin your own post to profile
    void cmd_toggle_mute_conversation(const nlohmann::json& cmd); // mute/unmute a thread's notifs
    void cmd_delete_post(const nlohmann::json& cmd);     // delete your own post
    void cmd_post(const nlohmann::json& cmd);
    void cmd_compose_context(const nlohmann::json& cmd);
    void cmd_open_status(const nlohmann::json& cmd);
    void cmd_open_post_links(const nlohmann::json& cmd); // open links found inside a post
    void cmd_post_info(const nlohmann::json& cmd);
    void cmd_vote_poll(const nlohmann::json& cmd); // {id, choices[]} -> vote + reopen results
    void cmd_play_media(const nlohmann::json& cmd); // {id} -> play the post's media
    void play_one_media(const std::string& url, const std::string& kind, const std::string& title);
    void cmd_move(const nlohmann::json& cmd);
    void cmd_cycle_movement(const nlohmann::json& cmd);
    void cmd_go_back();
    void cmd_get_spawnable();
    void cmd_spawn_timeline(const nlohmann::json& cmd);
    // Refresh the selected account's lists (Mastodon lists / Bluesky curation
    // lists) and Bluesky custom feeds in the background so the New Timeline dialog
    // (Ctrl+T) can offer them instantly from cache.
    void refresh_lists(SocialAccount* account);
    void refresh_feeds(SocialAccount* account);
    // Fetch the account's muted words (Bluesky) in the background, then re-apply
    // the client filter so open timelines mute matching posts.
    void refresh_muted_words(SocialAccount* account);

    // --- Mastodon lists: user membership + list management ---
    void cmd_get_user_lists(const nlohmann::json& cmd); // {account_id, acct} -> user_lists event
    void cmd_set_user_list(const nlohmann::json& cmd);  // {list_id, account_id, add}
    void cmd_list_lists();                              // emit lists event (all lists) for the manager
    void cmd_create_list(const nlohmann::json& cmd);    // {title}
    void cmd_rename_list(const nlohmann::json& cmd);    // {id, title}
    void cmd_delete_list(const nlohmann::json& cmd);    // {id}
    void emit_lists();                                  // lists event {supported, lists:[...]}
    // Run a list create/update/delete on the worker, then re-emit the updated set
    // and announce the outcome.
    void run_list_mutation(SocialAccount* acct, std::function<bool()> op, std::string ok_msg,
                           std::string fail_msg);
    // --- Followed hashtags (Mastodon) ---
    void cmd_follow_hashtag_prompt(const nlohmann::json& cmd); // {id} -> hashtag_prompt event
    void cmd_follow_hashtag(const nlohmann::json& cmd);        // {name}
    void cmd_unfollow_hashtag(const nlohmann::json& cmd);      // {name}
    void cmd_list_followed_hashtags();                         // -> followed_hashtags event
    void emit_followed_hashtags();
    void cmd_list_trending_hashtags();                         // -> trending_hashtags event
    void cmd_open_thread(const nlohmann::json& cmd);
    void cmd_open_status_actors(const nlohmann::json& cmd, bool boosted); // favorited/boosted-by list
    void cmd_open_user_timeline(const nlohmann::json& cmd);
    void cmd_open_user_profile(const nlohmann::json& cmd);
    // Ctrl+; : one user in the focused post -> speak their info (user template);
    // many -> open a timeline of the post's users. Ctrl+Shift+; : speak the post's
    // referenced (in_reply_to) parent; a second press within a moment jumps to it.
    void cmd_speak_user(const nlohmann::json& cmd);
    void cmd_speak_reply(const nlohmann::json& cmd);
    // User aliases (global, cross-account custom display names). begin_alias
    // resolves the focused row's user(s) and emits an alias_prompt; set_alias /
    // clear_alias mutate + persist the store; list_aliases feeds the manager UI.
    void cmd_begin_alias(const nlohmann::json& cmd);
    void emit_alias_prompt(const User& u);
    void cmd_set_alias(const nlohmann::json& cmd);
    void cmd_clear_alias(const nlohmann::json& cmd);
    void cmd_list_aliases();
    // Typeahead for @-mention autocomplete: search accounts by partial handle and
    // emit a "user_suggestions" event (echoing the query so stale replies drop).
    void cmd_autocomplete_users(const nlohmann::json& cmd);
    void speak_user_info(const User& u);          // fetch full profile, speak via template
    void spawn_post_users(const std::vector<User>& users, const std::string& status_id,
                          const std::string& title);
    // User Analysis: fetch the current account's COMPLETE followers + following
    // lists, compute the requested category, and spawn a seeded user timeline of
    // the result. If either list can't be fully loaded (rate limit / failure),
    // announce an error and spawn NOTHING — a partial list is never shown.
    void cmd_analyze_users(const nlohmann::json& cmd);
    void spawn_analyzed_users(const std::vector<User>& users, const std::string& category,
                              const std::string& title);
    // Select (jump to) a row by id if it's in the current timeline; returns true.
    bool jump_to_row(const std::string& row_id);
    // The first loaded copy of a status (by id) across all open timelines, or null.
    const Status* find_status_anywhere(const std::string& status_id) const;
    // Every user referenced by a post (author, mentions, and one level of
    // quoted/boosted author + mentions), deduped — for the u / Ctrl+U picker.
    std::vector<User> users_in_post(const TimelineItem& item) const;
    void emit_user_profile(const User& u);
    void emit_user_picker(const std::string& purpose, const std::string& row_id,
                          const std::vector<User>& users);
    void cmd_set_relationship(const nlohmann::json& cmd);
    void cmd_open_followers(const nlohmann::json& cmd);
    void cmd_open_following(const nlohmann::json& cmd);
    void open_user_list(const nlohmann::json& cmd, bool following);
    void cmd_user_action(const nlohmann::json& cmd); // batch follow/mute/block
    // Follow the post author if not following, unfollow if following. Resolves the
    // target user the same way the u / Ctrl+U actions do (picker for multi-user posts).
    void cmd_follow_toggle(const nlohmann::json& cmd);
    void follow_toggle_user(SocialAccount* acct, const std::string& id, const std::string& handle);
    // Resolve a typed handle to a User off-thread, then run `then` on the loop
    // thread; announces an error if the handle can't be found.
    void resolve_handle(const std::string& handle, std::function<void(const User&)> then);
    void cmd_reorder_timeline(const nlohmann::json& cmd); // move current timeline up/down
    void cmd_toggle_pin(); // pin/unpin the current tab (locks/unlocks dismissal)
    void cmd_toggle_mute(); // mute/unmute the current tab's new-item earcon
    void cmd_toggle_auto_read(); // toggle auto-reading new posts in the current tab
    void cmd_copy(const nlohmann::json& cmd); // copy the focused row to the clipboard
    void cmd_close_timeline();
    void cmd_clear_timeline();
    void cmd_clear_all_timelines();
    void cmd_add_account(const nlohmann::json& cmd);
    // Mastodon OAuth for front ends with their own redirect handling (Android):
    // begin registers the app + emits an open_url; finish exchanges the code.
    void cmd_begin_mastodon_login(const nlohmann::json& cmd);
    void cmd_finish_mastodon_login(const nlohmann::json& cmd);
    void cmd_remove_account(const nlohmann::json& cmd);
    void cmd_play_earcon(const nlohmann::json& cmd);
    void cmd_reset_audio();

    // --- filters (per-timeline client-side + Mastodon server-side) ---
    void cmd_get_client_filter();               // emit the current timeline's client filter
    void cmd_get_speech_catalog();              // -> speech_catalog event (field keys + labels)
    void cmd_get_movement_catalog();            // -> movement_catalog event (unit keys + labels)
    void cmd_set_client_filter(const nlohmann::json& cmd); // {filter} -> save + apply
    void cmd_clear_client_filter();             // drop the current timeline's client filter
    void cmd_list_server_filters();             // emit server_filters {supported, filters}
    void cmd_save_server_filter(const nlohmann::json& cmd);   // create or update
    void cmd_delete_server_filter(const nlohmann::json& cmd); // {id}
    void emit_client_filter();                  // client_filter event for the current timeline
    // Per-timeline client filters (cache_key -> filter), remembered across restarts.
    std::filesystem::path client_filters_path() const;
    void load_client_filters();
    void save_client_filters() const;

    // --- invisible interface (global hotkeys / keyhook / layer) ---
    void cmd_get_action_catalog();
    void cmd_get_keymap(const nlohmann::json& cmd);   // {name?} -> resolved bindings
    void cmd_set_active_keymap(const nlohmann::json& cmd);
    void cmd_save_keymap(const nlohmann::json& cmd);  // {name, overrides, unbinds}
    void cmd_import_keymap(const nlohmann::json& cmd); // {name, text} — from an old FastSM keymap
    void cmd_delete_keymap(const nlohmann::json& cmd);
    // Atomically write a keymap file (temp + rename) so a partial write can't blank it.
    void write_keymap_file(const std::string& name, const std::string& contents);
    void cmd_perform_action(const nlohmann::json& cmd); // {action}
    void cmd_get_layer_keymap();                        // layer bindings + activation combo
    void cmd_set_window_shown(const nlohmann::json& cmd); // persist window visibility
    void cmd_check_for_update(const nlohmann::json& cmd); // {silent} -> update_status event
    void cmd_download_update(const nlohmann::json& cmd);  // {url} -> update_ready/update_error
    // Keymap file location + loading. User keymaps live in <config>/keymaps and are
    // editable; built-in keymaps ship in the app's keymaps folder and are read-only.
    std::filesystem::path keymaps_dir() const; // the user (editable) keymaps dir
    bool is_user_keymap(const std::string& name) const; // has an editable user file
    std::optional<std::filesystem::path> keymap_file(const std::string& name) const; // user, else bundled
    std::vector<std::string> list_keymaps() const; // "default" + user + built-in keymaps
    void emit_keymap(const std::string& name);
    // Invisible navigation helpers: move the current timeline's position without a
    // visible list, emitting select_row (list follows if shown) + a spoken label.
    void invisible_step(int delta);
    void invisible_move_unit(bool down);
    void invisible_goto_edge(bool top);
    // Fetch more posts (gap-fill / load-older) as invisible navigation nears an
    // edge, so it loads content the same way scrolling the window does.
    void invisible_autoload(TimelineController* tc, int visible_index);
    void invisible_speak_index(int visible_index);
    // "<n> of <count>" for the current position in a timeline (or "empty").
    std::string timeline_position_text(const TimelineController* tc) const;

    void rebuild_timelines(); // (re)build timelines for every account
    // Build + warm (cache load + refresh) an account's timelines from these sources.
    std::vector<std::unique_ptr<TimelineController>>
    build_timelines_for(SocialAccount* account, const std::vector<TimelineSource>& sources);
    // The set of open timelines is remembered across restarts (per account) in a
    // small open_timelines.json, so spawned/closed timelines reopen as you left them.
    std::filesystem::path open_timelines_path() const;
    // A saved open timeline: its source plus per-tab state (pinned) that isn't part
    // of the source's identity.
    struct SavedTimeline {
        TimelineSource source;
        bool pinned = false;
        bool muted = false;
        bool auto_read = false;
    };
    std::map<std::string, std::vector<SavedTimeline>> load_open_timelines() const;
    void save_open_timelines() const;
    // Switch the displayed account: park the current, unpark (or build) the target.
    void switch_account(const std::string& new_key);
    void refresh_all_accounts(); // refresh the current + every parked account
    // The timelines for an account (the live list if displayed, else its parked
    // list), or nullptr if the account isn't loaded.
    std::vector<std::unique_ptr<TimelineController>>* timelines_for_account(const std::string& key);
    void spawn_source(const TimelineSource& src); // open a timeline (or focus it)
    std::unique_ptr<TimelineController> make_controller(SocialAccount* account,
                                                        const TimelineSource& src);
    TimelineController* current() const;
    int index_of(const TimelineController* tc) const;
    const TimelineItem* find_item(const TimelineController* tc, const std::string& id) const;
    void apply_settings();
    // Apply the per-timeline settings (refresh depth, the Notifications mentions
    // filter) to one controller. Called for every controller at creation and
    // whenever settings change, so it holds no matter when a timeline is built.
    void apply_timeline_settings(TimelineController& tc);
    void save_config();
    // Per-timeline reading position (cache_key -> selected post id), remembered
    // across restarts in a small positions.json (separate from the item cache).
    std::filesystem::path aliases_path() const;
    void load_aliases();
    void save_aliases() const;
    std::filesystem::path positions_path() const;
    void load_positions();
    void save_positions() const;
    void update_streaming();
    void auto_refresh_loop();
    static std::optional<TimelineSource> source_from_kind(const std::string& kind);

    // Event builders.
    void emit(const nlohmann::json& event);
    void emit_settings(); // settings + available soundpacks
    void emit_account_settings(); // focused account's handle + soundpack + pack list
    void emit_accounts();
    // The soundpack an account should sound in (its override, or the global default).
    std::string soundpack_for(const SocialAccount* account) const;
    void apply_active_soundpack(); // point the mixer at the selected account's pack
    void emit_timelines();
    void emit_timeline(int index);
    // Coalesce a controller's on_change (streaming/refresh) into one emit per
    // loop batch: streaming a firehose fires on_change per post, and a full
    // emit_timeline re-serializes every row — that flooded the loop thread.
    // Marks the controller dirty and posts a single flush; bursts that arrive
    // while the loop is busy collapse to one emit. See emit_dirty_timelines().
    void schedule_timeline_emit(TimelineController* tc);
    void emit_dirty_timelines();
    void emit_announce(const std::string& message);
    void emit_all_timelines();
    nlohmann::json row_json(const TimelineItem& item, std::int64_t now) const;

    std::filesystem::path config_path_;
    std::filesystem::path bundled_keymaps_dir_; // read-only keymaps shipped with the app
    // cache_key -> remembered reading position. The timestamp is a fallback anchor
    // for when the id itself is gone by the next launch (the cache keeps only the
    // newest N rows, so a position in a busy feed regularly falls off the end).
    struct Position {
        std::string id;
        std::int64_t date = 0;
    };
    std::map<std::string, Position> positions_;
    std::map<std::string, ClientFilter> client_filters_; // cache_key -> per-timeline client filter
    std::map<std::string, present::AliasEntry> aliases_; // canonical key -> user alias (global)
    // account_key -> the account's timeline lists, refreshed in the background so
    // Ctrl+T can offer them without a network round trip in the hot path.
    std::map<std::string, std::vector<TimelineList>> lists_by_account_;
    // account_key -> the account's custom feeds (Bluesky), cached like lists_by_account_.
    std::map<std::string, std::vector<TimelineList>> feeds_by_account_;
    // account_key -> the account's muted words (Bluesky), lowercased; applied by
    // apply_timeline_settings as a client-side keyword mute.
    std::map<std::string, std::vector<std::string>> muted_words_by_account_;
    std::function<void(const std::string&)> emit_;

    std::unique_ptr<net::IHttpClient> http_;
    sound::SoundManager sound_;
    store::TimelineCache cache_;
    AccountStore accounts_;
    store::AppSettings settings_;
    std::vector<std::unique_ptr<TimelineController>> timelines_; // the displayed account
    // Every other account's timelines, kept alive + refreshed in the background so
    // switching accounts is instant and warm (keyed by account_key).
    std::map<std::string, std::vector<std::unique_ptr<TimelineController>>> parked_;
    // Closed timelines kept alive (with in-flight async) until shutdown.
    std::vector<std::unique_ptr<TimelineController>> retired_;
    int current_ = 0;

    // Between begin_mastodon_login (app registered) and finish_mastodon_login
    // (code exchanged): the app credentials awaiting the redirect's code.
    MastodonCredentials pending_mastodon_;

    std::vector<MovementUnit> movement_units_ = MovementUnit::catalog();

    // Timelines whose rows changed and need re-emitting, drained by a single
    // flush posted to loop_ (see schedule_timeline_emit). Pointers, not
    // indices, so a close/reorder before the flush can't misdirect the emit.
    std::unordered_set<TimelineController*> dirty_timelines_;
    bool timeline_flush_pending_ = false;
    int movement_unit_ = 0; // currently selected unit (Ctrl+Left/Right cycles)

    // Double-press tracking for Speak-reply (Ctrl+Shift+;): the row spoken last and
    // when (steady-clock ms). A second press on the same row within the window jumps.
    std::string last_speak_reply_row_;
    std::int64_t last_speak_reply_ms_ = 0;

    std::atomic<int> auto_refresh_seconds_{0};
    std::atomic<bool> auto_refresh_running_{true};
    std::thread auto_refresh_thread_;

    // Threads/clients last. Destruction order (reverse) is streams_, worker_,
    // loop_: worker_ (I/O) joins before loop_ (state) since worker tasks post to
    // loop_; the streaming + auto-refresh threads are stopped explicitly in the
    // destructor before any member is torn down. loop_ precedes streams_ so the
    // stream clients can be constructed with &loop_.
    runtime::WorkerQueue loop_;   // owns engine state; the controllers' IMainExecutor
    runtime::WorkerQueue worker_; // blocking network/cache I/O
    // One live streaming connection per streaming-capable account (keyed by
    // account_key), so all accounts stream at once, not just the displayed one.
    std::map<std::string, std::unique_ptr<StreamingClient>> streams_;
};

} // namespace fastsm
