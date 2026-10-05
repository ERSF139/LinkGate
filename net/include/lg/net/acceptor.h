#pragma once

#include <functional>

#include "lg/net/channel.h"
#include "lg/net/inet_address.h"
#include "lg/net/unique_fd.h"

namespace lg::net {

class EventLoop;

// 监听 socket。可读时循环 accept 到 EAGAIN，把新连接的 fd 交给回调。
class Acceptor {
public:
    using NewConnectionCallback = std::function<void(UniqueFd, const InetAddress&)>;

    Acceptor(EventLoop* loop, const InetAddress& listen_addr, bool reuse_port);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    void set_new_connection_callback(NewConnectionCallback cb) { callback_ = std::move(cb); }
    void listen();
    bool listening() const noexcept { return listening_; }
    InetAddress local_address() const noexcept;

private:
    void handle_read();
    void handle_fd_exhaustion();

    EventLoop* loop_;
    UniqueFd socket_;
    Channel channel_;
    NewConnectionCallback callback_;
    bool listening_ = false;
    // 预留一个 fd：进程 fd 用完时，靠它先 accept 再立刻关闭，把对端从积压队列里清掉，
    // 否则 LT 模式下监听 socket 会一直可读，事件循环空转。
    UniqueFd reserve_fd_;
};

}  // namespace lg::net
