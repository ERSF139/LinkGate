#include "devproto/crc16.h"

namespace devproto {

std::uint16_t crc16_modbus(const std::uint8_t* data, std::size_t size) noexcept {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size; ++i) {
        crc = static_cast<std::uint16_t>(crc ^ data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001) != 0) {
                crc = static_cast<std::uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<std::uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

}  // namespace devproto
