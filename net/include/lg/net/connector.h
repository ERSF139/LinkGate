#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "lg/net/channel.h"
#include "lg/net/inet_address.h"
#include "lg/net/timer.h"
#include "lg/net/unique_fd.h"

namespace lg::net {

class EventLoop;

// 非阻塞 connect：EINPROGRESS 之后等 socket 可写，再用 SO_ERROR 判断成败；
// 失败时按指数退避重试（500 ms 起，最长 30 s）。
// 只通过 shared_ptr 持有；最后一个引用应在 loop 线程里释放（TcpClient 保证这一点）。
class Connector : public std::enable_shared_from_this<Connector> {
public:
    using NewConnectionCallback = std::function<void(UniqueFd)>;

    Connector(EventLoop* loop, const InetAddress& server);
    ~Connector();

    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;

    void set_new_connection_callback(NewConnectionCallback cb) { callback_ = std::move(cb); }

    // 任意线程。
    void start();
    void stop();
    // loop 线程：连接断开后重新开始（重置退避时间）。
    void restart();

    const InetAddress& server_address() const noexcept { return server_; }

private:
    enum class State { Disconnected, Connecting, Connected };

    void start_in_loop();
    void stop_in_loop();
    void connect();
    void connecting(UniqueFd socket);
    void handle_write();
    void handle_error();
    void retry();
    UniqueFd detach_channel();

    EventLoop* loop_;
    InetAddress server_;
    std::atomic<bool> wanted_{false};
    State state_ = State::Disconnected;
    std::unique_ptr<Channel> channel_;
    UniqueFd socket_;
    NewConnectionCallback callback_;
    Duration retry_delay_;
    TimerId retry_timer_;
};

}  // namespace lg::net
