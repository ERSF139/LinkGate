#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace lg::gateway {

// 每个 IO 线程一份计数器，只有这个线程写，其他线程（统计输出）只读。
// 单写者不需要 fetch_add 这种带锁前缀的读改写；按 64 字节对齐，避免不同线程的计数器落在同一缓存行（伪共享）。
struct alignas(64) ShardCounters {
    std::atomic<std::int64_t> devices_online{0};
    std::atomic<std::int64_t> subscribers_online{0};

    std::atomic<std::uint64_t> device_connections{0};
    std::atomic<std::uint64_t> subscriber_connections{0};

    std::atomic<std::uint64_t> bytes_in{0};
    std::atomic<std::uint64_t> frames_in{0};
    std::atomic<std::uint64_t> samples_in{0};
    std::atomic<std::uint64_t> heartbeats_in{0};
    std::atomic<std::uint64_t> bad_frames{0};
    std::atomic<std::uint64_t> dropped_bytes{0};

    std::atomic<std::uint64_t> samples_forwarded{0};
    std::atomic<std::uint64_t> heartbeats_sent{0};
    std::atomic<std::uint64_t> bytes_out{0};

    std::atomic<std::uint64_t> subscriber_dropped_samples{0};
    std::atomic<std::uint64_t> subscriber_pauses{0};
    std::atomic<std::uint64_t> subscriber_stall_disconnects{0};

    std::atomic<std::uint64_t> device_idle_kicks{0};
    std::atomic<std::uint64_t> device_id_conflicts{0};
    std::atomic<std::uint64_t> device_id_mismatches{0};
};

template <class T>
struct NonDeduced {
    using type = T;
};

template <class T>
inline void bump(std::atomic<T>& counter, typename NonDeduced<T>::type delta = 1) noexcept {
    counter.store(counter.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
}

struct StatsSnapshot {
    std::int64_t devices_online = 0;
    std::int64_t subscribers_online = 0;
    std::uint64_t device_connections = 0;
    std::uint64_t subscriber_connections = 0;
    std::uint64_t bytes_in = 0;
    std::uint64_t frames_in = 0;
    std::uint64_t samples_in = 0;
    std::uint64_t heartbeats_in = 0;
    std::uint64_t bad_frames = 0;
    std::uint64_t dropped_bytes = 0;
    std::uint64_t samples_forwarded = 0;
    std::uint64_t heartbeats_sent = 0;
    std::uint64_t bytes_out = 0;
    std::uint64_t subscriber_dropped_samples = 0;
    std::uint64_t subscriber_pauses = 0;
    std::uint64_t subscriber_stall_disconnects = 0;
    std::uint64_t device_idle_kicks = 0;
    std::uint64_t device_id_conflicts = 0;
    std::uint64_t device_id_mismatches = 0;

    void add(const ShardCounters& counters) noexcept;
    std::string to_string() const;
};

}  // namespace lg::gateway
