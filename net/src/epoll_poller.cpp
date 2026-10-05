#include "lg/net/epoll_poller.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <system_error>

#include "lg/net/channel.h"
#include "lg/net/event_loop.h"
#include "lg/net/log.h"

namespace lg::net {

namespace {

constexpr std::size_t kInitialEventListSize = 16;

}  // namespace

EpollPoller::EpollPoller(EventLoop* loop)
    : owner_(loop), epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kInitialEventListSize) {
    if (!epoll_fd_) {
        throw std::system_error(errno, std::generic_category(), "epoll_create1");
    }
}

void EpollPoller::poll(int timeout_ms, std::vector<Channel*>* active_channels) {
    const int n = ::epoll_wait(epoll_fd_.get(), events_.data(), static_cast<int>(events_.size()),
                               timeout_ms);
    if (n < 0) {
        if (errno != EINTR) {
            LG_LOG(Error) << "epoll_wait: " << std::strerror(errno);
        }
        return;
    }
    const auto ready = static_cast<std::size_t>(n);
    for (std::size_t i = 0; i < ready; ++i) {
        auto* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        active_channels->push_back(channel);
    }
    if (ready == events_.size()) {
        events_.resize(events_.size() * 2);
    }
}

void EpollPoller::update_channel(Channel* channel) {
    owner_->assert_in_loop_thread();
    const Channel::PollState state = channel->poll_state();
    if (state == Channel::PollState::New || state == Channel::PollState::Detached) {
        if (state == Channel::PollState::New) {
            assert(channels_.count(channel->fd()) == 0);
            channels_[channel->fd()] = channel;
        }
        if (channel->is_none_event()) {
            channel->set_poll_state(Channel::PollState::Detached);
            return;
        }
        channel->set_poll_state(Channel::PollState::Added);
        control(EPOLL_CTL_ADD, channel);
    } else if (channel->is_none_event()) {
        control(EPOLL_CTL_DEL, channel);
        channel->set_poll_state(Channel::PollState::Detached);
    } else {
        control(EPOLL_CTL_MOD, channel);
    }
}

void EpollPoller::remove_channel(Channel* channel) {
    owner_->assert_in_loop_thread();
    channels_.erase(channel->fd());
    if (channel->poll_state() == Channel::PollState::Added) {
        control(EPOLL_CTL_DEL, channel);
    }
    channel->set_poll_state(Channel::PollState::New);
}

bool EpollPoller::has_channel(const Channel* channel) const {
    const auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

void EpollPoller::control(int operation, Channel* channel) {
    epoll_event event{};
    event.events = channel->events();
    event.data.ptr = channel;
    if (::epoll_ctl(epoll_fd_.get(), operation, channel->fd(), &event) < 0) {
        LG_LOG(Error) << "epoll_ctl op=" << operation << " fd=" << channel->fd() << ": "
                      << std::strerror(errno);
    }
}

}  // namespace lg::net
