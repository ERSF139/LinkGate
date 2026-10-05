#include "lg/net/acceptor.h"

#include <fcntl.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>

#include "lg/net/event_loop.h"
#include "lg/net/log.h"
#include "lg/net/socket_ops.h"

namespace lg::net {

namespace {

constexpr int kListenBacklog = 4096;
constexpr int kMaxAcceptPerEvent = 128;

UniqueFd open_reserve_fd() { return UniqueFd(::open("/dev/null", O_RDONLY | O_CLOEXEC)); }

}  // namespace

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listen_addr, bool reuse_port)
    : loop_(loop),
      socket_(sockets::create_nonblocking_socket()),
      channel_(loop, socket_.get()),
      reserve_fd_(open_reserve_fd()) {
    sockets::set_reuse_addr(socket_.get(), true);
    sockets::set_reuse_port(socket_.get(), reuse_port);
    sockets::bind_or_throw(socket_.get(), listen_addr);
    channel_.set_read_callback([this] { handle_read(); });
}

Acceptor::~Acceptor() {
    channel_.disable_all();
    channel_.remove();
}

void Acceptor::listen() {
    loop_->assert_in_loop_thread();
    sockets::listen_or_throw(socket_.get(), kListenBacklog);
    listening_ = true;
    channel_.enable_reading();
}

InetAddress Acceptor::local_address() const noexcept { return sockets::local_address(socket_.get()); }

void Acceptor::handle_read() {
    loop_->assert_in_loop_thread();
    for (int i = 0; i < kMaxAcceptPerEvent; ++i) {
        InetAddress peer;
        const int fd = sockets::accept(socket_.get(), &peer);
        if (fd >= 0) {
            UniqueFd connection(fd);
            if (callback_) {
                callback_(std::move(connection), peer);
            }
            continue;
        }
        const int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return;
        }
        if (err == EINTR || err == ECONNABORTED || err == EPROTO) {
            continue;
        }
        if (err == EMFILE || err == ENFILE) {
            handle_fd_exhaustion();
            return;
        }
        LG_LOG(Error) << "accept: " << std::strerror(err);
        return;
    }
}

void Acceptor::handle_fd_exhaustion() {
    LG_LOG(Warn) << "accept: file descriptors exhausted, shedding one pending connection";
    reserve_fd_.reset();
    UniqueFd shed(::accept(socket_.get(), nullptr, nullptr));
    shed.reset();
    reserve_fd_ = open_reserve_fd();
}

}  // namespace lg::net
