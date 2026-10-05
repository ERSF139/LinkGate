#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "lg/net/callbacks.h"
#include "lg/net/inet_address.h"
#include "lg/net/unique_fd.h"

namespace lg::net {

class Acceptor;
class EventLoop;
class EventLoopThreadPool;

// 监听一个端口：accept 在 base loop 里完成，新连接按轮询分给 pool 里的 IO loop。
// pool 可以由多个 TcpServer 共享（网关的设备端口和订阅端口共用一组 IO 线程）。
// 构造、start()、析构都必须在 base loop 线程里进行；pool 必须比 TcpServer 活得久。
class TcpServer {
public:
    TcpServer(EventLoop* loop, const InetAddress& listen_addr, std::string name,
              EventLoopThreadPool* pool = nullptr, bool reuse_port = false);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void set_connection_callback(ConnectionCallback cb) { connection_callback_ = std::move(cb); }
    void set_message_callback(MessageCallback cb) { message_callback_ = std::move(cb); }
    void set_write_complete_callback(WriteCompleteCallback cb) { write_complete_callback_ = std::move(cb); }

    void start();

    InetAddress listen_address() const noexcept;
    const std::string& name() const noexcept { return name_; }
    // base loop 线程。
    std::size_t connection_count() const noexcept;

private:
    // 连接的关闭回调只持有 Core 的 weak_ptr：TcpServer 析构之后才关闭的连接不会碰到悬空的 this。
    struct Core {
        EventLoop* loop;
        std::unordered_map<std::uint64_t, TcpConnectionPtr> connections;
    };

    void new_connection(UniqueFd socket, const InetAddress& peer);
    static void remove_connection(const std::weak_ptr<Core>& weak_core, const TcpConnectionPtr& conn);

    EventLoop* loop_;
    std::string name_;
    EventLoopThreadPool* pool_;
    std::unique_ptr<Acceptor> acceptor_;
    std::shared_ptr<Core> core_;
    std::atomic<bool> started_{false};
    std::uint64_t next_connection_id_ = 0;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    WriteCompleteCallback write_complete_callback_;
};

}  // namespace lg::net
