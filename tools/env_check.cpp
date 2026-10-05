#include <sys/epoll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

int main() {
    std::cout << "compiler: " << __VERSION__ << ", __cplusplus=" << __cplusplus << '\n';

    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        std::cout << "epoll_create1 failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    std::cout << "epoll fd = " << epfd << '\n';
    ::close(epfd);
    return 0;
}
