#pragma once

#include <gdk/gdk.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fastsmgtk {

// Global shortcuts via evdev, with X11 grabs when evdev is unavailable.
class InvisibleHotkeys {
public:
    using ActionFn = std::function<void(const std::string& action, guint32 timestamp)>; // main thread

    ~InvisibleHotkeys() { stop(); }

    // Starts listening with the given canonical-keystring -> action bindings.
    // Returns false when no keyboard device could be opened (not installed /
    // no permission); `error` then says why, for an announcement.
    bool start(std::unordered_map<std::string, std::string> bindings, ActionFn on_action,
               std::string& error);
    void stop();
    bool running() const { return running_.load(); }

private:
    void run();
    bool start_x11(const std::unordered_map<std::string, std::string>& bindings);
    void stop_x11();
    static GdkFilterReturn filter_x11(GdkXEvent* event, GdkEvent*, gpointer data);
    GdkWindow* x11_root_ = nullptr;
    unsigned x11_locks_ = 0;
    std::unordered_map<unsigned, std::string> x11_actions_;

    std::unordered_map<std::string, std::string> bindings_;
    ActionFn on_action_;
    std::vector<int> fds_;
    int wake_pipe_[2] = {-1, -1};
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace fastsmgtk
