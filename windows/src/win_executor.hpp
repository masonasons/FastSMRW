#pragma once

#include <deque>
#include <functional>
#include <mutex>

#include <windows.h>

#include "fastsm/runtime/main_executor.hpp"

namespace fastsmui {

// Marshals closures from worker threads onto the UI thread: queue + PostMessage,
// drained when the window receives WM_APP_DISPATCH.
class WinExecutor : public fastsm::runtime::IMainExecutor {
public:
    void bind(HWND hwnd, UINT msg) {
        hwnd_ = hwnd;
        msg_ = msg;
    }

    void post(std::function<void()> fn) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(fn));
        }
        if (hwnd_)
            PostMessageW(hwnd_, msg_, 0, 0);
    }

    // Called on the UI thread when WM_APP_DISPATCH arrives.
    void drain() {
        std::deque<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            local.swap(queue_);
        }
        for (auto& fn : local)
            if (fn)
                fn();
    }

private:
    HWND hwnd_ = nullptr;
    UINT msg_ = 0;
    std::mutex mutex_;
    std::deque<std::function<void()>> queue_;
};

} // namespace fastsmui
