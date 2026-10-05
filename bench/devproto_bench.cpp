#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

#include "devproto/crc16.h"
#include "devproto/frame.h"
#include "devproto/parser.h"

namespace {

std::vector<std::uint8_t> make_stream(std::size_t frames) {
    std::vector<std::uint8_t> stream;
    stream.reserve(frames * 44);
    for (std::size_t i = 0; i < frames; ++i) {
        devproto::Sample sample;
        sample.device_id = static_cast<std::int32_t>(i);
        sample.timestamp_ms = static_cast<std::int64_t>(i);
        sample.temperature = 25.0;
        const auto frame = devproto::encode_sample_frame(sample);
        stream.insert(stream.end(), frame.begin(), frame.end());
    }
    return stream;
}

void BM_Crc16(benchmark::State& state) {
    const std::vector<std::uint8_t> data(static_cast<std::size_t>(state.range(0)), 0x5A);
    for (auto _ : state) {
        benchmark::DoNotOptimize(devproto::crc16_modbus(data.data(), data.size()));
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * state.range(0));
}
BENCHMARK(BM_Crc16)->Arg(42)->Arg(4096);

void BM_EncodeSampleFrame(benchmark::State& state) {
    devproto::Sample sample;
    sample.device_id = 7;
    for (auto _ : state) {
        auto frame = devproto::encode_sample_frame(sample);
        benchmark::DoNotOptimize(frame);
    }
}
BENCHMARK(BM_EncodeSampleFrame);

void BM_ParseSampleStream(benchmark::State& state) {
    const auto stream = make_stream(1000);
    for (auto _ : state) {
        devproto::StreamParser parser;
        std::size_t offset = 0;
        std::size_t frames = 0;
        while (true) {
            const auto result = parser.parse(stream.data() + offset, stream.size() - offset);
            offset += result.consumed;
            if (!result.frame) break;
            ++frames;
        }
        benchmark::DoNotOptimize(frames);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 1000);
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(stream.size()));
}
BENCHMARK(BM_ParseSampleStream);

}  // namespace

BENCHMARK_MAIN();
