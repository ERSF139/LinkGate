#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "lg/net/callbacks.h"
#include "lg/net/inet_address.h"
#include "lg/net/unique_fd.h"

namespace lg::net {

class Connector;
class EventLoop;

// 一条主动发起的连接，可选断线重连。回调要在 connect() 之前设置。
// 必须在它的 EventLoop 销毁之前析构；可以在任意线程析构（不在 loop 线程时会同步等待 loop 完成清理）。
class TcpClient {
public:
    TcpClient(EventLoop* loop, const InetAddress& server, std::string name);
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    void set_connection_callback(ConnectionCallback cb) { state_->connection_callback = std::move(cb); }
    void set_message_callback(MessageCallback cb) { state_->message_callback = std::move(cb); }
    void set_write_complete_callback(WriteCompleteCallback cb) {
        state_->write_complete_callback = std::move(cb);
    }
    void enable_retry(bool on) noexcept { state_->retry.store(on, std::memory_order_release); }

    void connect();
    void disconnect();

    EventLoop* loop() const noexcept { return state_->loop; }
    TcpConnectionPtr connection() const;

private:
    // 连接和 Connector 的回调只持有它的 weak_ptr，TcpClient 析构后到达的回调不会访问悬空对象。
    struct State {
        EventLoop* loop = nullptr;
        std::string name;
        std::shared_ptr<Connector> connector;
        std::atomic<bool> retry{false};
        std::atomic<bool> wanted{false};
        std::uint64_t next_id = 0;
        mutable std::mutex mutex;
        TcpConnectionPtr connection;
        ConnectionCallback connection_callback;
        MessageCallback message_callback;
        WriteCompleteCallback write_complete_callback;
    };

    static void on_connected(const std::weak_ptr<State>& weak_state, UniqueFd socket);
    static void on_closed(const std::weak_ptr<State>& weak_state, const TcpConnectionPtr& conn);

    std::shared_ptr<State> state_;
};

}  // namespace lg::net
