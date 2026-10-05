#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "lg/gateway/config.h"
#include "lg/gateway/stats.h"
#include "lg/net/callbacks.h"
#include "lg/net/timer.h"
#include "lg/net/timing_wheel.h"

namespace lg::net {
class EventLoop;
}

namespace lg::gateway {

// 一次读事件里从同一台设备解析出来的合法 Sample 帧，原样首尾相接。
// 用 shared_ptr<const Batch> 在各 IO 线程之间共享，一份数据只分配一次。
struct Batch {
    std::vector<std::uint8_t> bytes;
    std::uint64_t samples = 0;
};
using BatchPtr = std::shared_ptr<const Batch>;

// 一个 IO 线程的全部网关状态：这个线程上的订阅者、设备空闲时间轮、计数器。
// 除标明"任意线程"的接口外，只能在 loop() 所在线程调用（见 decisions.md D-002）。
class Shard {
public:
    Shard(net::EventLoop* loop, std::size_t index, const Config& config);

    Shard(const Shard&) = delete;
    Shard& operator=(const Shard&) = delete;

    void start_in_loop();
    void stop_in_loop();

    void add_subscriber(const net::TcpConnectionPtr& conn);
    bool remove_subscriber(const net::TcpConnectionPtr& conn);
    void deliver(const BatchPtr& batch);
    void touch(const net::TimingWheel::Entry& entry) { wheel_.touch(entry); }

    // 任意线程。
    net::EventLoop* loop() const noexcept { return loop_; }
    std::size_t index() const noexcept { return index_; }
    bool has_subscribers() const noexcept { return subscriber_count_.load(std::memory_order_relaxed) > 0; }
    ShardCounters& counters() noexcept { return counters_; }
    const ShardCounters& counters() const noexcept { return counters_; }

private:
    struct Subscriber {
        net::TcpConnectionPtr conn;
        bool paused = false;
        net::TimePoint paused_since{};
    };

    bool deliver_to(Subscriber& subscriber, const std::uint8_t* data, std::size_t size,
                    std::uint64_t samples, net::TimePoint now);
    void on_heartbeat_tick();

    net::EventLoop* loop_;
    std::size_t index_;
    const Config& config_;
    std::unordered_map<std::uint64_t, Subscriber> subscribers_;
    std::atomic<std::size_t> subscriber_count_{0};
    net::TimingWheel wheel_;
    net::TimerId heartbeat_timer_;
    net::TimerId wheel_timer_;
    ShardCounters counters_;
};

// 把一台设备的数据扇出到所有 IO 线程上的订阅者。
class Hub {
public:
    explicit Hub(std::vector<std::unique_ptr<Shard>> shards);

    // 任意线程（映射表构造后只读）。
    Shard& shard_for(const net::EventLoop* loop) const;
    const std::vector<std::unique_ptr<Shard>>& shards() const noexcept { return shards_; }

    // 在 origin 的线程里调用：同线程的订阅者直接发送，其他线程每个只投递一次任务。
    void broadcast(Shard& origin, const BatchPtr& batch) const;

    // 关闭阶段：之后不再向任何 loop 投递转发任务。调用方随后要在每个 IO loop 上做一次同步屏障，
    // 保证已经读到旧值、正在投递的线程都已结束，再开始停线程池。
    void stop() noexcept { stopping_.store(true, std::memory_order_release); }
    bool stopping() const noexcept { return stopping_.load(std::memory_order_acquire); }

private:
    std::vector<std::unique_ptr<Shard>> shards_;
    std::unordered_map<const net::EventLoop*, Shard*> by_loop_;
    std::atomic<bool> stopping_{false};
};

}  // namespace lg::gateway
