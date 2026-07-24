#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "fastsm/runtime/main_executor.hpp"

namespace fastsm::runtime {

// A single background thread running posted tasks serially (FIFO). Used for all
// network and cache I/O so the UI thread never blocks, and (as an IMainExecutor)
// as the core's own state-owning "core loop" thread. Destruction drains the
// in-flight task and joins.
class WorkerQueue : public IMainExecutor {
public:
    WorkerQueue();
    ~WorkerQueue() override;

    WorkerQueue(const WorkerQueue&) = delete;
    WorkerQueue& operator=(const WorkerQueue&) = delete;

    void post(std::function<void()> task) override;
    void post_delayed(std::chrono::milliseconds delay, std::function<void()> task) override;

private:
    void run();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    // Tasks due at or after a wall-clock time, ordered soonest-first. run() moves
    // them into `tasks_` as they come due; a multimap allows several at one time.
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> delayed_;
    bool stop_ = false;
};

} // namespace fastsm::runtime
