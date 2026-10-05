// 网关的端到端测试：网关跑在后台线程里，设备和订阅者用独立实现的阻塞 socket 客户端。
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "devproto/frame.h"
#include "devproto/parser.h"
#include "golden.h"
#include "lg/gateway/gateway.h"
#include "lg/net/event_loop.h"
#include "net_util.h"

using lg::gateway::Config;
using lg::gateway::StatsSnapshot;
using lg::test::BlockingClient;
using Bytes = std::vector<std::uint8_t>;
using namespace std::chrono_literals;

namespace {

class GatewayRunner {
public:
    explicit GatewayRunner(Config config) {
        thread_ = std::thread([this, config] {
            lg::net::EventLoop loop;
            lg::gateway::Gateway gateway(&loop, config);
            gateway.start();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = &loop;
                gateway_ = &gateway;
                device_port_ = gateway.device_port();
                subscriber_port_ = gateway.subscriber_port();
            }
            ready_.notify_all();
            loop.loop();
        });
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return gateway_ != nullptr; });
    }

    ~GatewayRunner() {
        loop_->quit();
        thread_.join();
    }

    std::uint16_t device_port() const { return device_port_; }
    std::uint16_t subscriber_port() const { return subscriber_port_; }
    StatsSnapshot stats() const { return gateway_->stats(); }

private:
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    lg::net::EventLoop* loop_ = nullptr;
    lg::gateway::Gateway* gateway_ = nullptr;
    std::uint16_t device_port_ = 0;
    std::uint16_t subscriber_port_ = 0;
};

Config test_config() {
    Config config;
    config.bind_ip = "127.0.0.1";
    config.device_port = 0;
    config.subscriber_port = 0;
    config.io_threads = 2;
    config.tick_interval = 50ms;
    config.device_idle_timeout = 400ms;
    config.heartbeat_interval = 100ms;
    config.stall_timeout = 500ms;
    config.stats_interval = 0ms;
    return config;
}

Bytes sample_frame(std::int32_t device_id, std::int64_t timestamp = 1700000000000) {
    devproto::Sample sample;
    sample.device_id = device_id;
    sample.timestamp_ms = timestamp;
    sample.temperature = 25.5;
    sample.pressure = 101.3;
    sample.vibration = 0.5;
    const auto frame = devproto::encode_sample_frame(sample);
    return Bytes(frame.begin(), frame.end());
}

// 订阅者侧：持续读取并解析，直到收集到 want_samples 个 Sample 或超时。
struct Received {
    std::vector<Bytes> samples;
    std::size_t heartbeats = 0;
};

Received collect(BlockingClient& subscriber, std::size_t want_samples, std::chrono::milliseconds timeout,
                 std::size_t want_heartbeats = 0) {
    Received received;
    devproto::StreamParser parser;
    Bytes buffer;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline &&
           (received.samples.size() < want_samples || received.heartbeats < want_heartbeats)) {
        const Bytes chunk = subscriber.read_some(50ms);
        buffer.insert(buffer.end(), chunk.begin(), chunk.end());
        while (true) {
            const auto result = parser.parse(buffer.data(), buffer.size());
            if (result.frame) {
                if (result.frame->type == devproto::MessageType::Sample) {
                    received.samples.emplace_back(result.frame->data, result.frame->data + result.frame->size);
                } else {
                    ++received.heartbeats;
                }
            }
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(result.consumed));
            if (!result.frame) break;
        }
    }
    return received;
}

void connect_subscriber(GatewayRunner& gateway, BlockingClient& subscriber, std::int64_t expected_online = 1) {
    ASSERT_TRUE(subscriber.connect(gateway.subscriber_port()));
    ASSERT_TRUE(lg::test::wait_until([&] { return gateway.stats().subscribers_online == expected_online; }));
}

}  // namespace

TEST(Gateway, SendsHeartbeatsToSubscribersWithoutDevices) {
    GatewayRunner gateway(test_config());
    BlockingClient subscriber;
    connect_subscriber(gateway, subscriber);
    const Received received = collect(subscriber, 0, 2s, 3);
    EXPECT_GE(received.heartbeats, 3u);
}

