#include "parser_check.h"

#include <algorithm>

#include "devproto/byte_order.h"
#include "devproto/crc16.h"
#include "devproto/sample.h"

namespace lg::test {

bool run_parser_checked(const std::uint8_t* input, std::size_t size, std::size_t chunk, ParseRun* run,
                        const char** reason) {
    if (chunk == 0) chunk = 1;
    devproto::StreamParser parser;
    std::vector<std::uint8_t> buffer;
    std::size_t fed = 0;
    std::size_t frame_bytes = 0;

    while (fed < size) {
        const std::size_t n = std::min(chunk, size - fed);
        buffer.insert(buffer.end(), input + fed, input + fed + n);
        fed += n;

        while (true) {
            const auto result = parser.parse(buffer.data(), buffer.size());
            if (result.consumed > buffer.size()) {
                *reason = "consumed more bytes than available";
                return false;
            }
            if (result.frame) {
                const devproto::FrameView& f = *result.frame;
                if (f.data < buffer.data() || f.data + f.size > buffer.data() + result.consumed) {
                    *reason = "frame view outside the consumed range";
                    return false;
                }
                const auto expected = devproto::payload_size_for(f.data[3]);
                if (!expected || *expected != f.payload_size()) {
                    *reason = "frame type and length do not match";
                    return false;
                }
                const std::size_t covered = devproto::kHeaderSize + f.payload_size();
                if (devproto::crc16_modbus(f.data, covered) != devproto::load_be16(f.data + covered)) {
                    *reason = "frame with a wrong CRC was accepted";
                    return false;
                }
                if (f.type == devproto::MessageType::Sample &&
                    !devproto::decode_sample(f.payload(), f.payload_size())) {
                    *reason = "accepted Sample frame does not decode";
                    return false;
                }
                run->frames.emplace_back(f.data, f.data + f.size);
                frame_bytes += f.size;
            }
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(result.consumed));
            if (!result.frame) {
                break;
            }
        }
        // 尽早拒绝的直接后果：等待中的字节永远凑不成比最长合法帧还长的前缀。
        if (buffer.size() >= devproto::frame_size_for_payload(devproto::kSamplePayloadSize)) {
            *reason = "parser is holding more bytes than the longest valid frame";
            return false;
        }
    }

    run->stats = parser.stats();
    run->leftover = buffer.size();
    if (run->stats.frames != run->frames.size()) {
        *reason = "frame counter does not match frames returned";
        return false;
    }
    // 守恒：每个输入字节要么被丢弃，要么属于某一帧，要么还在等待。
    if (run->stats.dropped_bytes + frame_bytes + run->leftover != size) {
        *reason = "dropped + frame bytes + leftover != input size";
        return false;
    }
    return true;
}

}  // namespace lg::test
