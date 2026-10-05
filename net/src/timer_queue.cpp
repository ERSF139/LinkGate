#include "lg/net/timer_queue.h"

#include <sys/timerfd.h>

#include <cerrno>
#include <cstring>
#include <system_error>
#include <vector>

#include "lg/net/event_loop.h"
#include "lg/net/log.h"

namespace lg::net {

namespace {

constexpr auto kMinimumDelay = std::chrono::microseconds(100);

int create_timerfd() {
    const int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "timerfd_create");
    }
    return fd;
}

}  // namespace

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop), timer_fd_(create_timerfd()), channel_(loop, timer_fd_.get()) {
    channel_.set_read_callback([this] { handle_read(); });
    channel_.enable_reading();
}

TimerQueue::~TimerQueue() {
    channel_.disable_all();
    channel_.remove();
}

TimerId TimerQueue::add(Callback cb, TimePoint when, Duration interval) {
    const std::uint64_t sequence = next_sequence_.fetch_add(1, std::memory_order_relaxed);
    Timer timer{std::move(cb), when, interval};
    loop_->run_in_loop([this, sequence, timer = std::move(timer)]() mutable {
        add_in_loop(sequence, std::move(timer));
    });
    return TimerId{sequence};
}

void TimerQueue::cancel(TimerId id) {
    if (!id.valid()) {
        return;
    }
    loop_->run_in_loop([this, sequence = id.sequence] { cancel_in_loop(sequence); });
}

void TimerQueue::add_in_loop(std::uint64_t sequence, Timer timer) {
    const bool earliest = queue_.empty() || timer.when < queue_.begin()->first;
    queue_.emplace(timer.when, sequence);
    timers_.emplace(sequence, std::move(timer));
    if (earliest) {
        rearm();
    }
}

void TimerQueue::cancel_in_loop(std::uint64_t sequence) {
    const auto it = timers_.find(sequence);
    if (it != timers_.end()) {
        queue_.erase({it->second.when, sequence});
        timers_.erase(it);
        rearm();
    } else if (running_callbacks_) {
        // 正在执行的定时器已经从 timers_ 里取出，记下来，执行完后不再重新加入。
        cancelled_while_running_.insert(sequence);
    }
}

void TimerQueue::handle_read() {
    std::uint64_t expirations = 0;
    if (::read(timer_fd_.get(), &expirations, sizeof expirations) < 0 && errno != EAGAIN) {
        LG_LOG(Error) << "timerfd read: " << std::strerror(errno);
    }

    const TimePoint now = Clock::now();
    std::vector<std::uint64_t> expired;
    while (!queue_.empty() && queue_.begin()->first <= now) {
        expired.push_back(queue_.begin()->second);
        queue_.erase(queue_.begin());
    }

    running_callbacks_ = true;
    cancelled_while_running_.clear();
    for (const std::uint64_t sequence : expired) {
        const auto it = timers_.find(sequence);
        if (it == timers_.end()) {
            continue;
        }
        // 先把定时器从表里移出来再执行：回调里可能取消它自己，或者添加新的定时器。
        Timer timer = std::move(it->second);
        timers_.erase(it);
        timer.callback();
        if (timer.interval > Duration::zero() && cancelled_while_running_.count(sequence) == 0) {
            timer.when = now + timer.interval;
            queue_.emplace(timer.when, sequence);
            timers_.emplace(sequence, std::move(timer));
        }
    }
    running_callbacks_ = false;
    cancelled_while_running_.clear();
    rearm();
}

void TimerQueue::rearm() {
    itimerspec spec{};
    if (!queue_.empty()) {
        auto delay = queue_.begin()->first - Clock::now();
        if (delay < kMinimumDelay) {
            delay = kMinimumDelay;
        }
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count();
        spec.it_value.tv_sec = static_cast<time_t>(ns / 1000000000);
        spec.it_value.tv_nsec = static_cast<long>(ns % 1000000000);
    }
    if (::timerfd_settime(timer_fd_.get(), 0, &spec, nullptr) < 0) {
        LG_LOG(Error) << "timerfd_settime: " << std::strerror(errno);
    }
}

}  // namespace lg::net
