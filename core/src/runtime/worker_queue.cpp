#include "fastsm/runtime/worker_queue.hpp"

#include <exception>

#include "fastsm/util/log.hpp"

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
        // A task must never take the whole app down: an unhandled exception
        // escaping here would unwind out of the thread and std::terminate the
        // process (the 0xc0000409 crash). Contain it and keep the queue alive so
        // one malformed item (e.g. a fediverse server returning a null count)
        // can't kill the client.
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                log::write(std::string("task threw: ") + e.what());
            } catch (...) {
                log::write("task threw: unknown exception");
            }
        }
    }
}

} // namespace fastsm::runtime
