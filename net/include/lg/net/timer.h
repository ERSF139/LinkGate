#pragma once

#include <chrono>
#include <cstdint>

namespace lg::net {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

struct TimerId {
    std::uint64_t sequence = 0;
    bool valid() const noexcept { return sequence != 0; }
};

}  // namespace lg::net
