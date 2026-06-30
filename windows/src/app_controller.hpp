#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fastsm/net/winhttp_client.hpp"
#include "fastsm/runtime/main_executor.hpp"
#include "fastsm/runtime/worker_queue.hpp"
#include "fastsm/store/account_store.hpp"
#include "fastsm/sound/sound_manager.hpp"
#include "fastsm/store/timeline_cache.hpp"
#include "fastsm/timeline/timeline_controller.hpp"

namespace fastsmui {

using SoundManager = fastsm::sound::SoundManager;

// Implemented by the main window; AppController calls these on the UI thread.
class AppView {
public:
    virtual ~AppView() = default;
    virtual void timelines_rebuilt() = 0;                            // account/timeline set changed
    virtual void current_timeline_changed() = 0;                     // selection moved
    virtual void timeline_updated(fastsm::TimelineController* tc) = 0; // a controller's data changed
    virtual void announce(const std::string& message) = 0;
};

// Owns the app's services and the live timeline controllers for the selected
// account, and drives account sign-in.
class AppController {
public:
    AppController(fastsm::runtime::IMainExecutor* main, SoundManager* sound);

    void set_view(AppView* view) { view_ = view; }
    void bootstrap();

    bool has_account() const;
    std::vector<fastsm::TimelineController*> timelines() const;
    fastsm::TimelineController* current() const;
    int current_index() const { return current_; }
    void select_timeline(int index);
    void next_timeline();
    void previous_timeline();

    // Sign-in (runs on the worker thread; `done(ok, error)` fires on the UI thread).
    void add_mastodon(const std::string& instance, std::function<void(bool, std::string)> done);
    void add_bluesky(const std::string& service, const std::string& handle,
                     const std::string& app_password,
                     std::function<void(bool, std::string)> done);

    SoundManager* sound() const { return sound_; }

private:
    void rebuild_timelines();
    void save_config();

    fastsm::net::WinHttpClient http_;
    fastsm::store::TimelineCache cache_;
    fastsm::AccountStore accounts_;
    std::vector<std::unique_ptr<fastsm::TimelineController>> timelines_;
    // Declared last so it is destroyed (joined) first — its tasks reference the
    // members above.
    fastsm::runtime::WorkerQueue worker_;

    fastsm::runtime::IMainExecutor* main_;
    SoundManager* sound_;
    AppView* view_ = nullptr;
    int current_ = 0;
};

} // namespace fastsmui
