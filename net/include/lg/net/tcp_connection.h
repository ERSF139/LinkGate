#pragma once

#include <any>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "lg/net/buffer.h"
#include "lg/net/callbacks.h"
#include "lg/net/channel.h"
#include "lg/net/inet_address.h"
#include "lg/net/unique_fd.h"

namespace lg::net {

class EventLoop;

// 一条已建立的 TCP 连接。总是由 shared_ptr 持有：
// 关闭发生在 IO 线程的回调里，而持有者（服务器、业务层）可能还在别的线程引用它。
// 除了标明"任意线程"的接口，其余只能在所属 loop 的线程里调用。
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(EventLoop* loop, std::string name, std::uint64_t id, UniqueFd socket,
                  const InetAddress& local, const InetAddress& peer);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    EventLoop* loop() const noexcept { return loop_; }
    const std::string& name() const noexcept { return name_; }
    std::uint64_t id() const noexcept { return id_; }
    const InetAddress& local_address() const noexcept { return local_; }
    const InetAddress& peer_address() const noexcept { return peer_; }

    // 任意线程。
    bool connected() const noexcept { return state_.load(std::memory_order_acquire) == State::Connected; }
    bool disconnected() const noexcept {
        return state_.load(std::memory_order_acquire) == State::Disconnected;
    }
    void send(const void* data, std::size_t size);
    void shutdown();
    void force_close();
    void start_read();
    void stop_read();

    // loop 线程。
    void set_tcp_no_delay(bool on) noexcept;
    std::size_t output_bytes() const noexcept { return output_.readable_bytes(); }
    std::any& context() noexcept { return context_; }
    bool is_reading() const noexcept { return reading_; }

    void set_connection_callback(ConnectionCallback cb) { connection_callback_ = std::move(cb); }
    void set_message_callback(MessageCallback cb) { message_callback_ = std::move(cb); }
    void set_write_complete_callback(WriteCompleteCallback cb) { write_complete_callback_ = std::move(cb); }
    void set_high_water_mark_callback(HighWaterMarkCallback cb, std::size_t mark) {
        high_water_mark_callback_ = std::move(cb);
        high_water_mark_ = mark;
    }
    void set_close_callback(CloseCallback cb) { close_callback_ = std::move(cb); }

    // 由 TcpServer / TcpClient 在 loop 线程里调用。
    void connection_established();
    void connection_destroyed();

private:
    enum class State { Connecting, Connected, Disconnecting, Disconnected };

    void handle_read();
    void handle_write();
    void handle_close();
    void handle_error();

    void send_in_loop(const void* data, std::size_t size);
    void shutdown_in_loop();

    EventLoop* loop_;
    const std::string name_;
    const std::uint64_t id_;
    std::atomic<State> state_{State::Connecting};
    bool reading_ = false;

    UniqueFd socket_;
    Channel channel_;
    const InetAddress local_;
    const InetAddress peer_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    WriteCompleteCallback write_complete_callback_;
    HighWaterMarkCallback high_water_mark_callback_;
    CloseCallback close_callback_;
    std::size_t high_water_mark_ = 64 * 1024 * 1024;

    Buffer input_;
    Buffer output_;
    std::any context_;
};

}  // namespace lg::net
