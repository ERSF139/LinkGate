#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

#include "devproto/byte_order.h"

using namespace devproto;

TEST(ByteOrder, StoreWritesHighByteFirst) {
    std::array<std::uint8_t, 2> b16{};
    store_be16(b16.data(), 0x0102);
    EXPECT_EQ(b16, (std::array<std::uint8_t, 2>{0x01, 0x02}));

    std::array<std::uint8_t, 4> b32{};
    store_be32(b32.data(), 0x01020304);
    EXPECT_EQ(b32, (std::array<std::uint8_t, 4>{0x01, 0x02, 0x03, 0x04}));

    std::array<std::uint8_t, 8> b64{};
    store_be64(b64.data(), 0x0102030405060708);
    EXPECT_EQ(b64, (std::array<std::uint8_t, 8>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));
}

TEST(ByteOrder, RoundTripBoundaryValues) {
    std::array<std::uint8_t, 8> buf{};
    for (std::uint16_t v : {std::uint16_t{0x0000}, std::uint16_t{0x007F}, std::uint16_t{0x0080},
                            std::uint16_t{0x00FF}, std::uint16_t{0x7FFF}, std::uint16_t{0x8000},
                            std::uint16_t{0xFFFF}}) {
        store_be16(buf.data(), v);
        EXPECT_EQ(load_be16(buf.data()), v);
    }
    for (std::uint32_t v : {0x00000000u, 0x0000007Fu, 0x00000080u, 0x000000FFu, 0x7FFFFFFFu,
                            0x80000000u, 0xFFFFFFFFu}) {
        store_be32(buf.data(), v);
        EXPECT_EQ(load_be32(buf.data()), v);
    }
    for (std::uint64_t v : {std::uint64_t{0}, std::uint64_t{0x7FFFFFFFFFFFFFFF},
                            std::uint64_t{0x8000000000000000},
                            std::numeric_limits<std::uint64_t>::max()}) {
        store_be64(buf.data(), v);
        EXPECT_EQ(load_be64(buf.data()), v);
    }
}

TEST(ByteOrder, F64PreservesBitPatterns) {
    std::array<std::uint8_t, 8> buf{};
    for (std::uint64_t bits : {std::uint64_t{0x8000000000000000}, std::uint64_t{0x7FF8000000000000},
                               std::uint64_t{0x7FF0000000000000}, std::uint64_t{0x0000000000000001}}) {
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof value);
        store_be_f64(buf.data(), value);
        EXPECT_EQ(load_be64(buf.data()), bits);

        const double back = load_be_f64(buf.data());
        std::uint64_t back_bits = 0;
        std::memcpy(&back_bits, &back, sizeof back_bits);
        EXPECT_EQ(back_bits, bits);
    }
}
