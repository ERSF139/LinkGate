#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "lg/net/timer.h"
#include "lg/net/unique_fd.h"

namespace lg::net {

class Channel;
class EpollPoller;
class TimerQueue;

// one loop per thread：每个线程最多一个 EventLoop，Channel/连接的状态只在这个线程里读写。
// 跨线程的操作一律通过 run_in_loop / queue_in_loop 投递，由 eventfd 唤醒。
class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // 必须在创建它的线程里调用。quit() 之后返回，返回前把已投递的任务执行完。
    void loop();
    // 任意线程可调用。
    void quit();
    bool is_running() const noexcept { return looping_.load(std::memory_order_acquire); }

    // 任意线程可调用：在本线程就立即执行，否则排队到 loop 线程执行。
    void run_in_loop(Functor cb);
    // 任意线程可调用：总是排队，在本轮事件处理完之后执行。
    void queue_in_loop(Functor cb);
    // 任意线程可调用：在 loop 线程执行 cb，并阻塞到它执行完。loop 必须正在运行。
    void run_in_loop_sync(const Functor& cb);

    // 定时器，任意线程可调用。
    TimerId run_at(TimePoint when, Functor cb);
    TimerId run_after(Duration delay, Functor cb);
    TimerId run_every(Duration interval, Functor cb);
    void cancel(TimerId id);

    // 供 Channel 使用，只能在 loop 线程调用。
    void update_channel(Channel* channel);
    void remove_channel(Channel* channel);
    bool has_channel(const Channel* channel) const;

    bool is_in_loop_thread() const noexcept { return thread_id_ == std::this_thread::get_id(); }
    void assert_in_loop_thread() const;

    void wakeup() noexcept;

private:
    void handle_wakeup();
    void do_pending_functors();

    const std::thread::id thread_id_;
    std::atomic<bool> looping_{false};
    std::atomic<bool> quit_{false};
    std::atomic<bool> calling_pending_functors_{false};

    std::unique_ptr<EpollPoller> poller_;
    UniqueFd wakeup_fd_;
    std::unique_ptr<Channel> wakeup_channel_;
    std::unique_ptr<TimerQueue> timer_queue_;
    std::vector<Channel*> active_channels_;

    std::mutex mutex_;
    std::vector<Functor> pending_functors_;
};

}  // namespace lg::net
