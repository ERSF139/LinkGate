#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "devproto/frame.h"
#include "devproto/parser.h"
#include "golden.h"
#include "parser_check.h"

using devproto::MessageType;
using devproto::StreamParser;
using Bytes = std::vector<std::uint8_t>;

namespace {

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

Bytes heartbeat() { return lg::test::golden_frame("heartbeat"); }

Bytes noise(const std::string& text) { return Bytes(text.begin(), text.end()); }

// 反复调用 parse，按 consumed 丢弃，直到没有帧为止。
std::vector<Bytes> parse_all(StreamParser& parser, Bytes& buffer) {
    std::vector<Bytes> frames;
    while (true) {
        const auto result = parser.parse(buffer.data(), buffer.size());
        if (result.frame) {
            frames.emplace_back(result.frame->data, result.frame->data + result.frame->size);
        }
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(result.consumed));
        if (!result.frame) {
            return frames;
        }
    }
}

}  // namespace

TEST(StreamParser, SingleFrame) {
    StreamParser parser;
    const Bytes frame = heartbeat();
    const auto result = parser.parse(frame.data(), frame.size());
    ASSERT_TRUE(result.frame.has_value());
    EXPECT_EQ(result.frame->type, MessageType::Heartbeat);
    EXPECT_EQ(result.frame->size, frame.size());
    EXPECT_EQ(result.frame->data, frame.data());
    EXPECT_EQ(result.consumed, frame.size());
    EXPECT_EQ(parser.stats().frames, 1u);
    EXPECT_EQ(parser.stats().bad_frames, 0u);
    EXPECT_EQ(parser.stats().dropped_bytes, 0u);
}

TEST(StreamParser, StickyFramesComeOutOneByOne) {
    StreamParser parser;
    Bytes buffer = concat({lg::test::golden_frame("sample_basic"), heartbeat(),
                           lg::test::golden_frame("sample_signs")});
    const auto frames = parse_all(parser, buffer);
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(frames[0], lg::test::golden_frame("sample_basic"));
    EXPECT_EQ(frames[1], heartbeat());
    EXPECT_EQ(frames[2], lg::test::golden_frame("sample_signs"));
    EXPECT_TRUE(buffer.empty());
}

TEST(StreamParser, ByteByByteFeedRecoversEveryFrame) {
    const Bytes stream = concat({lg::test::golden_frame("sample_basic"), heartbeat(),
                                 lg::test::golden_frame("sample_extremes")});
    lg::test::ParseRun run;
    const char* reason = "";
    ASSERT_TRUE(lg::test::run_parser_checked(stream.data(), stream.size(), 1, &run, &reason)) << reason;
    ASSERT_EQ(run.frames.size(), 3u);
    EXPECT_EQ(run.stats.dropped_bytes, 0u);
}

TEST(StreamParser, LeadingNoiseIsDroppedWithoutCountingBadFrames) {
    StreamParser parser;
    const Bytes garbage = noise("garbage before the real frame");
    Bytes buffer = concat({garbage, heartbeat()});
    const auto frames = parse_all(parser, buffer);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(parser.stats().dropped_bytes, garbage.size());
    EXPECT_EQ(parser.stats().bad_frames, 0u);
}

TEST(StreamParser, IncompleteHeaderWaitsWithoutJudging) {
    StreamParser parser;
    const Bytes partial{0xA5, 0x5A, 0x99};
    const auto result = parser.parse(partial.data(), partial.size());
    EXPECT_FALSE(result.frame.has_value());
    EXPECT_EQ(result.consumed, 0u);
    EXPECT_EQ(parser.stats().bad_frames, 0u);
    EXPECT_EQ(parser.stats().dropped_bytes, 0u);
}

TEST(StreamParser, TrailingSyncByteIsKept) {
    StreamParser parser;
    const Bytes input{'x', 'y', 0xA5};
    const auto result = parser.parse(input.data(), input.size());
    EXPECT_FALSE(result.frame.has_value());
    EXPECT_EQ(result.consumed, 2u);
    EXPECT_EQ(parser.stats().dropped_bytes, 2u);
}

TEST(StreamParser, WrongVersionIsRejectedAtTheHeader) {
    StreamParser parser;
    Bytes buffer = concat({Bytes{0xA5, 0x5A, 0x99, 0x02, 0x00, 0x00}, heartbeat()});
    const auto frames = parse_all(parser, buffer);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(parser.stats().bad_frames, 1u);
    EXPECT_EQ(parser.stats().dropped_bytes, 6u);
}

TEST(StreamParser, UnknownTypeIsRejectedEvenWithValidCrc) {
    StreamParser parser;
    const Bytes unknown = devproto::encode_frame(static_cast<MessageType>(0x03), nullptr, 0);
    Bytes buffer = concat({unknown, heartbeat()});
    const auto frames = parse_all(parser, buffer);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], heartbeat());
    EXPECT_EQ(parser.stats().bad_frames, 1u);
}

// protocol.md 第 8 节的例子：Sample 的长度字段写成 4000。DeviceLink 要等 4008 字节，
// devproto 读完 6 字节帧头就拒绝，紧跟其后的合法帧立刻就能解析出来。
TEST(StreamParser, LengthNotMatchingTypeIsRejectedWithoutWaiting) {
    StreamParser parser;
    Bytes buffer = concat({Bytes{0xA5, 0x5A, 0x01, 0x01, 0x0F, 0xA0}, heartbeat()});
    ASSERT_LT(buffer.size(), 4008u);
    const auto frames = parse_all(parser, buffer);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], heartbeat());
    EXPECT_EQ(parser.stats().bad_frames, 1u);
}

TEST(StreamParser, OversizedLengthIsRejected) {
    StreamParser parser;
    Bytes buffer = concat({Bytes{0xA5, 0x5A, 0x01, 0x01, 0xFF, 0xFF}, Bytes(10, 0), heartbeat()});
    const auto frames = parse_all(parser, buffer);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(parser.stats().bad_frames, 1u);
}

TEST(StreamParser, CrcMismatchResyncsToTheNextFrame) {
    StreamParser parser;
    Bytes bad = lg::test::golden_frame("sample_basic");
    bad[devproto::kHeaderSize + 3] ^= 0xFF;
    Bytes buffer = concat({bad, heartbeat()});
    const auto frames = parse_all(parser, buffer);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], heartbeat());
    EXPECT_EQ(parser.stats().bad_frames, 1u);
    EXPECT_EQ(parser.stats().dropped_bytes, bad.size());
}

// CRC 失败后只滑动 1 字节而不是跳过整帧：坏帧载荷里如果藏着一帧完整的合法帧，它仍然会被找到。
TEST(StreamParser, ResyncSlidesOneByteAndFindsFrameInsideBadFrame) {
    std::uint8_t payload[devproto::kSamplePayloadSize] = {};
    const Bytes inner = heartbeat();
    std::memcpy(payload, inner.data(), inner.size());
    Bytes outer = devproto::encode_frame(MessageType::Sample, payload, sizeof payload);
    outer.back() ^= 0x01;

    StreamParser parser;
    Bytes buffer = outer;
    const auto frames = parse_all(parser, buffer);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], inner);
    EXPECT_EQ(parser.stats().bad_frames, 1u);
}

TEST(StreamParser, EmptyInput) {
    StreamParser parser;
    const auto result = parser.parse(nullptr, 0);
    EXPECT_FALSE(result.frame.has_value());
    EXPECT_EQ(result.consumed, 0u);
}