TEST(Gateway, ForwardsSampleBytesVerbatim) {
    GatewayRunner gateway(test_config());
    BlockingClient subscriber;
    connect_subscriber(gateway, subscriber);

    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    const Bytes golden = lg::test::golden_frame("sample_basic");
    ASSERT_TRUE(device.send_all(golden));

    const Received received = collect(subscriber, 1, 2s);
    ASSERT_EQ(received.samples.size(), 1u);
    EXPECT_EQ(received.samples[0], golden);
    EXPECT_TRUE(lg::test::wait_until([&] { return gateway.stats().samples_forwarded == 1; }));
}

TEST(Gateway, DeviceHeartbeatsAreNotForwarded) {
    GatewayRunner gateway(test_config());
    BlockingClient subscriber;
    connect_subscriber(gateway, subscriber);
    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    const auto& hb = devproto::heartbeat_frame();
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(device.send_all(hb.data(), hb.size()));
    }
    ASSERT_TRUE(lg::test::wait_until([&] { return gateway.stats().heartbeats_in == 20; }));
    EXPECT_EQ(gateway.stats().samples_forwarded, 0u);
}

TEST(Gateway, InvalidFramesAreDroppedAndOnlyValidSamplesReachSubscribers) {
    GatewayRunner gateway(test_config());
    BlockingClient subscriber;
    connect_subscriber(gateway, subscriber);
    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));

    Bytes stream;
    const std::string garbage = "noise before frames";
    stream.insert(stream.end(), garbage.begin(), garbage.end());
    const Bytes unknown = devproto::encode_frame(static_cast<devproto::MessageType>(0x03), nullptr, 0);
    stream.insert(stream.end(), unknown.begin(), unknown.end());
    Bytes corrupted = sample_frame(7);
    corrupted[10] ^= 0xFF;
    stream.insert(stream.end(), corrupted.begin(), corrupted.end());
    const Bytes good = sample_frame(7, 42);
    stream.insert(stream.end(), good.begin(), good.end());
    ASSERT_TRUE(device.send_all(stream));

    const Received received = collect(subscriber, 2, 1s);
    ASSERT_EQ(received.samples.size(), 1u);
    EXPECT_EQ(received.samples[0], good);
    const StatsSnapshot stats = gateway.stats();
    EXPECT_GE(stats.bad_frames, 2u);
    EXPECT_GT(stats.dropped_bytes, garbage.size());
}

TEST(Gateway, FansOutToSubscribersOnEveryIoThread) {
    Config config = test_config();
    config.io_threads = 3;
    GatewayRunner gateway(config);
    std::vector<BlockingClient> subscribers(4);
    for (std::size_t i = 0; i < subscribers.size(); ++i) {
        connect_subscriber(gateway, subscribers[i], static_cast<std::int64_t>(i + 1));
    }
    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    Bytes burst;
    for (int i = 0; i < 50; ++i) {
        const Bytes frame = sample_frame(9, i);
        burst.insert(burst.end(), frame.begin(), frame.end());
    }
    ASSERT_TRUE(device.send_all(burst));
    for (auto& subscriber : subscribers) {
        const Received received = collect(subscriber, 50, 3s);
        ASSERT_EQ(received.samples.size(), 50u);
        for (int i = 0; i < 50; ++i) {
            EXPECT_EQ(received.samples[static_cast<std::size_t>(i)], sample_frame(9, i));
        }
    }
}

TEST(Gateway, SilentDeviceIsClosedAfterIdleTimeout) {
    GatewayRunner gateway(test_config());
    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(device.wait_closed(3s));
    EXPECT_GE(std::chrono::steady_clock::now() - start, 300ms);
    EXPECT_TRUE(lg::test::wait_until([&] { return gateway.stats().device_idle_kicks == 1; }));
}

