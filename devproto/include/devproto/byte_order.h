#pragma once

#include <cstdint>
#include <cstring>

namespace devproto {

inline std::uint16_t load_be16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

inline std::uint32_t load_be32(const std::uint8_t* p) noexcept {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
           (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}

inline std::uint64_t load_be64(const std::uint8_t* p) noexcept {
    return (std::uint64_t{load_be32(p)} << 32) | load_be32(p + 4);
}

inline void store_be16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}

inline void store_be32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

inline void store_be64(std::uint8_t* p, std::uint64_t v) noexcept {
    store_be32(p, static_cast<std::uint32_t>(v >> 32));
    store_be32(p + 4, static_cast<std::uint32_t>(v));
}

inline double load_be_f64(const std::uint8_t* p) noexcept {
    const std::uint64_t bits = load_be64(p);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

inline void store_be_f64(std::uint8_t* p, double value) noexcept {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    store_be64(p, bits);
}

}  // namespace devproto
