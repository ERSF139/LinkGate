#include "net_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <thread>

namespace lg::test {

namespace {

int to_ms(std::chrono::milliseconds timeout) { return static_cast<int>(timeout.count()); }

bool wait_readable(int fd, std::chrono::milliseconds timeout) {
    pollfd p{};
    p.fd = fd;
    p.events = POLLIN;
    return ::poll(&p, 1, to_ms(timeout)) > 0;
}

}  // namespace

BlockingClient::~BlockingClient() { close(); }

BlockingClient::BlockingClient(BlockingClient&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

BlockingClient& BlockingClient::operator=(BlockingClient&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool BlockingClient::connect(std::uint16_t port, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        close();
        fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    close();
    return false;
}

bool BlockingClient::send_all(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd_, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool BlockingClient::read_exact(std::size_t size, std::vector<std::uint8_t>* out,
                                std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    out->clear();
    std::uint8_t buf[4096];
    while (out->size() < size) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0 || !wait_readable(fd_, left)) {
            return false;
        }
        const std::size_t want = std::min(sizeof buf, size - out->size());
        const ssize_t n = ::recv(fd_, buf, want, 0);
        if (n <= 0) {
            return false;
        }
        out->insert(out->end(), buf, buf + n);
    }
    return true;
}

std::vector<std::uint8_t> BlockingClient::read_some(std::chrono::milliseconds timeout) {
    std::vector<std::uint8_t> out;
    if (!wait_readable(fd_, timeout)) {
        return out;
    }
    std::uint8_t buf[65536];
    const ssize_t n = ::recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
    if (n > 0) {
        out.assign(buf, buf + n);
    }
    return out;
}

bool BlockingClient::wait_closed(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::uint8_t buf[4096];
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0 || !wait_readable(fd_, left)) {
            return false;
        }
        const ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
        if (n == 0 || (n < 0 && errno == ECONNRESET)) {
            return true;
        }
        if (n < 0 && errno != EINTR && errno != EAGAIN) {
            return true;
        }
    }
}

void BlockingClient::set_receive_buffer(int bytes) {
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof bytes);
}

void BlockingClient::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool wait_until(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

}  // namespace lg::test
