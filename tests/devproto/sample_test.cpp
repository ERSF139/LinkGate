#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "devproto/constants.h"
#include "devproto/sample.h"
#include "golden.h"

using devproto::decode_sample;
using devproto::encode_sample;
using devproto::kHeaderSize;
using devproto::kSamplePayloadSize;
using devproto::Sample;

namespace {

std::uint64_t bits_of(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

std::vector<std::uint8_t> golden_payload(const std::string& name) {
    const std::vector<std::uint8_t> frame = lg::test::golden_frame(name);
    const std::uint8_t* begin = frame.data() + kHeaderSize;
    return {begin, begin + kSamplePayloadSize};
}

}  // namespace

TEST(Sample, DecodeGoldenBasic) {
    const auto payload = golden_payload("sample_basic");
    const auto sample = decode_sample(payload.data(), payload.size());
    ASSERT_TRUE(sample.has_value());
    EXPECT_EQ(sample->device_id, 7);
    EXPECT_EQ(sample->timestamp_ms, std::int64_t{1755500000123});
    EXPECT_EQ(bits_of(sample->temperature), bits_of(25.31));
    EXPECT_EQ(bits_of(sample->pressure), bits_of(101.28));
    EXPECT_EQ(bits_of(sample->vibration), bits_of(0.512));
}

TEST(Sample, DecodeGoldenSigns) {
    const auto payload = golden_payload("sample_signs");
    const auto sample = decode_sample(payload.data(), payload.size());
    ASSERT_TRUE(sample.has_value());
    EXPECT_EQ(sample->device_id, -1);
    EXPECT_EQ(sample->timestamp_ms, std::int64_t{0});
    EXPECT_EQ(bits_of(sample->temperature), bits_of(-20.0));
    EXPECT_EQ(bits_of(sample->pressure), bits_of(0.0));
    EXPECT_EQ(bits_of(sample->vibration), bits_of(-0.0));
}

TEST(Sample, DecodeGoldenExtremes) {
    const auto payload = golden_payload("sample_extremes");
    const auto sample = decode_sample(payload.data(), payload.size());
    ASSERT_TRUE(sample.has_value());
    EXPECT_EQ(sample->device_id, std::numeric_limits<std::int32_t>::max());
    EXPECT_EQ(sample->timestamp_ms, std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(bits_of(sample->temperature), std::uint64_t{0x7FF8000000000000});
    EXPECT_EQ(bits_of(sample->pressure), bits_of(std::numeric_limits<double>::infinity()));
    EXPECT_EQ(bits_of(sample->vibration), bits_of(std::numeric_limits<double>::denorm_min()));
}

TEST(Sample, EncodeReproducesGolden) {
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
        const auto encoded = encode_sample(c.sample);
        EXPECT_EQ(std::vector<std::uint8_t>(encoded.begin(), encoded.end()), golden_payload(c.name));
    }
}

TEST(Sample, DecodeRejectsWrongSize) {
    const std::vector<std::uint8_t> bytes(kSamplePayloadSize + 1, 0);
    EXPECT_FALSE(decode_sample(bytes.data(), kSamplePayloadSize - 1).has_value());
    EXPECT_FALSE(decode_sample(bytes.data(), kSamplePayloadSize + 1).has_value());
    EXPECT_FALSE(decode_sample(nullptr, 0).has_value());
}
