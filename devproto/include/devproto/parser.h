#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "devproto/frame.h"

namespace devproto {

// 统计口径见 protocol.md 第 7 节。
struct ParserStats {
    std::uint64_t frames = 0;
    std::uint64_t bad_frames = 0;
    std::uint64_t dropped_bytes = 0;
};

// 无缓冲的流式解析器：不拥有任何字节，只告诉调用方"这段输入里有没有一帧、可以丢掉多少字节"。
// 缓冲区由调用方管理（网关里是 lg::net::Buffer），因此解析过程不发生任何拷贝。
class StreamParser {
public:
    struct Result {
        // 有值时指向 data 内部的一整帧，调用方必须在丢弃 consumed 字节之前用完它。
        std::optional<FrameView> frame;
        // 调用方应从输入开头丢弃的字节数：跳过的噪声，加上返回的那一帧（如果有）。
        // 没有帧时，[consumed, size) 是必须保留、等待更多数据的部分。
        std::size_t consumed = 0;
    };

    // 每次调用最多返回一帧。调用方丢弃 consumed 字节后应继续调用，直到 frame 为空。
    Result parse(const std::uint8_t* data, std::size_t size) noexcept;

    const ParserStats& stats() const noexcept { return stats_; }

private:
    ParserStats stats_;
};

}  // namespace devproto
