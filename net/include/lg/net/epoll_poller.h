#pragma once

#include <sys/epoll.h>

#include <unordered_map>
#include <vector>

#include "lg/net/unique_fd.h"

namespace lg::net {

class Channel;
class EventLoop;

// 对 epoll 的薄封装，水平触发（LT），见 decisions.md D-004。只在所属 EventLoop 的线程里使用。
class EpollPoller {
public:
    explicit EpollPoller(EventLoop* loop);

    void poll(int timeout_ms, std::vector<Channel*>* active_channels);
    void update_channel(Channel* channel);
    void remove_channel(Channel* channel);
    bool has_channel(const Channel* channel) const;

private:
    void control(int operation, Channel* channel);

    EventLoop* owner_;
    UniqueFd epoll_fd_;
    std::vector<epoll_event> events_;
    std::unordered_map<int, Channel*> channels_;
};

}  // namespace lg::net
