#include "devproto/parser.h"

#include "devproto/byte_order.h"
#include "devproto/crc16.h"

namespace devproto {

namespace {

bool header_acceptable(const std::uint8_t* header) noexcept {
    if (header[2] != kVersion) {
        return false;
    }
    const std::optional<std::size_t> expected = payload_size_for(header[3]);
    if (!expected) {
        return false;
    }
    const std::size_t length = load_be16(header + 4);
    return length == *expected && length <= kMaxPayloadSize;
}

}  // namespace

StreamParser::Result StreamParser::parse(const std::uint8_t* data, std::size_t size) noexcept {
    std::size_t pos = 0;
    while (true) {
        while (pos < size) {
            if (data[pos] == kSync0 && (pos + 1 == size || data[pos + 1] == kSync1)) {
                break;
            }
            ++pos;
            ++stats_.dropped_bytes;
        }

        if (size - pos < kHeaderSize) {
            return {std::nullopt, pos};
        }

        const std::uint8_t* header = data + pos;
        if (!header_acceptable(header)) {
            ++stats_.bad_frames;
            ++stats_.dropped_bytes;
            ++pos;
            continue;
        }

        const std::size_t payload_size = load_be16(header + 4);
        const std::size_t total = frame_size_for_payload(payload_size);
        if (size - pos < total) {
            return {std::nullopt, pos};
        }

        const std::size_t covered = kHeaderSize + payload_size;
        if (crc16_modbus(header, covered) != load_be16(header + covered)) {
            ++stats_.bad_frames;
            ++stats_.dropped_bytes;
            ++pos;
            continue;
        }

        ++stats_.frames;
        FrameView view;
        view.type = static_cast<MessageType>(header[3]);
        view.data = header;
        view.size = total;
        return {view, pos + total};
    }
}

}  // namespace devproto
