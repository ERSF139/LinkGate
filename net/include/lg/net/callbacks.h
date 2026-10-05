#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace lg::net {

class Buffer;
class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&, std::size_t)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;

}  // namespace lg::net
