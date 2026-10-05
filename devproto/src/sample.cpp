#include "devproto/sample.h"

#include "devproto/byte_order.h"

namespace devproto {

namespace {

constexpr std::size_t kOffsetDeviceId = 0;
constexpr std::size_t kOffsetTimestamp = 4;
constexpr std::size_t kOffsetTemperature = 12;
constexpr std::size_t kOffsetPressure = 20;
constexpr std::size_t kOffsetVibration = 28;

}  // namespace

SamplePayload encode_sample(const Sample& sample) noexcept {
    SamplePayload out{};
    store_be32(out.data() + kOffsetDeviceId, static_cast<std::uint32_t>(sample.device_id));
    store_be64(out.data() + kOffsetTimestamp, static_cast<std::uint64_t>(sample.timestamp_ms));
    store_be_f64(out.data() + kOffsetTemperature, sample.temperature);
    store_be_f64(out.data() + kOffsetPressure, sample.pressure);
    store_be_f64(out.data() + kOffsetVibration, sample.vibration);
    return out;
}

std::optional<Sample> decode_sample(const std::uint8_t* data, std::size_t size) noexcept {
    if (size != kSamplePayloadSize) {
        return std::nullopt;
    }
    Sample sample;
    sample.device_id = static_cast<std::int32_t>(load_be32(data + kOffsetDeviceId));
    sample.timestamp_ms = static_cast<std::int64_t>(load_be64(data + kOffsetTimestamp));
    sample.temperature = load_be_f64(data + kOffsetTemperature);
    sample.pressure = load_be_f64(data + kOffsetPressure);
    sample.vibration = load_be_f64(data + kOffsetVibration);
    return sample;
}

}  // namespace devproto
