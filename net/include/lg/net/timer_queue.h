#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "lg/net/channel.h"
#include "lg/net/timer.h"
#include "lg/net/unique_fd.h"

namespace lg::net {

class EventLoop;

// 基于 timerfd 的定时器队列：所有定时器按到期时间排在一个有序集合里，
// timerfd 只设置成最早的那个到期时间，到期后在 loop 线程里执行回调。
class TimerQueue {
public:
    using Callback = std::function<void()>;

    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;

    // 任意线程可调用。interval 为零表示只触发一次。
    TimerId add(Callback cb, TimePoint when, Duration interval);
    void cancel(TimerId id);

private:
    struct Timer {
        Callback callback;
        TimePoint when;
        Duration interval;
    };

    void add_in_loop(std::uint64_t sequence, Timer timer);
    void cancel_in_loop(std::uint64_t sequence);
    void handle_read();
    void rearm();

    EventLoop* loop_;
    UniqueFd timer_fd_;
    Channel channel_;

    std::set<std::pair<TimePoint, std::uint64_t>> queue_;
    std::unordered_map<std::uint64_t, Timer> timers_;
    std::atomic<std::uint64_t> next_sequence_{1};

    bool running_callbacks_ = false;
    std::unordered_set<std::uint64_t> cancelled_while_running_;
};

}  // namespace lg::net
