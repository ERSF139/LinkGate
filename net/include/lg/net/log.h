#pragma once

#include <sstream>
#include <string>

namespace lg::net {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void set_log_level(LogLevel level) noexcept;
LogLevel log_level() noexcept;
bool parse_log_level(const std::string& text, LogLevel* level) noexcept;

void log_write(LogLevel level, const char* file, int line, const std::string& message);

class LogLine {
public:
    LogLine(LogLevel level, const char* file, int line) : level_(level), file_(file), line_(line) {}
    ~LogLine() { log_write(level_, file_, line_, stream_.str()); }

    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;

    template <class T>
    LogLine& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    const char* file_;
    int line_;
    std::ostringstream stream_;
};

}  // namespace lg::net

#define LG_LOG(level)                                                       \
    if (::lg::net::LogLevel::level < ::lg::net::log_level()) {             \
    } else                                                                  \
        ::lg::net::LogLine(::lg::net::LogLevel::level, __FILE__, __LINE__)
