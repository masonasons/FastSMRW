#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace fastsm::runtime {

// A single background thread running posted tasks serially (FIFO). Used for all
// network and cache I/O so the UI thread never blocks. Destruction drains the
// in-flight task and joins.
class WorkerQueue {
public:
    WorkerQueue();
    ~WorkerQueue();

    WorkerQueue(const WorkerQueue&) = delete;
    WorkerQueue& operator=(const WorkerQueue&) = delete;

    void post(std::function<void()> task);

private:
    void run();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    bool stop_ = false;
};

} // namespace fastsm::runtime
