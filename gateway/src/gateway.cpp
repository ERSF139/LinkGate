#include "lg/gateway/gateway.h"

#include <any>
#include <optional>
#include <stdexcept>
#include <vector>

#include "devproto/byte_order.h"
#include "devproto/parser.h"
#include "lg/gateway/hub.h"
#include "lg/net/buffer.h"
#include "lg/net/event_loop.h"
#include "lg/net/event_loop_thread.h"
#include "lg/net/log.h"
#include "lg/net/tcp_connection.h"
#include "lg/net/tcp_server.h"

namespace lg::gateway {

namespace {

struct DeviceSession {
    devproto::StreamParser parser;
    devproto::ParserStats reported;
    std::optional<std::int32_t> device_id;
    std::weak_ptr<void> idle_entry;
};

using DeviceSessionPtr = std::shared_ptr<DeviceSession>;

// 时间轮里的条目：它的最后一个强引用随时间轮的槽释放时，说明设备在超时时间内没有发来任何合法帧。
struct IdleEntry {
    std::weak_ptr<net::TcpConnection> conn;
    ShardCounters* counters = nullptr;

    ~IdleEntry() {
        const net::TcpConnectionPtr c = conn.lock();
        if (c && c->connected()) {
            bump(counters->device_idle_kicks);
            LG_LOG(Info) << c->name() << " " << c->peer_address().to_string()
                         << " sent no valid frame within the idle timeout, closing";
            c->force_close();
        }
    }
};

DeviceSession* device_session(const net::TcpConnectionPtr& conn) {
    auto* session = std::any_cast<DeviceSessionPtr>(&conn->context());
    return session != nullptr ? session->get() : nullptr;
}

void account_parser(DeviceSession& session, ShardCounters& counters) {
    const devproto::ParserStats& now = session.parser.stats();
    bump(counters.frames_in, now.frames - session.reported.frames);
    bump(counters.bad_frames, now.bad_frames - session.reported.bad_frames);
    bump(counters.dropped_bytes, now.dropped_bytes - session.reported.dropped_bytes);
    session.reported = now;
}

net::InetAddress listen_address(const std::string& ip, std::uint16_t port) {
    const auto address = net::InetAddress::from_ip_port(ip, port);
    if (!address) {
        throw std::invalid_argument("invalid --bind address: " + ip);
    }
    return *address;
}

}  // namespace

Gateway::Gateway(net::EventLoop* base_loop, Config config)
    : base_loop_(base_loop), config_(std::move(config)) {}

Gateway::~Gateway() {
    base_loop_->assert_in_loop_thread();
    base_loop_->cancel(stats_timer_);
    if (shards_started_) {
        // 先停止跨线程转发，再借每个 shard 的同步 stop 做屏障：线程池逐个退出时，
        // 还在运行的 IO 线程不能再往已经退出的 loop 里投递任务。
        hub_->stop();
        for (const auto& shard : hub_->shards()) {
            Shard* s = shard.get();
            s->loop()->run_in_loop_sync([s] { s->stop_in_loop(); });
        }
    }
    subscriber_server_.reset();
    device_server_.reset();
    pool_.reset();
}

void Gateway::start() {
    base_loop_->assert_in_loop_thread();

    pool_ = std::make_unique<net::EventLoopThreadPool>(base_loop_, config_.io_threads, "lg-io");
    pool_->start();

    std::vector<std::unique_ptr<Shard>> shards;
    for (std::size_t i = 0; i < pool_->loops().size(); ++i) {
        shards.push_back(std::make_unique<Shard>(pool_->loops()[i], i, config_));
    }
    hub_ = std::make_unique<Hub>(std::move(shards));
    for (const auto& shard : hub_->shards()) {
        Shard* s = shard.get();
        s->loop()->run_in_loop_sync([s] { s->start_in_loop(); });
    }
    shards_started_ = true;

    device_server_ = std::make_unique<net::TcpServer>(
        base_loop_, listen_address(config_.bind_ip, config_.device_port), "device", pool_.get());
    device_server_->set_connection_callback([this](const net::TcpConnectionPtr& c) { on_device_connection(c); });
    device_server_->set_message_callback(
        [this](const net::TcpConnectionPtr& c, net::Buffer* b) { on_device_message(c, b); });

    subscriber_server_ = std::make_unique<net::TcpServer>(
        base_loop_, listen_address(config_.bind_ip, config_.subscriber_port), "subscriber", pool_.get());
    subscriber_server_->set_connection_callback(
        [this](const net::TcpConnectionPtr& c) { on_subscriber_connection(c); });
    subscriber_server_->set_message_callback(
        [this](const net::TcpConnectionPtr& c, net::Buffer* b) { on_subscriber_message(c, b); });

    device_server_->start();
    subscriber_server_->start();

    if (config_.stats_interval.count() > 0) {
        last_stats_time_ = net::Clock::now();
        stats_timer_ = base_loop_->run_every(config_.stats_interval, [this] { log_stats(); });
    }
    LG_LOG(Info) << "LinkGate listening: devices on " << device_server_->listen_address().to_string()
                 << ", subscribers on " << subscriber_server_->listen_address().to_string() << ", "
                 << pool_->loops().size() << " IO loop(s)";
}

std::uint16_t Gateway::device_port() const { return device_server_->listen_address().port(); }

std::uint16_t Gateway::subscriber_port() const { return subscriber_server_->listen_address().port(); }

StatsSnapshot Gateway::stats() const {
    StatsSnapshot snapshot;
    if (hub_) {
        for (const auto& shard : hub_->shards()) {
            snapshot.add(shard->counters());
        }
    }
    return snapshot;
}

void Gateway::on_device_connection(const net::TcpConnectionPtr& conn) {
    Shard& shard = hub_->shard_for(conn->loop());
    ShardCounters& counters = shard.counters();
    if (conn->connected()) {
        auto session = std::make_shared<DeviceSession>();
        auto entry = std::make_shared<IdleEntry>();
        entry->conn = conn;
        entry->counters = &counters;
        shard.touch(entry);
        session->idle_entry = entry;
        conn->context() = DeviceSessionPtr(session);
        bump(counters.devices_online, std::int64_t{1});
        bump(counters.device_connections);
        LG_LOG(Debug) << conn->name() << " device connected from " << conn->peer_address().to_string();
        return;
    }
    DeviceSession* session = device_session(conn);
    if (session == nullptr) {
        return;
    }
    if (session->device_id) {
        registry_.unbind(*session->device_id, conn.get());
    }
    bump(counters.devices_online, std::int64_t{-1});
    conn->context().reset();
    LG_LOG(Debug) << conn->name() << " device disconnected";
}

void Gateway::on_device_message(const net::TcpConnectionPtr& conn, net::Buffer* buffer) {
    DeviceSession* session = device_session(conn);
    if (session == nullptr || hub_->stopping()) {
        buffer->retrieve_all();
        return;
    }
    Shard& shard = hub_->shard_for(conn->loop());
    ShardCounters& counters = shard.counters();
    bump(counters.bytes_in, static_cast<std::uint64_t>(buffer->readable_bytes()));

    std::shared_ptr<Batch> batch;
    bool saw_valid_frame = false;
    while (true) {
        const devproto::StreamParser::Result result =
            session->parser.parse(buffer->peek(), buffer->readable_bytes());
        if (!result.frame) {
            buffer->retrieve(result.consumed);
            break;
        }
        const devproto::FrameView frame = *result.frame;
        saw_valid_frame = true;
        if (frame.type == devproto::MessageType::Sample) {
            const auto device_id = static_cast<std::int32_t>(devproto::load_be32(frame.payload()));
            if (!session->device_id) {
                session->device_id = device_id;
                if (const net::TcpConnectionPtr previous = registry_.bind(device_id, conn)) {
                    bump(counters.device_id_conflicts);
                    LG_LOG(Warn) << "device " << device_id << " reconnected from "
                                 << conn->peer_address().to_string() << ", closing previous connection "
                                 << previous->name();
                    previous->force_close();
                }
            } else if (*session->device_id != device_id) {
                bump(counters.device_id_mismatches);
                LG_LOG(Warn) << conn->name() << " bound to device " << *session->device_id
                             << " but sent a sample for device " << device_id << ", closing";
                buffer->retrieve_all();
                account_parser(*session, counters);
                conn->force_close();
                return;
            }
            if (!batch) {
                batch = std::make_shared<Batch>();
                batch->bytes.reserve(buffer->readable_bytes());
            }
            batch->bytes.insert(batch->bytes.end(), frame.data, frame.data + frame.size);
            ++batch->samples;
            bump(counters.samples_in);
        } else {
            bump(counters.heartbeats_in);
        }
        buffer->retrieve(result.consumed);
    }
    account_parser(*session, counters);

    if (saw_valid_frame) {
        if (const std::shared_ptr<void> entry = session->idle_entry.lock()) {
            shard.touch(entry);
        }
    }
    if (batch) {
        hub_->broadcast(shard, batch);
    }
}

void Gateway::on_subscriber_connection(const net::TcpConnectionPtr& conn) {
    Shard& shard = hub_->shard_for(conn->loop());
    ShardCounters& counters = shard.counters();
    if (conn->connected()) {
        conn->set_tcp_no_delay(true);
        shard.add_subscriber(conn);
        bump(counters.subscribers_online, std::int64_t{1});
        bump(counters.subscriber_connections);
        LG_LOG(Info) << conn->name() << " subscriber connected from " << conn->peer_address().to_string();
        return;
    }
    if (shard.remove_subscriber(conn)) {
        bump(counters.subscribers_online, std::int64_t{-1});
        LG_LOG(Info) << conn->name() << " subscriber disconnected";
    }
}

void Gateway::on_subscriber_message(const net::TcpConnectionPtr&, net::Buffer* buffer) {
    buffer->retrieve_all();
}

void Gateway::log_stats() {
    const StatsSnapshot now = stats();
    const net::TimePoint time = net::Clock::now();
    const double seconds = std::chrono::duration<double>(time - last_stats_time_).count();
    const double in_rate = seconds > 0 ? static_cast<double>(now.samples_in - last_stats_.samples_in) / seconds : 0;
    const double out_rate =
        seconds > 0 ? static_cast<double>(now.samples_forwarded - last_stats_.samples_forwarded) / seconds : 0;
    LG_LOG(Info) << "stats: samples_in/s=" << static_cast<long long>(in_rate)
                 << " samples_out/s=" << static_cast<long long>(out_rate) << " " << now.to_string();
    last_stats_ = now;
    last_stats_time_ = time;
}

}  // namespace lg::gateway
