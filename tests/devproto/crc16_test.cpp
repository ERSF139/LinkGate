#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "devproto/crc16.h"

using devproto::crc16_modbus;

TEST(Crc16Modbus, StandardCheckValue) {
    const std::array<std::uint8_t, 9> digits{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(crc16_modbus(digits.data(), digits.size()), std::uint16_t{0x4B37});
}

TEST(Crc16Modbus, EmptyInputReturnsInitialValue) {
    EXPECT_EQ(crc16_modbus(nullptr, 0), std::uint16_t{0xFFFF});
}

TEST(Crc16Modbus, HeaderWithHighBytes) {
    const std::array<std::uint8_t, 6> header{0xA5, 0x5A, 0x01, 0x02, 0x00, 0x00};
    EXPECT_EQ(crc16_modbus(header.data(), header.size()), std::uint16_t{0xDFE0});
}
