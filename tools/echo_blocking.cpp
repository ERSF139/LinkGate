// 第 2 章的对照实验：最朴素的阻塞式 echo 服务器，一次只能服务一个客户端。
// 用两个终端同时 `nc 127.0.0.1 9200`，第二个客户端要等第一个断开才会得到回应。
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::atoi(argv[1]) : 9200;

    const int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        std::cerr << "socket: " << std::strerror(errno) << '\n';
        return 1;
    }
    const int on = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(listen_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0) {
        std::cerr << "bind: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (::listen(listen_fd, 16) < 0) {
        std::cerr << "listen: " << std::strerror(errno) << '\n';
        return 1;
    }
    std::cout << "blocking echo listening on port " << port << '\n';

    while (true) {
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        const int fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &len);
        if (fd < 0) {
            if (errno == EINTR) continue;
            std::cerr << "accept: " << std::strerror(errno) << '\n';
            break;
        }
        char ip[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        std::cout << "client " << ip << ':' << ntohs(peer.sin_port) << " connected\n";

        char buf[4096];
        while (true) {
            const ssize_t n = ::read(fd, buf, sizeof buf);
            if (n > 0) {
                ssize_t sent = 0;
                while (sent < n) {
                    const ssize_t w = ::write(fd, buf + sent, static_cast<std::size_t>(n - sent));
                    if (w <= 0) break;
                    sent += w;
                }
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        std::cout << "client " << ip << ':' << ntohs(peer.sin_port) << " disconnected\n";
        ::close(fd);
    }
    ::close(listen_fd);
    return 0;
}
