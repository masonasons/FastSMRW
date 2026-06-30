#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fastsm/net/http_client.hpp"
#include "fastsm/runtime/worker_queue.hpp"
#include "fastsm/store/account_store.hpp"
#include "fastsm/store/app_settings.hpp"
#include "fastsm/store/timeline_cache.hpp"
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
        std::filesystem::path config_dir;        // holds config.json + cache/
        std::filesystem::path bundled_soundpacks; // packs shipped with the app
    };

    // `http` is the platform transport (WinHTTP today; injectable for tests).
    // `emit` is called with a serialized event (JSON) on the core-loop thread;
    // the front end marshals it to its own UI thread.
    CoreSession(Paths paths, std::unique_ptr<net::IHttpClient> http,
                std::function<void(const std::string&)> emit);
    ~CoreSession();

    CoreSession(const CoreSession&) = delete;
    CoreSession& operator=(const CoreSession&) = delete;

    // Submit a command (JSON). Non-blocking: queued onto the core-loop thread.
    void dispatch(const std::string& command_json);

private:
    void handle(const nlohmann::json& cmd); // runs on the core-loop thread

    // Command handlers (Phase 1 slice).
    void cmd_start();
    void cmd_get_settings();
    void cmd_select_timeline(const nlohmann::json& cmd);
    void cmd_refresh();

    void rebuild_timelines();
    std::unique_ptr<TimelineController> make_controller(const TimelineSource& src);
    TimelineController* current() const;
    void apply_settings();

    // Event builders.
    void emit(const nlohmann::json& event);
    void emit_accounts();
    void emit_timelines();      // titles/kinds/current
    void emit_timeline(int index); // timeline_updated for one timeline
    void emit_announce(const std::string& message);
    nlohmann::json row_json(const TimelineItem& item, std::int64_t now) const;

    std::filesystem::path config_path_;
    std::filesystem::path bundled_soundpacks_;
    std::function<void(const std::string&)> emit_;

    std::unique_ptr<net::IHttpClient> http_;
    store::TimelineCache cache_;
    AccountStore accounts_;
    store::AppSettings settings_;
    std::vector<std::unique_ptr<TimelineController>> timelines_;
    int current_ = 0;

    // Threads, declared last so they are joined before the members they touch:
    // worker_ (I/O) is joined before loop_ (state) since worker tasks post to it.
    runtime::WorkerQueue loop_;   // owns engine state; the controllers' IMainExecutor
    runtime::WorkerQueue worker_; // blocking network/cache I/O
};

} // namespace fastsm
