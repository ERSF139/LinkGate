#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace devproto {

inline constexpr std::uint8_t kSync0 = 0xA5;
inline constexpr std::uint8_t kSync1 = 0x5A;
inline constexpr std::uint8_t kVersion = 0x01;

inline constexpr std::size_t kHeaderSize = 6;
inline constexpr std::size_t kCrcSize = 2;
inline constexpr std::size_t kMinFrameSize = kHeaderSize + kCrcSize;
inline constexpr std::size_t kMaxPayloadSize = 4096;

enum class MessageType : std::uint8_t {
    Sample = 0x01,
    Heartbeat = 0x02,
};

inline constexpr std::size_t kSamplePayloadSize = 36;
inline constexpr std::size_t kHeartbeatPayloadSize = 0;

// protocol.md 第 4 节：版本 0x01 的类型表是封闭的（D-001），未知类型没有合法长度。
constexpr std::optional<std::size_t> payload_size_for(std::uint8_t type) noexcept {
    switch (static_cast<MessageType>(type)) {
        case MessageType::Sample: return kSamplePayloadSize;
        case MessageType::Heartbeat: return kHeartbeatPayloadSize;
    }
    return std::nullopt;
}

}  // namespace devproto
