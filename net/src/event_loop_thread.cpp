#include "lg/net/event_loop_thread.h"

#include <pthread.h>

#include "lg/net/event_loop.h"

namespace lg::net {

EventLoopThread::EventLoopThread(std::string name) : name_(std::move(name)) {}

EventLoopThread::~EventLoopThread() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_ != nullptr) {
            loop_->quit();
        }
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

EventLoop* EventLoopThread::start() {
    thread_ = std::thread([this] { thread_main(); });
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return loop_ != nullptr; });
    return loop_;
}

void EventLoopThread::thread_main() {
    if (!name_.empty()) {
        ::pthread_setname_np(::pthread_self(), name_.substr(0, 15).c_str());
    }
    EventLoop loop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
    }
    ready_.notify_one();
    loop.loop();
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}

EventLoopThreadPool::EventLoopThreadPool(EventLoop* base_loop, std::size_t threads, std::string name)
    : base_loop_(base_loop), thread_count_(threads), name_(std::move(name)) {}

EventLoopThreadPool::~EventLoopThreadPool() = default;

void EventLoopThreadPool::start() {
    for (std::size_t i = 0; i < thread_count_; ++i) {
        auto thread = std::make_unique<EventLoopThread>(name_ + std::to_string(i));
        loops_.push_back(thread->start());
        threads_.push_back(std::move(thread));
    }
    if (loops_.empty()) {
        loops_.push_back(base_loop_);
    }
}

EventLoop* EventLoopThreadPool::next_loop() {
    const std::size_t index = next_.fetch_add(1, std::memory_order_relaxed);
    return loops_[index % loops_.size()];
}

}  // namespace lg::net
