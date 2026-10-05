#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "devproto/frame.h"
#include "golden.h"

using devproto::MessageType;
using devproto::Sample;

TEST(Frame, HeartbeatMatchesGolden) {
    const auto& frame = devproto::heartbeat_frame();
    EXPECT_EQ(std::vector<std::uint8_t>(frame.begin(), frame.end()), lg::test::golden_frame("heartbeat"));
}

TEST(Frame, SampleFramesMatchGolden) {
    struct Case {
        const char* name;
        Sample sample;
    };
    const Case cases[] = {
        {"sample_basic", {7, 1755500000123, 25.31, 101.28, 0.512}},
        {"sample_signs", {-1, 0, -20.0, 0.0, -0.0}},
        {"sample_extremes",
         {std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int64_t>::max(),
          std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::denorm_min()}},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        const auto frame = devproto::encode_sample_frame(c.sample);
        EXPECT_EQ(std::vector<std::uint8_t>(frame.begin(), frame.end()), lg::test::golden_frame(c.name));
    }
}

TEST(Frame, GenericEncoderLayout) {
    const std::uint8_t payload[3] = {'A', 'B', 'C'};
    const auto frame = devproto::encode_frame(MessageType::Sample, payload, sizeof payload);
    ASSERT_EQ(frame.size(), devproto::frame_size_for_payload(3));
    EXPECT_EQ(frame[0], 0xA5);
    EXPECT_EQ(frame[1], 0x5A);
    EXPECT_EQ(frame[2], 0x01);
    EXPECT_EQ(frame[3], 0x01);
    EXPECT_EQ(frame[4], 0x00);
    EXPECT_EQ(frame[5], 0x03);
    EXPECT_EQ(frame[6], 'A');
    EXPECT_EQ(frame[8], 'C');
}

TEST(Frame, PayloadSizeTable) {
    EXPECT_EQ(devproto::payload_size_for(0x01), std::optional<std::size_t>{36});
    EXPECT_EQ(devproto::payload_size_for(0x02), std::optional<std::size_t>{0});
    EXPECT_FALSE(devproto::payload_size_for(0x00).has_value());
    EXPECT_FALSE(devproto::payload_size_for(0x03).has_value());
    EXPECT_FALSE(devproto::payload_size_for(0xFF).has_value());
}
