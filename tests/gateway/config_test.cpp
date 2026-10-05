#include <gtest/gtest.h>

#include <vector>

#include "lg/gateway/config.h"

using lg::gateway::Config;
using lg::gateway::parse_args;

namespace {

lg::gateway::ParseOutcome parse(std::vector<const char*> args) {
    args.insert(args.begin(), "linkgate");
    return parse_args(static_cast<int>(args.size()), args.data());
}

}  // namespace

TEST(Config, DefaultsAreValid) {
    const auto outcome = parse({});
    ASSERT_TRUE(outcome.config.has_value()) << outcome.error;
    EXPECT_EQ(outcome.config->device_port, 9100);
    EXPECT_EQ(outcome.config->subscriber_port, 9000);
}

TEST(Config, ParsesOptions) {
    const auto outcome = parse({"--device-port=19100", "--subscriber-port=19000", "--io-threads=8",
                                "--idle-timeout-ms=5000", "--tick-ms=500", "--high-water=1000",
                                "--low-water=100", "--log-level=debug", "--bind=127.0.0.1"});
    ASSERT_TRUE(outcome.config.has_value()) << outcome.error;
    const Config& c = *outcome.config;
    EXPECT_EQ(c.device_port, 19100);
    EXPECT_EQ(c.subscriber_port, 19000);
    EXPECT_EQ(c.io_threads, 8u);
    EXPECT_EQ(c.device_idle_timeout.count(), 5000);
    EXPECT_EQ(c.high_water_bytes, 1000u);
    EXPECT_EQ(c.log_level, lg::net::LogLevel::Debug);
    EXPECT_EQ(c.bind_ip, "127.0.0.1");
    EXPECT_EQ(c.idle_wheel_slots(), 11u);
}

TEST(Config, RejectsBadInput) {
    EXPECT_FALSE(parse({"--device-port=70000"}).config.has_value());
    EXPECT_FALSE(parse({"--io-threads=abc"}).config.has_value());
    EXPECT_FALSE(parse({"--unknown=1"}).config.has_value());
    EXPECT_FALSE(parse({"positional"}).config.has_value());
    EXPECT_FALSE(parse({"--low-water=10", "--high-water=5"}).config.has_value());
    EXPECT_FALSE(parse({"--log-level=verbose"}).config.has_value());
}

TEST(Config, HelpFlag) { EXPECT_TRUE(parse({"--help"}).help); }
