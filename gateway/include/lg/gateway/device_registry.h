#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "lg/net/callbacks.h"

namespace lg::gateway {

// 设备 ID 到当前连接的映射，跨 IO 线程共享，用一把锁保护（见 decisions.md D-002）。
// 同一个 ID 只能有一条连接：新连接上线时顶掉旧连接（典型场景是设备重启后旧连接还没被发现断开）。
class DeviceRegistry {
public:
    // 返回被顶掉的旧连接（调用方负责关闭），没有则返回空。
    net::TcpConnectionPtr bind(std::int32_t device_id, const net::TcpConnectionPtr& conn);
    // 只有当前登记的仍是 conn 时才删除，避免旧连接下线时把新连接的登记删掉。
    void unbind(std::int32_t device_id, const net::TcpConnection* conn);
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::int32_t, std::weak_ptr<net::TcpConnection>> owners_;
};

}  // namespace lg::gateway
