#include "lg/gateway/device_registry.h"

#include "lg/net/tcp_connection.h"

namespace lg::gateway {

net::TcpConnectionPtr DeviceRegistry::bind(std::int32_t device_id, const net::TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::weak_ptr<net::TcpConnection>& owner = owners_[device_id];
    net::TcpConnectionPtr previous = owner.lock();
    owner = conn;
    if (previous == conn) {
        return {};
    }
    return previous;
}

void DeviceRegistry::unbind(std::int32_t device_id, const net::TcpConnection* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = owners_.find(device_id);
    if (it == owners_.end()) {
        return;
    }
    const net::TcpConnectionPtr current = it->second.lock();
    if (!current || current.get() == conn) {
        owners_.erase(it);
    }
}

std::size_t DeviceRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return owners_.size();
}

}  // namespace lg::gateway
