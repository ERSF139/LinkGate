#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace lg::net {

class EventLoop;

// 一个 fd 在事件循环里的登记项：关注哪些事件、事件到来时调用哪些回调。
// 不拥有 fd。只能在所属 EventLoop 的线程里使用。
class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd) noexcept : loop_(loop), fd_(fd) {}
    ~Channel() = default;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    void handle_event();

    void set_read_callback(EventCallback cb) { read_callback_ = std::move(cb); }
    void set_write_callback(EventCallback cb) { write_callback_ = std::move(cb); }
    void set_close_callback(EventCallback cb) { close_callback_ = std::move(cb); }
    void set_error_callback(EventCallback cb) { error_callback_ = std::move(cb); }

    // 回调执行期间持有 owner 的强引用，防止回调里把 owner 析构掉（例如连接在读回调里关闭）。
    void tie(const std::shared_ptr<void>& owner);

    int fd() const noexcept { return fd_; }
    std::uint32_t events() const noexcept { return events_; }
    void set_revents(std::uint32_t revents) noexcept { revents_ = revents; }
    bool is_none_event() const noexcept { return events_ == 0; }
    bool is_reading() const noexcept { return (events_ & kReadEvent) != 0; }
    bool is_writing() const noexcept { return (events_ & kWriteEvent) != 0; }

    void enable_reading() { events_ |= kReadEvent; update(); }
    void disable_reading() { events_ &= ~kReadEvent; update(); }
    void enable_writing() { events_ |= kWriteEvent; update(); }
    void disable_writing() { events_ &= ~kWriteEvent; update(); }
    void disable_all() { events_ = 0; update(); }

    // 从 poller 中注销。之后才可以安全析构。
    void remove();

    EventLoop* owner_loop() const noexcept { return loop_; }

    // 供 EpollPoller 记录登记状态。
    enum class PollState { New, Added, Detached };
    PollState poll_state() const noexcept { return poll_state_; }
    void set_poll_state(PollState state) noexcept { poll_state_ = state; }

    static constexpr std::uint32_t kReadEvent = 0x001 | 0x002;  // EPOLLIN | EPOLLPRI
    static constexpr std::uint32_t kWriteEvent = 0x004;         // EPOLLOUT

private:
    void update();
    void handle_event_with_guard();

    EventLoop* loop_;
    const int fd_;
    std::uint32_t events_ = 0;
    std::uint32_t revents_ = 0;
    PollState poll_state_ = PollState::New;

    std::weak_ptr<void> tie_;
    bool tied_ = false;

    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
    EventCallback error_callback_;
};

}  // namespace lg::net
