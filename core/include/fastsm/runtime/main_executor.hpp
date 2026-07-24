#pragma once

#include <chrono>
#include <functional>

// Bridge for marshalling work back onto the UI/main thread. The core runs
// network and cache I/O on a background worker; when results are ready it calls
// IMainExecutor::post() so the front end can apply them on the thread that owns
// the UI. The Win32 front end implements this by queueing the closure and
// PostMessage-ing a custom message to drain it on the message-pump thread.

namespace fastsm::runtime {

class IMainExecutor {
public:
    virtual ~IMainExecutor() = default;

    // Schedules fn to run on the main thread. Safe to call from any thread.
    virtual void post(std::function<void()> fn) = 0;

    // Schedules fn to run on the main thread after at least `delay`. Used for
    // debouncing (e.g. coalescing home-position marker saves). The default runs
    // fn immediately; WorkerQueue overrides it with a real timer.
    virtual void post_delayed(std::chrono::milliseconds /*delay*/, std::function<void()> fn) {
        post(std::move(fn));
    }
};

} // namespace fastsm::runtime
