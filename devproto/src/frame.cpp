#include "devproto/frame.h"

#include <cassert>
#include <cstring>

#include "devproto/byte_order.h"
#include "devproto/crc16.h"

namespace devproto {

std::size_t encode_frame_into(MessageType type, const std::uint8_t* payload,
                              std::size_t payload_size, std::uint8_t* out) noexcept {
    assert(payload_size <= kMaxPayloadSize);
    out[0] = kSync0;
    out[1] = kSync1;
    out[2] = kVersion;
    out[3] = static_cast<std::uint8_t>(type);
    store_be16(out + 4, static_cast<std::uint16_t>(payload_size));
    if (payload_size > 0) {
        std::memcpy(out + kHeaderSize, payload, payload_size);
    }
    const std::size_t covered = kHeaderSize + payload_size;
    store_be16(out + covered, crc16_modbus(out, covered));
    return covered + kCrcSize;
}

std::vector<std::uint8_t> encode_frame(MessageType type, const std::uint8_t* payload,
                                       std::size_t payload_size) {
    std::vector<std::uint8_t> frame(frame_size_for_payload(payload_size));
    encode_frame_into(type, payload, payload_size, frame.data());
    return frame;
}

std::array<std::uint8_t, frame_size_for_payload(kSamplePayloadSize)> encode_sample_frame(
    const Sample& sample) noexcept {
    const SamplePayload payload = encode_sample(sample);
    std::array<std::uint8_t, frame_size_for_payload(kSamplePayloadSize)> frame{};
    encode_frame_into(MessageType::Sample, payload.data(), payload.size(), frame.data());
    return frame;
}

const std::array<std::uint8_t, kMinFrameSize>& heartbeat_frame() noexcept {
    static const std::array<std::uint8_t, kMinFrameSize> frame = [] {
        std::array<std::uint8_t, kMinFrameSize> bytes{};
        encode_frame_into(MessageType::Heartbeat, nullptr, 0, bytes.data());
        return bytes;
    }();
    return frame;
}

}  // namespace devproto
