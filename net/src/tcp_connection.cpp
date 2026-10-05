#include "lg/net/tcp_connection.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "lg/net/event_loop.h"
#include "lg/net/log.h"
#include "lg/net/socket_ops.h"

namespace lg::net {

TcpConnection::TcpConnection(EventLoop* loop, std::string name, std::uint64_t id, UniqueFd socket,
                             const InetAddress& local, const InetAddress& peer)
    : loop_(loop),
      name_(std::move(name)),
      id_(id),
      socket_(std::move(socket)),
      channel_(loop, socket_.get()),
      local_(local),
      peer_(peer) {
    channel_.set_read_callback([this] { handle_read(); });
    channel_.set_write_callback([this] { handle_write(); });
    channel_.set_close_callback([this] { handle_close(); });
    channel_.set_error_callback([this] { handle_error(); });
    sockets::set_keep_alive(socket_.get(), true);
}

TcpConnection::~TcpConnection() = default;

void TcpConnection::send(const void* data, std::size_t size) {
    if (!connected()) {
        return;
    }
    if (loop_->is_in_loop_thread()) {
        send_in_loop(data, size);
        return;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::vector<std::uint8_t> copy(bytes, bytes + size);
    loop_->run_in_loop([self = shared_from_this(), copy = std::move(copy)] {
        self->send_in_loop(copy.data(), copy.size());
    });
}

void TcpConnection::send_in_loop(const void* data, std::size_t size) {
    loop_->assert_in_loop_thread();
    if (state_.load(std::memory_order_acquire) == State::Disconnected) {
        return;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t written = 0;
    bool fault = false;

    if (!channel_.is_writing() && output_.readable_bytes() == 0) {
        const ssize_t n = ::send(socket_.get(), bytes, size, MSG_NOSIGNAL);
        if (n >= 0) {
            written = static_cast<std::size_t>(n);
            if (written == size && write_complete_callback_) {
                loop_->queue_in_loop([self = shared_from_this()] {
                    self->write_complete_callback_(self);
                });
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            if (errno == EPIPE || errno == ECONNRESET) {
                fault = true;
            } else {
                LG_LOG(Warn) << name_ << " send: " << std::strerror(errno);
            }
        }
    }

    const std::size_t remaining = size - written;
    if (fault || remaining == 0) {
        return;
    }
    const std::size_t before = output_.readable_bytes();
    if (before + remaining >= high_water_mark_ && before < high_water_mark_ && high_water_mark_callback_) {
        loop_->queue_in_loop([self = shared_from_this(), total = before + remaining] {
            self->high_water_mark_callback_(self, total);
        });
    }
    output_.append(bytes + written, remaining);
    if (!channel_.is_writing()) {
        channel_.enable_writing();
    }
}

void TcpConnection::shutdown() {
    State expected = State::Connected;
    if (state_.compare_exchange_strong(expected, State::Disconnecting, std::memory_order_acq_rel)) {
        loop_->run_in_loop([self = shared_from_this()] { self->shutdown_in_loop(); });
    }
}

void TcpConnection::shutdown_in_loop() {
    loop_->assert_in_loop_thread();
    if (!channel_.is_writing()) {
        sockets::shutdown_write(socket_.get());
    }
}

void TcpConnection::force_close() {
    State state = state_.load(std::memory_order_acquire);
    // 只允许 Connected -> Disconnecting；用 CAS，免得和 loop 线程里的 handle_close 竞争，
    // 把已经是 Disconnected 的状态又改回 Disconnecting。
    while (state == State::Connected &&
           !state_.compare_exchange_weak(state, State::Disconnecting, std::memory_order_acq_rel)) {
    }
    if (state == State::Connected || state == State::Disconnecting) {
        loop_->queue_in_loop([self = shared_from_this()] {
            if (self->state_.load(std::memory_order_acquire) != State::Disconnected) {
                self->handle_close();
            }
        });
    }
}

void TcpConnection::start_read() {
    loop_->run_in_loop([self = shared_from_this()] {
        if (!self->reading_ && !self->disconnected()) {
            self->channel_.enable_reading();
            self->reading_ = true;
        }
    });
}

void TcpConnection::stop_read() {
    loop_->run_in_loop([self = shared_from_this()] {
        if (self->reading_) {
            self->channel_.disable_reading();
            self->reading_ = false;
        }
    });
}

void TcpConnection::set_tcp_no_delay(bool on) noexcept { sockets::set_tcp_no_delay(socket_.get(), on); }

void TcpConnection::connection_established() {
    loop_->assert_in_loop_thread();
    state_.store(State::Connected, std::memory_order_release);
    channel_.tie(shared_from_this());
    channel_.enable_reading();
    reading_ = true;
    if (connection_callback_) {
        connection_callback_(shared_from_this());
    }
}

void TcpConnection::connection_destroyed() {
    loop_->assert_in_loop_thread();
    if (state_.load(std::memory_order_acquire) != State::Disconnected) {
        state_.store(State::Disconnected, std::memory_order_release);
        channel_.disable_all();
        if (connection_callback_) {
            connection_callback_(shared_from_this());
        }
    }
    channel_.remove();
}

void TcpConnection::handle_read() {
    loop_->assert_in_loop_thread();
    int saved_errno = 0;
    const ssize_t n = input_.read_fd(socket_.get(), &saved_errno);
    if (n > 0) {
        if (message_callback_) {
            message_callback_(shared_from_this(), &input_);
        } else {
            input_.retrieve_all();
        }
    } else if (n == 0) {
        handle_close();
    } else if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK && saved_errno != EINTR) {
        errno = saved_errno;
        handle_error();
        handle_close();
    }
}

void TcpConnection::handle_write() {
    loop_->assert_in_loop_thread();
    if (!channel_.is_writing()) {
        return;
    }
    const ssize_t n = ::send(socket_.get(), output_.peek(), output_.readable_bytes(), MSG_NOSIGNAL);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            LG_LOG(Warn) << name_ << " write: " << std::strerror(errno);
        }
        return;
    }
    output_.retrieve(static_cast<std::size_t>(n));
    if (output_.readable_bytes() != 0) {
        return;
    }
    channel_.disable_writing();
    if (output_.capacity() > 4 * Buffer::kInitialSize * 1024) {
        output_.shrink(Buffer::kInitialSize);
    }
    if (write_complete_callback_) {
        loop_->queue_in_loop([self = shared_from_this()] { self->write_complete_callback_(self); });
    }
    if (state_.load(std::memory_order_acquire) == State::Disconnecting) {
        shutdown_in_loop();
    }
}

void TcpConnection::handle_close() {
    loop_->assert_in_loop_thread();
    if (state_.load(std::memory_order_acquire) == State::Disconnected) {
        return;
    }
    state_.store(State::Disconnected, std::memory_order_release);
    channel_.disable_all();
    reading_ = false;
    const TcpConnectionPtr guard = shared_from_this();
    if (connection_callback_) {
        connection_callback_(guard);
    }
    if (close_callback_) {
        close_callback_(guard);
    }
}

void TcpConnection::handle_error() {
    const int err = sockets::socket_error(socket_.get());
    if (err != 0 && err != ECONNRESET && err != EPIPE) {
        LG_LOG(Debug) << name_ << " socket error: " << std::strerror(err);
    }
}

}  // namespace lg::net
