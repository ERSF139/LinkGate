#include "lg/net/inet_address.h"

#include <arpa/inet.h>

namespace lg::net {

InetAddress::InetAddress(std::uint16_t port, bool loopback_only) noexcept {
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    addr_.sin_port = htons(port);
}

std::optional<InetAddress> InetAddress::from_ip_port(const std::string& ip, std::uint16_t port) noexcept {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        return std::nullopt;
    }
    return InetAddress(addr);
}

std::string InetAddress::ip() const {
    char text[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &addr_.sin_addr, text, sizeof text);
    return text;
}

std::uint16_t InetAddress::port() const noexcept { return ntohs(addr_.sin_port); }

std::string InetAddress::to_string() const { return ip() + ":" + std::to_string(port()); }

}  // namespace lg::net
