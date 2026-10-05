#include "lg/gateway/hub.h"

#include <cassert>
#include <chrono>

#include "devproto/frame.h"
#include "lg/net/event_loop.h"
#include "lg/net/log.h"
#include "lg/net/tcp_connection.h"

namespace lg::gateway {

Shard::Shard(net::EventLoop* loop, std::size_t index, const Config& config)
    : loop_(loop), index_(index), config_(config), wheel_(config.idle_wheel_slots()) {}

void Shard::start_in_loop() {
    loop_->assert_in_loop_thread();
    heartbeat_timer_ = loop_->run_every(config_.heartbeat_interval, [this] { on_heartbeat_tick(); });
    wheel_timer_ = loop_->run_every(config_.tick_interval, [this] { wheel_.tick(); });
}

void Shard::stop_in_loop() {
    loop_->assert_in_loop_thread();
    loop_->cancel(heartbeat_timer_);
    loop_->cancel(wheel_timer_);
    heartbeat_timer_ = net::TimerId{};
    wheel_timer_ = net::TimerId{};
}

void Shard::add_subscriber(const net::TcpConnectionPtr& conn) {
    loop_->assert_in_loop_thread();
    Subscriber subscriber;
    subscriber.conn = conn;
    subscribers_[conn->id()] = std::move(subscriber);
    subscriber_count_.store(subscribers_.size(), std::memory_order_relaxed);
}

bool Shard::remove_subscriber(const net::TcpConnectionPtr& conn) {
    loop_->assert_in_loop_thread();
    const bool removed = subscribers_.erase(conn->id()) > 0;
    subscriber_count_.store(subscribers_.size(), std::memory_order_relaxed);
    return removed;
}

void Shard::deliver(const BatchPtr& batch) {
    loop_->assert_in_loop_thread();
    const net::TimePoint now = net::Clock::now();
    for (auto& entry : subscribers_) {
        if (deliver_to(entry.second, batch->bytes.data(), batch->bytes.size(), batch->samples, now)) {
            bump(counters_.samples_forwarded, batch->samples);
        }
    }
}

bool Shard::deliver_to(Subscriber& subscriber, const std::uint8_t* data, std::size_t size,
                       std::uint64_t samples, net::TimePoint now) {
    const net::TcpConnectionPtr& conn = subscriber.conn;
    if (!conn->connected()) {
        return false;
    }
    const std::size_t pending = conn->output_bytes();
    if (subscriber.paused) {
        if (pending > config_.low_water_bytes) {
            bump(counters_.subscriber_dropped_samples, samples);
            return false;
        }
        subscriber.paused = false;
        LG_LOG(Info) << conn->name() << " " << conn->peer_address().to_string()
                     << " caught up, resuming delivery";
    }
    // 只看已经积压的字节，不把这一批算进去：一次读事件攒出的批次本身就可能超过高水位，
    // 否则积压为 0 的订阅者也会被判定为过慢。缓冲上限因此是高水位加一个批次。
    if (pending >= config_.high_water_bytes) {
        subscriber.paused = true;
        subscriber.paused_since = now;
        bump(counters_.subscriber_pauses);
        bump(counters_.subscriber_dropped_samples, samples);
        LG_LOG(Warn) << conn->name() << " " << conn->peer_address().to_string() << " backlog " << pending
                     << " bytes exceeds high water, dropping its data until it drains";
        return false;
    }
    conn->send(data, size);
    bump(counters_.bytes_out, static_cast<std::uint64_t>(size));
    return true;
}

void Shard::on_heartbeat_tick() {
    const net::TimePoint now = net::Clock::now();
    const auto& heartbeat = devproto::heartbeat_frame();
    for (auto& entry : subscribers_) {
        Subscriber& subscriber = entry.second;
        if (subscriber.paused && now - subscriber.paused_since >= config_.stall_timeout) {
            bump(counters_.subscriber_stall_disconnects);
            LG_LOG(Warn) << subscriber.conn->name() << " " << subscriber.conn->peer_address().to_string()
                         << " stalled for "
                         << std::chrono::duration_cast<std::chrono::milliseconds>(now - subscriber.paused_since)
                                .count()
                         << " ms, disconnecting";
            subscriber.conn->force_close();
            continue;
        }
        if (deliver_to(subscriber, heartbeat.data(), heartbeat.size(), 0, now)) {
            bump(counters_.heartbeats_sent);
        }
    }
}

Hub::Hub(std::vector<std::unique_ptr<Shard>> shards) : shards_(std::move(shards)) {
    for (const auto& shard : shards_) {
        by_loop_[shard->loop()] = shard.get();
    }
}

Shard& Hub::shard_for(const net::EventLoop* loop) const {
    const auto it = by_loop_.find(loop);
    assert(it != by_loop_.end());
    return *it->second;
}

void Hub::broadcast(Shard& origin, const BatchPtr& batch) const {
    if (stopping()) {
        return;
    }
    for (const auto& shard : shards_) {
        if (!shard->has_subscribers()) {
            continue;
        }
        if (shard.get() == &origin) {
            shard->deliver(batch);
        } else {
            Shard* target = shard.get();
            target->loop()->queue_in_loop([target, batch] { target->deliver(batch); });
        }
    }
}

}  // namespace lg::gateway
