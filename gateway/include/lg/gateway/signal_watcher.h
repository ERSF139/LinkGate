#pragma once

#include <functional>
#include <initializer_list>
#include <memory>

#include "lg/net/unique_fd.h"

namespace lg::net {
class Channel;
class EventLoop;
}  // namespace lg::net

namespace lg::gateway {

// 用 signalfd 把信号变成事件循环里的一个可读事件，回调在 loop 线程里执行，
// 不受异步信号安全的限制（见 decisions.md D-008）。
// 必须在启动任何其他线程之前构造：它屏蔽当前线程的这些信号，之后创建的线程继承这个屏蔽字，
// 否则信号可能被投递给某个 IO 线程，绕过 signalfd。
class SignalWatcher {
public:
    SignalWatcher(net::EventLoop* loop, std::initializer_list<int> signals, std::function<void(int)> handler);
    ~SignalWatcher();

    SignalWatcher(const SignalWatcher&) = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;

private:
    void handle_read();

    net::EventLoop* loop_;
    net::UniqueFd fd_;
    std::unique_ptr<net::Channel> channel_;
    std::function<void(int)> handler_;
};

}  // namespace lg::gateway
