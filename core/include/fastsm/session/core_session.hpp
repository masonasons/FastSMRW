#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
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
    void cmd_note_selection(const nlohmann::json& cmd);
    void cmd_toggle_boost(const nlohmann::json& cmd);
    void cmd_toggle_favorite(const nlohmann::json& cmd);
    void cmd_post(const nlohmann::json& cmd);
    void cmd_compose_context(const nlohmann::json& cmd);
    void cmd_get_spawnable();
    void cmd_spawn_timeline(const nlohmann::json& cmd);
    void cmd_close_timeline();
    void cmd_clear_timeline();
    void cmd_add_account(const nlohmann::json& cmd);
    void cmd_remove_account(const nlohmann::json& cmd);
    void cmd_play_earcon(const nlohmann::json& cmd);

    void rebuild_timelines();
    std::unique_ptr<TimelineController> make_controller(const TimelineSource& src);
    TimelineController* current() const;
    int index_of(const TimelineController* tc) const;
    const TimelineItem* find_item(const TimelineController* tc, const std::string& id) const;
    void apply_settings();
    void save_config();
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
    std::function<void(const std::string&)> emit_;

    std::unique_ptr<net::IHttpClient> http_;
    sound::SoundManager sound_;
    store::TimelineCache cache_;
    AccountStore accounts_;
    store::AppSettings settings_;
    std::vector<std::unique_ptr<TimelineController>> timelines_;
    // Closed timelines kept alive (with in-flight async) until shutdown.
    std::vector<std::unique_ptr<TimelineController>> retired_;
    int current_ = 0;

    std::atomic<int> auto_refresh_seconds_{0};
    std::atomic<bool> auto_refresh_running_{true};
    std::thread auto_refresh_thread_;

    // Threads/clients last. Destruction order (reverse) is stream_, worker_,
    // loop_: worker_ (I/O) joins before loop_ (state) since worker tasks post to
    // loop_; the streaming + auto-refresh threads are stopped explicitly in the
    // destructor before any member is torn down. loop_ precedes stream_ so
    // stream_ can be constructed with &loop_.
    runtime::WorkerQueue loop_;   // owns engine state; the controllers' IMainExecutor
    runtime::WorkerQueue worker_; // blocking network/cache I/O
    StreamingClient stream_;
};

} // namespace fastsm
