#pragma once

#include <cstdint>
#include <memory>

#include "lg/gateway/config.h"
#include "lg/gateway/device_registry.h"
#include "lg/gateway/stats.h"
#include "lg/net/callbacks.h"
#include "lg/net/timer.h"

namespace lg::net {
class Buffer;
class EventLoop;
class EventLoopThreadPool;
class TcpServer;
}  // namespace lg::net

namespace lg::gateway {

class Hub;

// 设备端口 + 订阅端口 + 一组共享的 IO 线程。
// 构造、start()、析构都在 base loop 线程里进行；析构前 base loop 不必仍在运行。
class Gateway {
public:
    Gateway(net::EventLoop* base_loop, Config config);
    ~Gateway();

    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    // 端口被占用等启动失败抛 std::system_error。
    void start();

    std::uint16_t device_port() const;
    std::uint16_t subscriber_port() const;
    const Config& config() const noexcept { return config_; }

    // 任意线程。
    StatsSnapshot stats() const;
    std::size_t registered_devices() const { return registry_.size(); }

private:
    void on_device_connection(const net::TcpConnectionPtr& conn);
    void on_device_message(const net::TcpConnectionPtr& conn, net::Buffer* buffer);
    void on_subscriber_connection(const net::TcpConnectionPtr& conn);
    void on_subscriber_message(const net::TcpConnectionPtr& conn, net::Buffer* buffer);
    void log_stats();

    net::EventLoop* base_loop_;
    const Config config_;

    // 析构顺序依赖声明顺序：服务器先析构（连接在各自 IO 线程里注销），
    // 然后线程池 join，最后才是 Hub —— 已经投递到 IO 线程的转发任务还引用着它。
    std::unique_ptr<Hub> hub_;
    DeviceRegistry registry_;
    std::unique_ptr<net::EventLoopThreadPool> pool_;
    std::unique_ptr<net::TcpServer> device_server_;
    std::unique_ptr<net::TcpServer> subscriber_server_;

    bool shards_started_ = false;
    net::TimerId stats_timer_;
    StatsSnapshot last_stats_;
    net::TimePoint last_stats_time_{};
};

}  // namespace lg::gateway
