#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "devproto/constants.h"
#include "devproto/sample.h"

namespace devproto {

// 不拥有数据：data 指向调用方缓冲区里的一整帧（帧头 + 载荷 + CRC），
// 只在那块缓冲区未被修改前有效。
struct FrameView {
    MessageType type = MessageType::Heartbeat;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    const std::uint8_t* payload() const noexcept { return data + kHeaderSize; }
    std::size_t payload_size() const noexcept { return size - kMinFrameSize; }
};

constexpr std::size_t frame_size_for_payload(std::size_t payload_size) noexcept {
    return kMinFrameSize + payload_size;
}

// 不检查类型与长度是否匹配：测试要用它构造非法帧。payload_size 不得超过 kMaxPayloadSize，
// out 至少要有 frame_size_for_payload(payload_size) 字节。返回写入的字节数。
std::size_t encode_frame_into(MessageType type, const std::uint8_t* payload,
                              std::size_t payload_size, std::uint8_t* out) noexcept;

std::vector<std::uint8_t> encode_frame(MessageType type, const std::uint8_t* payload,
                                       std::size_t payload_size);

std::array<std::uint8_t, frame_size_for_payload(kSamplePayloadSize)> encode_sample_frame(
    const Sample& sample) noexcept;

const std::array<std::uint8_t, kMinFrameSize>& heartbeat_frame() noexcept;

}  // namespace devproto
