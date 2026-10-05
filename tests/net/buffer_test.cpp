#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

#include "lg/net/buffer.h"

using lg::net::Buffer;

namespace {

std::string readable(const Buffer& buffer) {
    return std::string(reinterpret_cast<const char*>(buffer.peek()), buffer.readable_bytes());
}

}  // namespace

TEST(Buffer, AppendAndRetrieve) {
    Buffer buffer;
    buffer.append("hello world", 11);
    EXPECT_EQ(buffer.readable_bytes(), 11u);
    buffer.retrieve(6);
    EXPECT_EQ(readable(buffer), "world");
    buffer.retrieve(5);
    EXPECT_EQ(buffer.readable_bytes(), 0u);
}

TEST(Buffer, RetrieveOnlyMovesReadIndex) {
    Buffer buffer;
    buffer.append("abcdef", 6);
    const std::uint8_t* before = buffer.peek();
    buffer.retrieve(2);
    EXPECT_EQ(buffer.peek(), before + 2);
}

TEST(Buffer, CompactsInsteadOfGrowingWhenFrontSpaceSuffices) {
    Buffer buffer(16);
    buffer.append("0123456789ab", 12);
    buffer.retrieve(10);
    const std::size_t capacity = buffer.capacity();
    buffer.append("cdefghijkl", 10);
    EXPECT_EQ(buffer.capacity(), capacity);
    EXPECT_EQ(readable(buffer), "abcdefghijkl");
}

TEST(Buffer, GrowsWhenNeeded) {
    Buffer buffer(8);
    const std::string big(1000, 'x');
    buffer.append(big.data(), big.size());
    EXPECT_EQ(buffer.readable_bytes(), 1000u);
    EXPECT_GE(buffer.capacity(), 1000u);
}

TEST(Buffer, ShrinkKeepsData) {
    Buffer buffer;
    const std::string big(100000, 'y');
    buffer.append(big.data(), big.size());
    buffer.retrieve(99990);
    buffer.shrink(16);
    EXPECT_EQ(readable(buffer), std::string(10, 'y'));
    EXPECT_LE(buffer.capacity(), 26u);
}

TEST(Buffer, ReadFdUsesExtraSpaceForLargeReads) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    const std::vector<char> data(20000, 'z');
    ASSERT_EQ(::write(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));

    Buffer buffer(64);
    int saved_errno = 0;
    const ssize_t n = buffer.read_fd(fds[0], &saved_errno);
    EXPECT_EQ(n, static_cast<ssize_t>(data.size()));
    EXPECT_EQ(buffer.readable_bytes(), data.size());
    ::close(fds[0]);
    ::close(fds[1]);
}
