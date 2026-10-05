#include "lg/gateway/config.h"

#include <charconv>
#include <limits>
#include <sstream>
#include <string_view>

namespace lg::gateway {

namespace {

template <class T>
bool parse_number(std::string_view text, T* out) {
    T value{};
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    *out = value;
    return true;
}

bool parse_port(std::string_view text, std::uint16_t* out) {
    unsigned long value = 0;
    if (!parse_number(text, &value) || value > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    *out = static_cast<std::uint16_t>(value);
    return true;
}

bool parse_millis(std::string_view text, std::chrono::milliseconds* out) {
    long long value = 0;
    if (!parse_number(text, &value) || value < 0) {
        return false;
    }
    *out = std::chrono::milliseconds(value);
    return true;
}

}  // namespace

std::size_t Config::idle_wheel_slots() const noexcept {
    const auto tick = tick_interval.count() > 0 ? tick_interval.count() : 1;
    const auto ticks = (device_idle_timeout.count() + tick - 1) / tick;
    return static_cast<std::size_t>(ticks) + 1;
}

std::string validate(const Config& config) {
    if (config.tick_interval.count() <= 0) return "--tick-ms must be positive";
    if (config.heartbeat_interval.count() <= 0) return "--heartbeat-ms must be positive";
    if (config.device_idle_timeout < config.tick_interval) return "--idle-timeout-ms must be >= --tick-ms";
    if (config.low_water_bytes >= config.high_water_bytes) return "--low-water must be below --high-water";
    if (config.stall_timeout.count() <= 0) return "--stall-timeout-ms must be positive";
    if (config.io_threads > 256) return "--io-threads must be <= 256";
    return {};
}

ParseOutcome parse_args(int argc, const char* const* argv) {
    ParseOutcome outcome;
    Config config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            outcome.help = true;
            return outcome;
        }
        const auto eq = arg.find('=');
        if (arg.substr(0, 2) != "--" || eq == std::string_view::npos) {
            outcome.error = "expected --key=value, got '" + std::string(arg) + "'";
            return outcome;
        }
        const std::string_view key = arg.substr(2, eq - 2);
        const std::string_view value = arg.substr(eq + 1);
        bool ok = true;
        if (key == "bind") {
            config.bind_ip = std::string(value);
        } else if (key == "device-port") {
            ok = parse_port(value, &config.device_port);
        } else if (key == "subscriber-port") {
            ok = parse_port(value, &config.subscriber_port);
        } else if (key == "io-threads") {
            ok = parse_number(value, &config.io_threads);
        } else if (key == "tick-ms") {
            ok = parse_millis(value, &config.tick_interval);
        } else if (key == "idle-timeout-ms") {
            ok = parse_millis(value, &config.device_idle_timeout);
        } else if (key == "heartbeat-ms") {
            ok = parse_millis(value, &config.heartbeat_interval);
        } else if (key == "high-water") {
            ok = parse_number(value, &config.high_water_bytes);
        } else if (key == "low-water") {
            ok = parse_number(value, &config.low_water_bytes);
        } else if (key == "stall-timeout-ms") {
            ok = parse_millis(value, &config.stall_timeout);
        } else if (key == "stats-interval-ms") {
            ok = parse_millis(value, &config.stats_interval);
        } else if (key == "log-level") {
            ok = net::parse_log_level(std::string(value), &config.log_level);
        } else {
            outcome.error = "unknown option --" + std::string(key);
            return outcome;
        }
        if (!ok) {
            outcome.error = "invalid value for --" + std::string(key) + ": '" + std::string(value) + "'";
            return outcome;
        }
    }
    outcome.error = validate(config);
    if (outcome.error.empty()) {
        outcome.config = config;
    }
    return outcome;
}

std::string usage(const char* program) {
    const Config d;
    std::ostringstream out;
    out << "Usage: " << program << " [--key=value ...]\n"
        << "  --bind=IP                 listen address (default " << d.bind_ip << ")\n"
        << "  --device-port=N           port for devices (default " << d.device_port << ")\n"
        << "  --subscriber-port=N       port for DeviceLink clients (default " << d.subscriber_port << ")\n"
        << "  --io-threads=N            IO threads, 0 = single thread (default " << d.io_threads << ")\n"
        << "  --idle-timeout-ms=N       close devices silent for this long (default "
        << d.device_idle_timeout.count() << ")\n"
        << "  --tick-ms=N               idle timing wheel granularity (default " << d.tick_interval.count()
        << ")\n"
        << "  --heartbeat-ms=N          heartbeat interval to subscribers (default "
        << d.heartbeat_interval.count() << ")\n"
        << "  --high-water=BYTES        pause a subscriber above this backlog (default " << d.high_water_bytes
        << ")\n"
        << "  --low-water=BYTES         resume a subscriber below this backlog (default " << d.low_water_bytes
        << ")\n"
        << "  --stall-timeout-ms=N      disconnect a subscriber paused this long (default "
        << d.stall_timeout.count() << ")\n"
        << "  --stats-interval-ms=N     periodic stats log, 0 = off (default " << d.stats_interval.count()
        << ")\n"
        << "  --log-level=LEVEL         debug | info | warn | error (default info)\n";
    return out.str();
}

}  // namespace lg::gateway
