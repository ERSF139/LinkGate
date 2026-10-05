#include "lg/gateway/signal_watcher.h"

#include <pthread.h>
#include <signal.h>
#include <sys/signalfd.h>

#include <cerrno>
#include <system_error>

#include "lg/net/channel.h"
#include "lg/net/event_loop.h"

namespace lg::gateway {

SignalWatcher::SignalWatcher(net::EventLoop* loop, std::initializer_list<int> signals,
                             std::function<void(int)> handler)
    : loop_(loop), handler_(std::move(handler)) {
    sigset_t mask;
    sigemptyset(&mask);
    for (const int signo : signals) {
        sigaddset(&mask, signo);
    }
    if (const int rc = ::pthread_sigmask(SIG_BLOCK, &mask, nullptr); rc != 0) {
        throw std::system_error(rc, std::generic_category(), "pthread_sigmask");
    }
    fd_.reset(::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
    if (!fd_) {
        throw std::system_error(errno, std::generic_category(), "signalfd");
    }
    channel_ = std::make_unique<net::Channel>(loop_, fd_.get());
    channel_->set_read_callback([this] { handle_read(); });
    channel_->enable_reading();
}

SignalWatcher::~SignalWatcher() {
    channel_->disable_all();
    channel_->remove();
}

void SignalWatcher::handle_read() {
    signalfd_siginfo info{};
    while (::read(fd_.get(), &info, sizeof info) == static_cast<ssize_t>(sizeof info)) {
        if (handler_) {
            handler_(static_cast<int>(info.ssi_signo));
        }
    }
}

}  // namespace lg::gateway
