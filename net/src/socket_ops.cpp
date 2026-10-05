#include "lg/net/socket_ops.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include <cerrno>
#include <system_error>

namespace lg::net::sockets {

namespace {

void set_flag(int fd, int level, int option, bool on) noexcept {
    const int value = on ? 1 : 0;
    ::setsockopt(fd, level, option, &value, static_cast<socklen_t>(sizeof value));
}

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

}  // namespace

UniqueFd create_nonblocking_socket() {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd < 0) {
        throw_errno("socket");
    }
    return UniqueFd(fd);
}

void bind_or_throw(int fd, const InetAddress& addr) {
    if (::bind(fd, addr.sockaddr_ptr(), addr.length()) < 0) {
        throw_errno(("bind " + addr.to_string()).c_str());
    }
}

void listen_or_throw(int fd, int backlog) {
    if (::listen(fd, backlog) < 0) {
        throw_errno("listen");
    }
}

int accept(int listen_fd, InetAddress* peer) noexcept {
    sockaddr_in addr{};
    socklen_t len = static_cast<socklen_t>(sizeof addr);
    const int fd = ::accept4(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd >= 0 && peer != nullptr) {
        *peer = InetAddress(addr);
    }
    return fd;
}

int connect(int fd, const InetAddress& addr) noexcept {
    return ::connect(fd, addr.sockaddr_ptr(), addr.length());
}

void set_reuse_addr(int fd, bool on) noexcept { set_flag(fd, SOL_SOCKET, SO_REUSEADDR, on); }
void set_reuse_port(int fd, bool on) noexcept { set_flag(fd, SOL_SOCKET, SO_REUSEPORT, on); }
void set_tcp_no_delay(int fd, bool on) noexcept { set_flag(fd, IPPROTO_TCP, TCP_NODELAY, on); }
void set_keep_alive(int fd, bool on) noexcept { set_flag(fd, SOL_SOCKET, SO_KEEPALIVE, on); }

int socket_error(int fd) noexcept {
    int value = 0;
    socklen_t len = static_cast<socklen_t>(sizeof value);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &value, &len) < 0) {
        return errno;
    }
    return value;
}

InetAddress local_address(int fd) noexcept {
    sockaddr_in addr{};
    socklen_t len = static_cast<socklen_t>(sizeof addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    return InetAddress(addr);
}

InetAddress peer_address(int fd) noexcept {
    sockaddr_in addr{};
    socklen_t len = static_cast<socklen_t>(sizeof addr);
    ::getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    return InetAddress(addr);
}

bool is_self_connect(int fd) noexcept {
    const sockaddr_in local = local_address(fd).raw();
    const sockaddr_in peer = peer_address(fd).raw();
    return local.sin_port == peer.sin_port && local.sin_addr.s_addr == peer.sin_addr.s_addr;
}

void shutdown_write(int fd) noexcept { ::shutdown(fd, SHUT_WR); }

std::size_t raise_fd_limit() noexcept {
    rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        return 0;
    }
    if (limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max;
        ::setrlimit(RLIMIT_NOFILE, &limit);
        ::getrlimit(RLIMIT_NOFILE, &limit);
    }
    return static_cast<std::size_t>(limit.rlim_cur);
}

}  // namespace lg::net::sockets
