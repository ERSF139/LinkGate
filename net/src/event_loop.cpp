#include "lg/net/event_loop.h"

#include <sys/eventfd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
#include <system_error>

#include "lg/net/channel.h"
#include "lg/net/epoll_poller.h"
#include "lg/net/log.h"
#include "lg/net/timer_queue.h"

namespace lg::net {

namespace {

thread_local EventLoop* t_loop_in_this_thread = nullptr;

constexpr int kPollTimeoutMs = 10000;
constexpr int kMaxDrainRounds = 64;

UniqueFd create_eventfd() {
    const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "eventfd");
    }
    return UniqueFd(fd);
}

}  // namespace

EventLoop::EventLoop()
    : thread_id_(std::this_thread::get_id()),
      poller_(std::make_unique<EpollPoller>(this)),
      wakeup_fd_(create_eventfd()),
      wakeup_channel_(std::make_unique<Channel>(this, wakeup_fd_.get())) {
    if (t_loop_in_this_thread != nullptr) {
        LG_LOG(Error) << "another EventLoop already exists in this thread";
        std::abort();
    }
    t_loop_in_this_thread = this;
    timer_queue_ = std::make_unique<TimerQueue>(this);
    wakeup_channel_->set_read_callback([this] { handle_wakeup(); });
    wakeup_channel_->enable_reading();
}

EventLoop::~EventLoop() {
    // 别的线程可能正处在 quit() / queue_in_loop() 里写 eventfd；它们在锁内完成唤醒，
    // 这里拿一次锁，保证它们都离开之后才开始析构。
    { std::lock_guard<std::mutex> lock(mutex_); }
    timer_queue_.reset();
    wakeup_channel_->disable_all();
    wakeup_channel_->remove();
    t_loop_in_this_thread = nullptr;
}

void EventLoop::loop() {
    assert_in_loop_thread();
    looping_.store(true, std::memory_order_release);
    while (!quit_.load(std::memory_order_acquire)) {
        active_channels_.clear();
        poller_->poll(kPollTimeoutMs, &active_channels_);
        for (Channel* channel : active_channels_) {
            channel->handle_event();
        }
        do_pending_functors();
    }
    // 退出前把排队的任务做完：连接的 connection_destroyed 往往就排在这里，
    // 不执行的话它们的 Channel 会留在 poller 里。
    for (int round = 0; round < kMaxDrainRounds; ++round) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_functors_.empty()) {
                break;
            }
        }
        do_pending_functors();
    }
    looping_.store(false, std::memory_order_release);
}

void EventLoop::quit() {
    // 必须在锁内唤醒：loop 线程一看到 quit_ 就可能退出并析构 EventLoop，
    // 锁外的 wakeup() 会写一个已经关闭的 eventfd（TSan 抓到过，见 docs/bugs.md）。
    std::lock_guard<std::mutex> lock(mutex_);
    quit_.store(true, std::memory_order_release);
    if (!is_in_loop_thread()) {
        wakeup();
    }
}

void EventLoop::run_in_loop(Functor cb) {
    if (is_in_loop_thread()) {
        cb();
    } else {
        queue_in_loop(std::move(cb));
    }
}

void EventLoop::queue_in_loop(Functor cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_functors_.push_back(std::move(cb));
    // 正在执行排队任务时新加入的任务要等下一轮，所以也要唤醒，否则会被 poll 阻塞住。
    // 唤醒放在锁内，原因同 quit()。
    if (!is_in_loop_thread() || calling_pending_functors_.load(std::memory_order_acquire)) {
        wakeup();
    }
}

void EventLoop::run_in_loop_sync(const Functor& cb) {
    if (is_in_loop_thread()) {
        cb();
        return;
    }
    std::promise<void> done;
    std::future<void> finished = done.get_future();
    queue_in_loop([&cb, &done] {
        cb();
        done.set_value();
    });
    finished.wait();
}

TimerId EventLoop::run_at(TimePoint when, Functor cb) {
    return timer_queue_->add(std::move(cb), when, Duration::zero());
}

TimerId EventLoop::run_after(Duration delay, Functor cb) {
    return run_at(Clock::now() + delay, std::move(cb));
}

TimerId EventLoop::run_every(Duration interval, Functor cb) {
    return timer_queue_->add(std::move(cb), Clock::now() + interval, interval);
}

void EventLoop::cancel(TimerId id) { timer_queue_->cancel(id); }

void EventLoop::update_channel(Channel* channel) {
    assert_in_loop_thread();
    poller_->update_channel(channel);
}

void EventLoop::remove_channel(Channel* channel) {
    assert_in_loop_thread();
    poller_->remove_channel(channel);
}

bool EventLoop::has_channel(const Channel* channel) const {
    assert_in_loop_thread();
    return poller_->has_channel(channel);
}

void EventLoop::assert_in_loop_thread() const {
    if (!is_in_loop_thread()) {
        LG_LOG(Error) << "EventLoop used from a thread that does not own it";
        std::abort();
    }
}

void EventLoop::wakeup() noexcept {
    const std::uint64_t one = 1;
    const ssize_t n = ::write(wakeup_fd_.get(), &one, sizeof one);
    if (n != static_cast<ssize_t>(sizeof one) && errno != EAGAIN) {
        LG_LOG(Error) << "eventfd write: " << std::strerror(errno);
    }
}

void EventLoop::handle_wakeup() {
    std::uint64_t value = 0;
    const ssize_t n = ::read(wakeup_fd_.get(), &value, sizeof value);
    if (n != static_cast<ssize_t>(sizeof value) && errno != EAGAIN) {
        LG_LOG(Error) << "eventfd read: " << std::strerror(errno);
    }
}

void EventLoop::do_pending_functors() {
    std::vector<Functor> functors;
    calling_pending_functors_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pending_functors_);
    }
    for (Functor& functor : functors) {
        functor();
    }
    calling_pending_functors_.store(false, std::memory_order_release);
}

}  // namespace lg::net