TEST(Gateway, DeviceSendingHeartbeatsStaysConnected) {
    GatewayRunner gateway(test_config());
    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    const auto& hb = devproto::heartbeat_frame();
    for (int i = 0; i < 12; ++i) {
        ASSERT_TRUE(device.send_all(hb.data(), hb.size()));
        std::this_thread::sleep_for(100ms);
    }
    EXPECT_EQ(gateway.stats().device_idle_kicks, 0u);
    EXPECT_EQ(gateway.stats().devices_online, 1);
}

TEST(Gateway, TricklingFakeHeaderDoesNotKeepDeviceAlive) {
    GatewayRunner gateway(test_config());
    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    const Bytes header{0xA5, 0x5A, 0x01, 0x01, 0x00, 0x24};
    ASSERT_TRUE(device.send_all(header));
    for (int i = 0; i < 30; ++i) {
        const std::uint8_t byte = 0;
        if (!device.send_all(&byte, 1)) break;
        std::this_thread::sleep_for(30ms);
    }
    EXPECT_TRUE(lg::test::wait_until([&] { return gateway.stats().device_idle_kicks == 1; }, 3s));
}

TEST(Gateway, DeviceIdMismatchClosesConnection) {
    GatewayRunner gateway(test_config());
    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    ASSERT_TRUE(device.send_all(sample_frame(7)));
    ASSERT_TRUE(device.send_all(sample_frame(8)));
    EXPECT_TRUE(device.wait_closed(2s));
    EXPECT_TRUE(lg::test::wait_until([&] { return gateway.stats().device_id_mismatches == 1; }));
}

TEST(Gateway, NewConnectionWithSameDeviceIdReplacesOldOne) {
    GatewayRunner gateway(test_config());
    BlockingClient first;
    ASSERT_TRUE(first.connect(gateway.device_port()));
    ASSERT_TRUE(first.send_all(sample_frame(5)));
    ASSERT_TRUE(lg::test::wait_until([&] { return gateway.stats().samples_in == 1; }));

    BlockingClient second;
    ASSERT_TRUE(second.connect(gateway.device_port()));
    ASSERT_TRUE(second.send_all(sample_frame(5)));
    EXPECT_TRUE(first.wait_closed(2s));
    EXPECT_TRUE(lg::test::wait_until([&] { return gateway.stats().device_id_conflicts == 1; }));
    EXPECT_TRUE(lg::test::wait_until([&] { return gateway.stats().devices_online == 1; }));
}

TEST(Gateway, SlowSubscriberIsPausedThenDisconnected) {
    Config config = test_config();
    config.high_water_bytes = 64 * 1024;
    config.low_water_bytes = 16 * 1024;
    config.stall_timeout = 300ms;
    config.device_idle_timeout = 5000ms;
    GatewayRunner gateway(config);

    BlockingClient stuck;
    stuck.set_receive_buffer(4096);
    connect_subscriber(gateway, stuck);

    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    Bytes burst;
    for (int i = 0; i < 2000; ++i) {
        const Bytes frame = sample_frame(11, i);
        burst.insert(burst.end(), frame.begin(), frame.end());
    }
    // 订阅者从不读取：持续灌数据，直到网关自己的输出缓冲越过高水位。
    for (int round = 0; round < 400 && gateway.stats().subscriber_pauses == 0; ++round) {
        ASSERT_TRUE(device.send_all(burst));
    }
    ASSERT_GE(gateway.stats().subscriber_pauses, 1u);
    EXPECT_GT(gateway.stats().subscriber_dropped_samples, 0u);
    EXPECT_TRUE(lg::test::wait_until([&] { return gateway.stats().subscriber_stall_disconnects == 1; }, 3s));
    EXPECT_TRUE(lg::test::wait_until([&] { return gateway.stats().subscribers_online == 0; }));
}

TEST(Gateway, SingleThreadModeWorks) {
    Config config = test_config();
    config.io_threads = 0;
    GatewayRunner gateway(config);
    BlockingClient subscriber;
    connect_subscriber(gateway, subscriber);
    BlockingClient device;
    ASSERT_TRUE(device.connect(gateway.device_port()));
    ASSERT_TRUE(device.send_all(sample_frame(3)));
    const Received received = collect(subscriber, 1, 2s);
    ASSERT_EQ(received.samples.size(), 1u);
}
