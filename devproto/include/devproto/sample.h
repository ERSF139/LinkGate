#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "devproto/constants.h"

namespace devproto {

struct Sample {
    std::int32_t device_id = 0;
    std::int64_t timestamp_ms = 0;
    double temperature = 0.0;
    double pressure = 0.0;
    double vibration = 0.0;
};

using SamplePayload = std::array<std::uint8_t, kSamplePayloadSize>;

SamplePayload encode_sample(const Sample& sample) noexcept;
std::optional<Sample> decode_sample(const std::uint8_t* data, std::size_t size) noexcept;

}  // namespace devproto
