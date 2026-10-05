#include <signal.h>

#include <cstdio>
#include <exception>
#include <iostream>

#include "lg/gateway/config.h"
#include "lg/gateway/gateway.h"
#include "lg/gateway/signal_watcher.h"
#include "lg/net/event_loop.h"
#include "lg/net/log.h"
#include "lg/net/socket_ops.h"

int main(int argc, char** argv) {
    const lg::gateway::ParseOutcome outcome = lg::gateway::parse_args(argc, argv);
    if (outcome.help) {
        std::cout << lg::gateway::usage(argv[0]);
        return 0;
    }
    if (!outcome.config) {
        std::cerr << "linkgate: " << outcome.error << "\n\n" << lg::gateway::usage(argv[0]);
        return 2;
    }
    const lg::gateway::Config& config = *outcome.config;
    lg::net::set_log_level(config.log_level);

    ::signal(SIGPIPE, SIG_IGN);
    const std::size_t fd_limit = lg::net::sockets::raise_fd_limit();
    LG_LOG(Info) << "file descriptor limit: " << fd_limit;

    try {
        lg::net::EventLoop loop;
        lg::gateway::SignalWatcher signals(&loop, {SIGINT, SIGTERM}, [&loop](int signo) {
            LG_LOG(Info) << "received signal " << signo << ", shutting down";
            loop.quit();
        });
        lg::gateway::Gateway gateway(&loop, config);
        gateway.start();
        loop.loop();
        LG_LOG(Info) << "final " << gateway.stats().to_string();
    } catch (const std::exception& e) {
        LG_LOG(Error) << "linkgate: " << e.what();
        return 1;
    }
    return 0;
}
