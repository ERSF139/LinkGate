// LinkGate 压测工具：模拟大量设备向网关的设备端口发 Sample，同时作为订阅者接收转发结果，统计吞吐与端到端延迟。
//
// 延迟测量：设备在 Sample 的 vibration 字段里写入发送时刻（steady_clock 纳秒，double 可精确表示），
// 订阅者收到后用当前时刻相减。设备和订阅者在同一进程里，时钟一致。这只是压测约定，正常设备不会这样用这个字段。
#include <signal.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "devproto/byte_order.h"
#include "devproto/frame.h"
#include "devproto/parser.h"
#include "lg/net/buffer.h"
#include "lg/net/event_loop.h"
#include "lg/net/event_loop_thread.h"
#include "lg/net/log.h"
#include "lg/net/socket_ops.h"
#include "lg/net/tcp_client.h"
#include "lg/net/tcp_connection.h"

namespace {

using lg::net::Clock;
using lg::net::EventLoop;
using lg::net::TcpConnectionPtr;
using lg::net::TimePoint;

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t device_port = 9100;
    std::uint16_t subscriber_port = 9000;
    std::size_t devices = 1000;
    double rate = 1.0;
    std::size_t subscribers = 1;
    double duration_s = 30.0;
    double warmup_s = 2.0;
    std::size_t threads = 2;
    std::size_t ramp = 1000;
    std::int32_t first_device_id = 1;
};

template <class T>
bool parse_number(std::string_view text, T* out) {
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    *out = value;
    return true;
}

bool parse_double(std::string_view text, double* out) {
    try {
        std::size_t used = 0;
        const double value = std::stod(std::string(text), &used);
        if (used != text.size()) return false;
        *out = value;
        return true;
    } catch (...) {
        return false;
    }
}

void print_usage(const char* program) {
    const Options d;
    std::cout << "Usage: " << program << " [--key=value ...]\n"
              << "  --host=IP               gateway address (default " << d.host << ")\n"
              << "  --device-port=N         (default " << d.device_port << ")\n"
              << "  --subscriber-port=N     (default " << d.subscriber_port << ")\n"
              << "  --devices=N             simulated devices, one connection each (default " << d.devices << ")\n"
              << "  --rate=HZ               samples per second per device (default " << d.rate << ")\n"
              << "  --subscribers=N         subscriber connections (default " << d.subscribers << ")\n"
              << "  --duration=S            measured seconds after ramp-up + warmup (default " << d.duration_s
              << ")\n"
              << "  --warmup=S              seconds excluded from latency stats (default " << d.warmup_s << ")\n"
              << "  --threads=N             IO threads (default " << d.threads << ")\n"
              << "  --ramp=N                new device connections per second (default " << d.ramp << ")\n"
              << "  --first-device-id=N     id of the first simulated device (default " << d.first_device_id
              << ")\n";
}

bool parse_options(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        const auto eq = arg.find('=');
        if (arg.substr(0, 2) != "--" || eq == std::string_view::npos) {
            std::cerr << "expected --key=value, got " << arg << '\n';
            return false;
        }
        const std::string_view key = arg.substr(2, eq - 2);
        const std::string_view value = arg.substr(eq + 1);
        bool ok = true;
        unsigned long port = 0;
        if (key == "host") {
            options->host = std::string(value);
        } else if (key == "device-port" || key == "subscriber-port") {
            ok = parse_number(value, &port) && port <= 65535;
            (key == "device-port" ? options->device_port : options->subscriber_port) =
                static_cast<std::uint16_t>(port);
        } else if (key == "devices") {
            ok = parse_number(value, &options->devices);
        } else if (key == "rate") {
            ok = parse_double(value, &options->rate) && options->rate > 0;
        } else if (key == "subscribers") {
            ok = parse_number(value, &options->subscribers);
        } else if (key == "duration") {
            ok = parse_double(value, &options->duration_s) && options->duration_s > 0;
        } else if (key == "warmup") {
            ok = parse_double(value, &options->warmup_s) && options->warmup_s >= 0;
        } else if (key == "threads") {
            ok = parse_number(value, &options->threads);
        } else if (key == "ramp") {
            ok = parse_number(value, &options->ramp) && options->ramp > 0;
        } else if (key == "first-device-id") {
            ok = parse_number(value, &options->first_device_id);
        } else {
            std::cerr << "unknown option --" << key << '\n';
            return false;
        }
        if (!ok) {
            std::cerr << "invalid value for --" << key << ": " << value << '\n';
            return false;
        }
    }
    return true;
}

