#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <optional>
#include <string>

namespace lg::net {

// 只支持 IPv4（见 decisions.md D-007）。
class InetAddress {
public:
    explicit InetAddress(std::uint16_t port = 0, bool loopback_only = false) noexcept;
    explicit InetAddress(const sockaddr_in& addr) noexcept : addr_(addr) {}

    static std::optional<InetAddress> from_ip_port(const std::string& ip, std::uint16_t port) noexcept;

    std::string ip() const;
    std::uint16_t port() const noexcept;
    std::string to_string() const;

    const sockaddr* sockaddr_ptr() const noexcept { return reinterpret_cast<const sockaddr*>(&addr_); }
    socklen_t length() const noexcept { return static_cast<socklen_t>(sizeof addr_); }
    const sockaddr_in& raw() const noexcept { return addr_; }

private:
    sockaddr_in addr_{};
};

}  // namespace lg::net
