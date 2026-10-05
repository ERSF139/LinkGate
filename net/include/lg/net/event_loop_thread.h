#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lg::net {

class EventLoop;

// 一个线程 + 它栈上的 EventLoop。start() 阻塞到 loop 创建完成；析构时 quit 并 join。
class EventLoopThread {
public:
    explicit EventLoopThread(std::string name = {});
    ~EventLoopThread();

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop* start();

private:
    void thread_main();

    std::string name_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable ready_;
    EventLoop* loop_ = nullptr;
};

// IO 线程池。threads 为 0 时所有连接都留在 base loop。
class EventLoopThreadPool {
public:
    EventLoopThreadPool(EventLoop* base_loop, std::size_t threads, std::string name);
    ~EventLoopThreadPool();

    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void start();
    // 轮询分配，任意线程可调用。
    EventLoop* next_loop();
    // start() 之后不再变化，可以在任意线程读取。
    const std::vector<EventLoop*>& loops() const noexcept { return loops_; }
    EventLoop* base_loop() const noexcept { return base_loop_; }

private:
    EventLoop* base_loop_;
    std::size_t thread_count_;
    std::string name_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
    std::atomic<std::size_t> next_{0};
};

}  // namespace lg::net