double steady_ns(TimePoint t) {
    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
}

std::int64_t wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct Device {
    std::int32_t id = 0;
    std::unique_ptr<lg::net::TcpClient> client;
    TcpConnectionPtr conn;
    TimePoint next_send{};
};

// 一个 IO 线程上的全部设备。除了创建时的 loop 指针，所有字段只在这个线程里访问。
struct Worker {
    EventLoop* loop = nullptr;
    std::vector<Device> devices;
    lg::net::TimerId send_timer;
    std::uint64_t sent = 0;
    std::atomic<std::size_t> connected{0};
};

struct Subscriber {
    std::unique_ptr<lg::net::TcpClient> client;
    devproto::StreamParser parser;
    std::vector<std::uint32_t> latencies_us;
    std::uint64_t samples = 0;
    std::uint64_t heartbeats = 0;
    bool measuring = false;
    std::atomic<bool> connected{false};
};

constexpr std::size_t kMaxLatencySamples = 20'000'000;

void on_subscriber_message(Subscriber* sub, lg::net::Buffer* buffer) {
    const double now = steady_ns(Clock::now());
    while (true) {
        const auto result = sub->parser.parse(buffer->peek(), buffer->readable_bytes());
        if (!result.frame) {
            buffer->retrieve(result.consumed);
            return;
        }
        const devproto::FrameView frame = *result.frame;
        if (frame.type == devproto::MessageType::Sample) {
            ++sub->samples;
            if (sub->measuring && sub->latencies_us.size() < kMaxLatencySamples) {
                const double sent_ns = devproto::load_be_f64(frame.payload() + 28);
                const double latency_us = (now - sent_ns) / 1000.0;
                sub->latencies_us.push_back(latency_us > 0 ? static_cast<std::uint32_t>(latency_us) : 0U);
            }
        } else {
            ++sub->heartbeats;
        }
        buffer->retrieve(result.consumed);
    }
}

void send_due_samples(Worker* worker, std::chrono::nanoseconds period, const std::atomic<bool>* sending) {
    if (!sending->load(std::memory_order_acquire)) {
        return;
    }
    const TimePoint now = Clock::now();
    for (Device& device : worker->devices) {
        if (!device.conn || now < device.next_send) {
            continue;
        }
        devproto::Sample sample;
        sample.device_id = device.id;
        sample.timestamp_ms = wall_ms();
        sample.temperature = 25.0;
        sample.pressure = 101.3;
        sample.vibration = steady_ns(Clock::now());
        const auto frame = devproto::encode_sample_frame(sample);
        device.conn->send(frame.data(), frame.size());
        ++worker->sent;
        device.next_send += period;
        if (device.next_send < now) {
            device.next_send = now + period;
        }
    }
}

