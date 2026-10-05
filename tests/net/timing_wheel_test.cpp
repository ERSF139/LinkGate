#include <gtest/gtest.h>

#include <memory>

#include "lg/net/timing_wheel.h"

namespace {

struct Probe {
    explicit Probe(int* expired) : expired_(expired) {}
    ~Probe() { ++*expired_; }
    int* expired_;
};

}  // namespace

TEST(TimingWheel, EntryExpiresAfterSlotTicksWithoutTouch) {
    int expired = 0;
    lg::net::TimingWheel wheel(3);
    {
        auto entry = std::make_shared<Probe>(&expired);
        wheel.touch(entry);
    }
    wheel.tick();
    wheel.tick();
    EXPECT_EQ(expired, 0);
    wheel.tick();
    EXPECT_EQ(expired, 1);
}

TEST(TimingWheel, TouchExtendsLifetime) {
    int expired = 0;
    lg::net::TimingWheel wheel(3);
    std::weak_ptr<Probe> weak;
    {
        auto entry = std::make_shared<Probe>(&expired);
        weak = entry;
        wheel.touch(entry);
    }
    for (int i = 0; i < 10; ++i) {
        wheel.tick();
        if (auto entry = weak.lock()) {
            wheel.touch(entry);
        }
    }
    EXPECT_EQ(expired, 0);
    wheel.tick();
    wheel.tick();
    wheel.tick();
    EXPECT_EQ(expired, 1);
}

TEST(TimingWheel, RepeatedTouchInSameSlotIsDeduplicated) {
    int expired = 0;
    lg::net::TimingWheel wheel(2);
    {
        auto entry = std::make_shared<Probe>(&expired);
        for (int i = 0; i < 1000; ++i) {
            wheel.touch(entry);
        }
        EXPECT_EQ(entry.use_count(), 2);
    }
    wheel.tick();
    wheel.tick();
    EXPECT_EQ(expired, 1);
}
