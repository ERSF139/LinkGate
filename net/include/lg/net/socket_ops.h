#pragma once

#include <cstddef>

#include "lg/net/inet_address.h"
#include "lg/net/unique_fd.h"

namespace lg::net::sockets {

// 启动阶段的失败（拿不到 socket、端口被占用）抛 std::system_error；
// 运行期的单连接错误返回 -1 并保留 errno，由调用方决定怎么处理（见 decisions.md D-006）。

UniqueFd create_nonblocking_socket();
void bind_or_throw(int fd, const InetAddress& addr);
void listen_or_throw(int fd, int backlog);

int accept(int listen_fd, InetAddress* peer) noexcept;
int connect(int fd, const InetAddress& addr) noexcept;

void set_reuse_addr(int fd, bool on) noexcept;
void set_reuse_port(int fd, bool on) noexcept;
void set_tcp_no_delay(int fd, bool on) noexcept;
void set_keep_alive(int fd, bool on) noexcept;

int socket_error(int fd) noexcept;
InetAddress local_address(int fd) noexcept;
InetAddress peer_address(int fd) noexcept;
bool is_self_connect(int fd) noexcept;
void shutdown_write(int fd) noexcept;

// 把 RLIMIT_NOFILE 的软限制提到硬限制，返回提升后的软限制。
std::size_t raise_fd_limit() noexcept;

}  // namespace lg::net::sockets
