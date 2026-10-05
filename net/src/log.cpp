#include "lg/net/log.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace lg::net {

namespace {

std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_write_mutex;

const char* level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

long current_tid() noexcept {
    thread_local const long tid = ::syscall(SYS_gettid);
    return tid;
}

const char* base_name(const char* path) noexcept {
    const char* slash = std::strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

}  // namespace

void set_log_level(LogLevel level) noexcept { g_level.store(level, std::memory_order_relaxed); }

LogLevel log_level() noexcept { return g_level.load(std::memory_order_relaxed); }

bool parse_log_level(const std::string& text, LogLevel* level) noexcept {
    if (text == "debug") {
        *level = LogLevel::Debug;
    } else if (text == "info") {
        *level = LogLevel::Info;
    } else if (text == "warn") {
        *level = LogLevel::Warn;
    } else if (text == "error") {
        *level = LogLevel::Error;
    } else {
        return false;
    }
    return true;
}

void log_write(LogLevel level, const char* file, int line, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
    ::localtime_r(&seconds, &local);

    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local);

    std::lock_guard<std::mutex> lock(g_write_mutex);
    std::fprintf(stderr, "%s.%03lld %s %ld %s:%d %s\n", stamp, static_cast<long long>(millis),
                 level_name(level), current_tid(), base_name(file), line, message.c_str());
}

}  // namespace lg::net
