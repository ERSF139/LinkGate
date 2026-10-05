#include "lg/net/connector.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <system_error>

#include "lg/net/event_loop.h"
#include "lg/net/log.h"
#include "lg/net/socket_ops.h"

namespace lg::net {

namespace {

constexpr Duration kInitialRetryDelay = std::chrono::milliseconds(500);
constexpr Duration kMaxRetryDelay = std::chrono::seconds(30);

}  // namespace

Connector::Connector(EventLoop* loop, const InetAddress& server)
    : loop_(loop), server_(server), retry_delay_(kInitialRetryDelay) {}

Connector::~Connector() = default;

void Connector::start() {
    wanted_.store(true, std::memory_order_release);
    loop_->run_in_loop([self = shared_from_this()] { self->start_in_loop(); });
}

void Connector::stop() {
    wanted_.store(false, std::memory_order_release);
    loop_->run_in_loop([self = shared_from_this()] { self->stop_in_loop(); });
}

void Connector::restart() {
    loop_->assert_in_loop_thread();
    state_ = State::Disconnected;
    retry_delay_ = kInitialRetryDelay;
    wanted_.store(true, std::memory_order_release);
    start_in_loop();
}

void Connector::start_in_loop() {
    loop_->assert_in_loop_thread();
    if (wanted_.load(std::memory_order_acquire) && state_ == State::Disconnected) {
        connect();
    }
}

void Connector::stop_in_loop() {
    loop_->assert_in_loop_thread();
    loop_->cancel(retry_timer_);
    retry_timer_ = TimerId{};
    if (state_ == State::Connecting) {
        detach_channel();
        state_ = State::Disconnected;
    }
}

void Connector::connect() {
    UniqueFd socket;
    try {
        socket = sockets::create_nonblocking_socket();
    } catch (const std::system_error& e) {
        LG_LOG(Warn) << "connect " << server_.to_string() << ": " << e.what();
        retry();
        return;
    }
    const int rc = sockets::connect(socket.get(), server_);
    const int err = rc == 0 ? 0 : errno;
    switch (err) {
        case 0:
        case EINPROGRESS:
        case EINTR:
        case EISCONN:
            connecting(std::move(socket));
            return;
        case EAGAIN:
        case EADDRINUSE:
        case EADDRNOTAVAIL:
        case ECONNREFUSED:
        case ENETUNREACH:
        case ETIMEDOUT:
            socket.reset();
            retry();
            return;
        default:
            LG_LOG(Warn) << "connect " << server_.to_string() << ": " << std::strerror(err);
            socket.reset();
            retry();
            return;
    }
}

void Connector::connecting(UniqueFd socket) {
    state_ = State::Connecting;
    socket_ = std::move(socket);
    channel_ = std::make_unique<Channel>(loop_, socket_.get());
    channel_->tie(shared_from_this());
    channel_->set_write_callback([this] { handle_write(); });
    channel_->set_error_callback([this] { handle_error(); });
    channel_->enable_writing();
}

UniqueFd Connector::detach_channel() {
    channel_->disable_all();
    channel_->remove();
    // 这里多半正处在 channel_ 自己的回调里，不能当场销毁它，推迟到本轮事件处理之后。
    std::shared_ptr<Channel> retired(std::move(channel_));
    loop_->queue_in_loop([retired]() mutable { retired.reset(); });
    return std::move(socket_);
}

void Connector::handle_write() {
    if (state_ != State::Connecting) {
        return;
    }
    UniqueFd socket = detach_channel();
    const int err = sockets::socket_error(socket.get());
    if (err != 0) {
        LG_LOG(Debug) << "connect " << server_.to_string() << ": " << std::strerror(err);
        socket.reset();
        state_ = State::Disconnected;
        retry();
        return;
    }
    if (sockets::is_self_connect(socket.get())) {
        socket.reset();
        state_ = State::Disconnected;
        retry();
        return;
    }
    state_ = State::Connected;
    if (wanted_.load(std::memory_order_acquire) && callback_) {
        callback_(std::move(socket));
    }
}

void Connector::handle_error() {
    if (state_ != State::Connecting) {
        return;
    }
    UniqueFd socket = detach_channel();
    socket.reset();
    state_ = State::Disconnected;
    retry();
}

void Connector::retry() {
    state_ = State::Disconnected;
    if (!wanted_.load(std::memory_order_acquire)) {
        return;
    }
    const std::weak_ptr<Connector> weak = shared_from_this();
    retry_timer_ = loop_->run_after(retry_delay_, [weak] {
        if (const auto self = weak.lock()) {
            self->retry_timer_ = TimerId{};
            self->start_in_loop();
        }
    });
    retry_delay_ = std::min(retry_delay_ * 2, kMaxRetryDelay);
}

}  // namespace lg::net
