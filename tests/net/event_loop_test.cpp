#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "lg/net/event_loop.h"
#include "lg/net/event_loop_thread.h"
#include "net_util.h"

using lg::net::EventLoop;
using lg::net::EventLoopThread;
using namespace std::chrono_literals;

TEST(EventLoop, RunInLoopFromAnotherThreadExecutesInLoopThread) {
    EventLoopThread thread("test-loop");
    EventLoop* loop = thread.start();
    std::atomic<bool> in_loop_thread{false};
    std::atomic<bool> done{false};
    loop->run_in_loop([&] {
        in_loop_thread = loop->is_in_loop_thread();
        done = true;
    });
    ASSERT_TRUE(lg::test::wait_until([&] { return done.load(); }));
    EXPECT_TRUE(in_loop_thread.load());
}

TEST(EventLoop, RunInLoopSyncWaitsForCompletion) {
    EventLoopThread thread("test-loop");
    EventLoop* loop = thread.start();
    int value = 0;
    loop->run_in_loop_sync([&] {
        std::this_thread::sleep_for(20ms);
        value = 42;
    });
    EXPECT_EQ(value, 42);
}

TEST(EventLoop, QueueInLoopPreservesOrder) {
    EventLoopThread thread("test-loop");
    EventLoop* loop = thread.start();
    std::vector<int> order;
    for (int i = 0; i < 100; ++i) {
        loop->queue_in_loop([&order, i] { order.push_back(i); });
    }
    loop->run_in_loop_sync([] {});
    ASSERT_EQ(order.size(), 100u);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
    }
}

TEST(EventLoop, TimersFireInDeadlineOrder) {
    EventLoopThread thread("test-loop");
    EventLoop* loop = thread.start();
    std::vector<int> order;
    std::atomic<int> fired{0};
    loop->run_in_loop_sync([&] {
        loop->run_after(60ms, [&] { order.push_back(3); ++fired; });
        loop->run_after(20ms, [&] { order.push_back(1); ++fired; });
        loop->run_after(40ms, [&] { order.push_back(2); ++fired; });
    });
    ASSERT_TRUE(lg::test::wait_until([&] { return fired.load() == 3; }));
    loop->run_in_loop_sync([&] { EXPECT_EQ(order, (std::vector<int>{1, 2, 3})); });
}

TEST(EventLoop, CancelledTimerDoesNotFire) {
    EventLoopThread thread("test-loop");
    EventLoop* loop = thread.start();
    std::atomic<bool> fired{false};
    const auto id = loop->run_after(30ms, [&] { fired = true; });
    loop->cancel(id);
    std::this_thread::sleep_for(80ms);
    EXPECT_FALSE(fired.load());
}

TEST(EventLoop, RepeatingTimerCanCancelItself) {
    EventLoopThread thread("test-loop");
    EventLoop* loop = thread.start();
    std::atomic<int> count{0};
    lg::net::TimerId id;
    loop->run_in_loop_sync([&] {
        id = loop->run_every(5ms, [&] {
            if (++count == 3) {
                loop->cancel(id);
            }
        });
    });
    ASSERT_TRUE(lg::test::wait_until([&] { return count.load() >= 3; }));
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(count.load(), 3);
}

TEST(EventLoop, QuitFromAnotherThreadStopsLoop) {
    EventLoop* loop_ptr = nullptr;
    std::atomic<bool> ready{false};
    std::thread t([&] {
        EventLoop loop;
        loop_ptr = &loop;
        ready = true;
        loop.loop();
    });
    ASSERT_TRUE(lg::test::wait_until([&] { return ready.load(); }));
    std::this_thread::sleep_for(10ms);
    loop_ptr->quit();
    t.join();
}

TEST(EventLoopThreadPool, RoundRobinAndZeroThreadsFallsBackToBase) {
    EventLoop base;
    lg::net::EventLoopThreadPool none(&base, 0, "none");
    none.start();
    EXPECT_EQ(none.next_loop(), &base);

    lg::net::EventLoopThreadPool pool(&base, 3, "pool");
    pool.start();
    ASSERT_EQ(pool.loops().size(), 3u);
    EventLoop* a = pool.next_loop();
    EventLoop* b = pool.next_loop();
    EventLoop* c = pool.next_loop();
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_EQ(pool.next_loop(), a);
}
