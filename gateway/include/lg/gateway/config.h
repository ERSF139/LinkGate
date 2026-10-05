#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "lg/net/log.h"

namespace lg::gateway {

struct Config {
    std::string bind_ip = "0.0.0.0";
    std::uint16_t device_port = 9100;
    std::uint16_t subscriber_port = 9000;
    std::size_t io_threads = 4;

    // 时间轮的粒度。设备在 device_idle_timeout 内没有发来任何合法帧就会被断开。
    std::chrono::milliseconds tick_interval{1000};
    std::chrono::milliseconds device_idle_timeout{10000};

    // 网关给订阅者发心跳的间隔（DeviceLink 客户端的看门狗是 3 秒）。
    std::chrono::milliseconds heartbeat_interval{1000};

    // 订阅者背压：输出缓冲超过高水位就暂停给它发数据（丢弃并计数），
    // 回落到低水位以下再恢复；连续暂停超过 stall_timeout 就断开（见 decisions.md D-003）。
    std::size_t high_water_bytes = 4 * 1024 * 1024;
    std::size_t low_water_bytes = 1024 * 1024;
    std::chrono::milliseconds stall_timeout{10000};

    // 0 表示不输出周期统计。
    std::chrono::milliseconds stats_interval{10000};

    net::LogLevel log_level = net::LogLevel::Info;

    std::size_t idle_wheel_slots() const noexcept;
};

struct ParseOutcome {
    std::optional<Config> config;
    std::string error;
    bool help = false;
};

ParseOutcome parse_args(int argc, const char* const* argv);
std::string usage(const char* program);
std::string validate(const Config& config);

}  // namespace lg::gateway
