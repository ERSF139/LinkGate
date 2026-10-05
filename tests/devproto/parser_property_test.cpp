// 随机性质测试：随机拼接合法帧、噪声和被改坏的帧，再随机切块喂给解析器。
// 期望：所有完好的合法帧按顺序、不多不少地被解析出来，并且 parser_check 里的不变式全部成立。
// 种子固定，失败时可以用同一个种子复现。
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "devproto/frame.h"
#include "parser_check.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes random_sample_frame(std::mt19937_64& rng) {
    devproto::Sample sample;
    sample.device_id = static_cast<std::int32_t>(rng());
    sample.timestamp_ms = static_cast<std::int64_t>(rng());
    const std::uint64_t bits[3] = {rng(), rng(), rng()};
    double values[3];
    std::memcpy(values, bits, sizeof values);
    sample.temperature = values[0];
    sample.pressure = values[1];
    sample.vibration = values[2];
    const auto frame = devproto::encode_sample_frame(sample);
    return Bytes(frame.begin(), frame.end());
}

struct Stream {
    Bytes bytes;
    std::vector<Bytes> intact_frames;
};

Stream build_stream(std::mt19937_64& rng, int segments) {
    Stream stream;
    std::uniform_int_distribution<int> kind(0, 9);
    std::uniform_int_distribution<int> noise_length(1, 64);
    for (int i = 0; i < segments; ++i) {
        const int k = kind(rng);
        if (k <= 4) {
            Bytes frame = random_sample_frame(rng);
            stream.intact_frames.push_back(frame);
            stream.bytes.insert(stream.bytes.end(), frame.begin(), frame.end());
        } else if (k == 5) {
            const auto& hb = devproto::heartbeat_frame();
            stream.intact_frames.emplace_back(hb.begin(), hb.end());
            stream.bytes.insert(stream.bytes.end(), hb.begin(), hb.end());
        } else if (k <= 7) {
            const int n = noise_length(rng);
            for (int j = 0; j < n; ++j) {
                // 刻意提高同步字节出现的概率，制造更多假帧头。
                const auto r = static_cast<std::uint8_t>(rng());
                stream.bytes.push_back(r < 32 ? devproto::kSync0 : (r < 64 ? devproto::kSync1 : r));
            }
        } else {
            Bytes frame = random_sample_frame(rng);
            const std::size_t bit = static_cast<std::size_t>(rng() % (frame.size() * 8));
            frame[bit / 8] = static_cast<std::uint8_t>(frame[bit / 8] ^ (1u << (bit % 8)));
            stream.bytes.insert(stream.bytes.end(), frame.begin(), frame.end());
        }
    }
    return stream;
}

}  // namespace

TEST(StreamParserProperty, RandomStreamsRecoverEveryIntactFrame) {
    for (std::uint64_t seed = 1; seed <= 400; ++seed) {
        std::mt19937_64 rng(seed);
        const Stream stream = build_stream(rng, 60);
        const std::size_t chunk = static_cast<std::size_t>(rng() % 64) + 1;

        lg::test::ParseRun run;
        const char* reason = "";
        ASSERT_TRUE(lg::test::run_parser_checked(stream.bytes.data(), stream.bytes.size(), chunk, &run, &reason))
            << "seed=" << seed << ": " << reason;
        ASSERT_EQ(run.frames, stream.intact_frames) << "seed=" << seed << " chunk=" << chunk;
    }
}

TEST(StreamParserProperty, PureNoiseNeverProducesInvalidFrames) {
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        std::mt19937_64 rng(seed * 7919);
        Bytes noise(4096);
        for (auto& b : noise) {
            const auto r = static_cast<std::uint8_t>(rng());
            b = r < 48 ? devproto::kSync0 : (r < 96 ? devproto::kSync1 : r);
        }
        lg::test::ParseRun run;
        const char* reason = "";
        ASSERT_TRUE(lg::test::run_parser_checked(noise.data(), noise.size(), (seed % 13) + 1, &run, &reason))
            << "seed=" << seed << ": " << reason;
    }
}
