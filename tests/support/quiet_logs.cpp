#include <cstdlib>

#include "lg/net/log.h"

namespace {

// 测试默认只输出警告以上的日志；排查问题时可以设置 LG_TEST_LOG=debug。
struct QuietLogs {
    QuietLogs() {
        lg::net::LogLevel level = lg::net::LogLevel::Warn;
        if (const char* env = std::getenv("LG_TEST_LOG")) {
            lg::net::parse_log_level(env, &level);
        }
        lg::net::set_log_level(level);
    }
};

const QuietLogs quiet_logs;

}  // namespace
