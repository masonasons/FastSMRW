#include "fastsm/runtime/worker_queue.hpp"

namespace fastsm::runtime {

WorkerQueue::WorkerQueue() : thread_([this] { run(); }) {}

WorkerQueue::~WorkerQueue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void WorkerQueue::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void WorkerQueue::run() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty())
                return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        if (task)
            task();
    }
}

} // namespace fastsm::runtime