std::uint32_t percentile(const std::vector<std::uint32_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    const auto rank = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[std::min(rank, sorted.size() - 1)];
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    lg::net::set_log_level(lg::net::LogLevel::Warn);
    ::signal(SIGPIPE, SIG_IGN);
    const std::size_t fd_limit = lg::net::sockets::raise_fd_limit();
    if (fd_limit < options.devices + options.subscribers + 64) {
        std::cerr << "warning: fd limit " << fd_limit << " is too low for " << options.devices
                  << " devices; run `ulimit -n 65535` first\n";
    }

    const auto device_addr = lg::net::InetAddress::from_ip_port(options.host, options.device_port);
    const auto subscriber_addr = lg::net::InetAddress::from_ip_port(options.host, options.subscriber_port);
    if (!device_addr || !subscriber_addr) {
        std::cerr << "invalid --host " << options.host << '\n';
        return 2;
    }

    EventLoop base;
    // 用 unique_ptr 持有，好在 workers / subscribers 析构之前先停掉 IO 线程：
    // 线程池退出前执行的排队任务还会回调到它们。
    auto pool = std::make_unique<lg::net::EventLoopThreadPool>(&base, std::max<std::size_t>(options.threads, 1),
                                                               "loadgen");
    pool->start();

    std::vector<std::unique_ptr<Worker>> workers;
    for (EventLoop* loop : pool->loops()) {
        auto worker = std::make_unique<Worker>();
        worker->loop = loop;
        workers.push_back(std::move(worker));
    }

    std::vector<std::unique_ptr<Subscriber>> subscribers;
    for (std::size_t i = 0; i < options.subscribers; ++i) {
        auto sub = std::make_unique<Subscriber>();
        Subscriber* raw = sub.get();
        EventLoop* loop = pool->loops()[i % pool->loops().size()];
        sub->client = std::make_unique<lg::net::TcpClient>(loop, *subscriber_addr, "sub" + std::to_string(i));
        sub->client->set_connection_callback(
            [raw](const TcpConnectionPtr& conn) { raw->connected.store(conn->connected()); });
        sub->client->set_message_callback(
            [raw](const TcpConnectionPtr&, lg::net::Buffer* buffer) { on_subscriber_message(raw, buffer); });
        sub->client->enable_retry(true);
        sub->client->connect();
        subscribers.push_back(std::move(sub));
    }

    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(1.0 / options.rate));
    std::atomic<bool> sending{true};

    for (auto& worker : workers) {
        Worker* w = worker.get();
        w->loop->run_in_loop_sync([w, period, &sending] {
            w->send_timer = w->loop->run_every(std::chrono::milliseconds(5),
                                               [w, period, &sending] { send_due_samples(w, period, &sending); });
        });
    }

    // 阶段一：等订阅者连上。阶段二：按 ramp 速率逐批建立设备连接。
    const TimePoint start = Clock::now();
    std::size_t created = 0;
    TimePoint ramp_done{};
    TimePoint measure_start{};
    TimePoint measure_end{};
    bool subscribers_ready = options.subscribers == 0;
    const std::size_t batch_per_tick = std::max<std::size_t>(options.ramp / 100, 1);

    auto set_measuring = [&subscribers](bool on) {
        for (auto& sub : subscribers) {
            Subscriber* s = sub.get();
            s->client->loop()->run_in_loop_sync([s, on] { s->measuring = on; });
        }
    };

    lg::net::TimerId ramp_timer;
    ramp_timer = base.run_every(std::chrono::milliseconds(10), [&] {
        const TimePoint now = Clock::now();
        if (!subscribers_ready) {
            subscribers_ready = std::all_of(subscribers.begin(), subscribers.end(),
                                            [](const auto& s) { return s->connected.load(); });
            if (!subscribers_ready && now - start > std::chrono::seconds(10)) {
                std::cerr << "subscribers could not connect to " << subscriber_addr->to_string() << '\n';
                base.quit();
            }
            return;
        }
        if (created < options.devices) {
            const std::size_t end = std::min(options.devices, created + batch_per_tick);
            for (; created < end; ++created) {
                Worker* w = workers[created % workers.size()].get();
                const auto id = static_cast<std::int32_t>(options.first_device_id + static_cast<std::int32_t>(created));
                const std::string name = "dev" + std::to_string(id);
                w->loop->run_in_loop([w, id, name, addr = *device_addr, period] {
                    const std::size_t index = w->devices.size();
                    Device device;
                    device.id = id;
                    device.client = std::make_unique<lg::net::TcpClient>(w->loop, addr, name);
                    device.client->set_connection_callback([w, index, period](const TcpConnectionPtr& conn) {
                        // 清理阶段 devices 已被清空，但已排队的关闭事件仍会回调到这里。
                        if (index >= w->devices.size()) {
                            return;
                        }
                        Device& d = w->devices[index];
                        if (conn->connected()) {
                            conn->set_tcp_no_delay(true);
                            d.conn = conn;
                            const auto offset = period * static_cast<long>(index % 97) / 97;
                            d.next_send = Clock::now() + offset;
                            w->connected.fetch_add(1);
                        } else if (d.conn == conn) {
                            d.conn.reset();
                            w->connected.fetch_sub(1);
                        }
                    });
                    device.client->set_message_callback(
                        [](const TcpConnectionPtr&, lg::net::Buffer* buffer) { buffer->retrieve_all(); });
                    device.client->enable_retry(true);
                    w->devices.push_back(std::move(device));
                    w->devices.back().client->connect();
                });
            }
            if (created == options.devices) {
                ramp_done = now;
            }
            return;
        }
        if (measure_start == TimePoint{} && now - ramp_done >= std::chrono::duration<double>(options.warmup_s)) {
            measure_start = now;
            set_measuring(true);
            std::size_t connected = 0;
            for (auto& w : workers) connected += w->connected.load();
            std::cout << "ramp-up done: " << connected << "/" << options.devices
                      << " devices connected, measuring for " << options.duration_s << " s" << std::endl;
            return;
        }
        if (measure_start != TimePoint{} && measure_end == TimePoint{} &&
            now - measure_start >= std::chrono::duration<double>(options.duration_s)) {
            measure_end = now;
            set_measuring(false);
            sending.store(false);
            base.cancel(ramp_timer);
            base.run_after(std::chrono::seconds(1), [&base] { base.quit(); });
        }
    });

    base.loop();

    // 统计（在各自线程里读取各自的数据）。
    std::size_t connected = 0;
    std::uint64_t sent = 0;
    for (auto& worker : workers) {
        Worker* w = worker.get();
        w->loop->run_in_loop_sync([w, &sent] { sent += w->sent; });
        connected += w->connected.load();
    }
    const double window = std::chrono::duration<double>(measure_end - measure_start).count();
    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();

    std::cout << "\n=== LinkGate loadgen report ===\n"
              << "devices: " << connected << "/" << options.devices << " connected, rate " << options.rate
              << " Hz each, " << pool->loops().size() << " loadgen thread(s)\n"
              << "samples sent: " << sent << " in " << elapsed << " s total\n";
    for (std::size_t i = 0; i < subscribers.size(); ++i) {
        Subscriber* s = subscribers[i].get();
        std::vector<std::uint32_t> latencies;
        std::uint64_t samples = 0;
        std::uint64_t heartbeats = 0;
        s->client->loop()->run_in_loop_sync([s, &latencies, &samples, &heartbeats] {
            latencies.swap(s->latencies_us);
            samples = s->samples;
            heartbeats = s->heartbeats;
        });
        std::sort(latencies.begin(), latencies.end());
        const double loss = sent > 0 ? 100.0 * (1.0 - static_cast<double>(samples) / static_cast<double>(sent)) : 0;
        std::cout << "subscriber " << i << ": received " << samples << " samples, " << heartbeats
                  << " heartbeats, loss " << loss << "%\n";
        if (window > 0) {
            std::cout << "  measured window " << window << " s: "
                      << static_cast<double>(latencies.size()) / window << " samples/s\n";
        }
        std::cout << "  latency us: p50=" << percentile(latencies, 0.50) << " p90=" << percentile(latencies, 0.90)
                  << " p99=" << percentile(latencies, 0.99) << " p999=" << percentile(latencies, 0.999)
                  << " max=" << (latencies.empty() ? 0 : latencies.back()) << " (n=" << latencies.size() << ")\n";
    }

    // 清理：客户端必须在各自 loop 线程里、在线程池停止之前销毁。
    for (auto& worker : workers) {
        Worker* w = worker.get();
        w->loop->run_in_loop_sync([w] {
            w->loop->cancel(w->send_timer);
            w->devices.clear();
        });
    }
    for (auto& sub : subscribers) {
        Subscriber* s = sub.get();
        s->client->loop()->run_in_loop_sync([s] { s->client.reset(); });
    }
    pool.reset();
    return 0;
}
