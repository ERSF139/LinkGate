#include "lg/net/channel.h"

#include <sys/epoll.h>

#include "lg/net/event_loop.h"

namespace lg::net {

static_assert(Channel::kReadEvent == (EPOLLIN | EPOLLPRI));
static_assert(Channel::kWriteEvent == EPOLLOUT);

void Channel::tie(const std::shared_ptr<void>& owner) {
    tie_ = owner;
    tied_ = true;
}

void Channel::update() { loop_->update_channel(this); }

void Channel::remove() { loop_->remove_channel(this); }

void Channel::handle_event() {
    if (tied_) {
        const std::shared_ptr<void> guard = tie_.lock();
        if (guard) {
            handle_event_with_guard();
        }
    } else {
        handle_event_with_guard();
    }
}

void Channel::handle_event_with_guard() {
    if ((revents_ & EPOLLHUP) != 0 && (revents_ & EPOLLIN) == 0) {
        if (close_callback_) close_callback_();
    }
    if ((revents_ & EPOLLERR) != 0) {
        if (error_callback_) error_callback_();
    }
    if ((revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) != 0) {
        if (read_callback_) read_callback_();
    }
    if ((revents_ & EPOLLOUT) != 0) {
        if (write_callback_) write_callback_();
    }
}

}  // namespace lg::net
