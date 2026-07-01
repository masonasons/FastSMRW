#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "fastsm/net/http_client.hpp"
#include "fastsm/runtime/worker_queue.hpp"
#include "fastsm/sound/sound_manager.hpp"
#include "fastsm/store/account_store.hpp"
#include "fastsm/store/app_settings.hpp"
#include "fastsm/store/timeline_cache.hpp"
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
    void cmd_select_timeline(const nlohmann::json& cmd);
    void cmd_select_account(const nlohmann::json& cmd);
    void cmd_refresh();
    void cmd_refresh_all();
    void cmd_load_older();
    void cmd_load_gap(const nlohmann::json& cmd);
    void cmd_note_selection(const nlohmann::json& cmd);
    void cmd_toggle_boost(const nlohmann::json& cmd);
    void cmd_toggle_favorite(const nlohmann::json& cmd);
    void cmd_post(const nlohmann::json& cmd);
    void cmd_compose_context(const nlohmann::json& cmd);
    void cmd_open_status(const nlohmann::json& cmd);
    void cmd_post_info(const nlohmann::json& cmd);
    void cmd_move(const nlohmann::json& cmd);
    void cmd_cycle_movement(const nlohmann::json& cmd);
    void cmd_go_back();
    void cmd_get_spawnable();
    void cmd_spawn_timeline(const nlohmann::json& cmd);
    void cmd_open_thread(const nlohmann::json& cmd);
    void cmd_open_user_timeline(const nlohmann::json& cmd);
    void cmd_open_user_profile(const nlohmann::json& cmd);
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
    void cmd_close_timeline();
    void cmd_clear_timeline();
    void cmd_clear_all_timelines();
    void cmd_add_account(const nlohmann::json& cmd);
    void cmd_remove_account(const nlohmann::json& cmd);
    void cmd_play_earcon(const nlohmann::json& cmd);

    // --- invisible interface (global hotkeys / keyhook / layer) ---
    void cmd_get_action_catalog();
    void cmd_get_keymap(const nlohmann::json& cmd);   // {name?} -> resolved bindings
    void cmd_set_active_keymap(const nlohmann::json& cmd);
    void cmd_save_keymap(const nlohmann::json& cmd);  // {name, overrides, unbinds}
    void cmd_delete_keymap(const nlohmann::json& cmd);
    void cmd_perform_action(const nlohmann::json& cmd); // {action}
    void cmd_get_layer_keymap();                        // layer bindings + activation combo
    void cmd_set_window_shown(const nlohmann::json& cmd); // persist window visibility
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
    void invisible_goto_edge(bool top);
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
    std::map<std::string, std::vector<TimelineSource>> load_open_timelines() const;
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
    void save_config();
    // Per-timeline reading position (cache_key -> selected post id), remembered
    // across restarts in a small positions.json (separate from the item cache).
    std::filesystem::path positions_path() const;
    void load_positions();
    void save_positions() const;
    void update_streaming();
    void auto_refresh_loop();
    static std::optional<TimelineSource> source_from_kind(const std::string& kind);

    // Event builders.
    void emit(const nlohmann::json& event);
    void emit_settings(); // settings + available soundpacks
    void emit_accounts();
    void emit_timelines();
    void emit_timeline(int index);
    void emit_announce(const std::string& message);
    void emit_all_timelines();
    nlohmann::json row_json(const TimelineItem& item, std::int64_t now) const;

    std::filesystem::path config_path_;
    std::filesystem::path bundled_keymaps_dir_; // read-only keymaps shipped with the app
    std::map<std::string, std::string> positions_; // cache_key -> remembered selected id
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

    std::vector<MovementUnit> movement_units_ = MovementUnit::catalog();
    int movement_unit_ = 0; // currently selected unit (Ctrl+Left/Right cycles)

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
