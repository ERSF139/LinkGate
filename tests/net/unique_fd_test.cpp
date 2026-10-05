#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "lg/net/unique_fd.h"

using lg::net::UniqueFd;

namespace {

bool is_open(int fd) { return ::fcntl(fd, F_GETFD) != -1 || errno != EBADF; }

int open_null() { return ::open("/dev/null", O_RDONLY | O_CLOEXEC); }

}  // namespace

TEST(UniqueFd, ClosesOnDestruction) {
    int raw = -1;
    {
        UniqueFd fd(open_null());
        raw = fd.get();
        ASSERT_TRUE(fd);
        EXPECT_TRUE(is_open(raw));
    }
    EXPECT_FALSE(is_open(raw));
}

TEST(UniqueFd, MoveTransfersOwnership) {
    UniqueFd a(open_null());
    const int raw = a.get();
    UniqueFd b(std::move(a));
    EXPECT_FALSE(a);
    EXPECT_EQ(b.get(), raw);

    UniqueFd c;
    c = std::move(b);
    EXPECT_FALSE(b);
    EXPECT_EQ(c.get(), raw);
    EXPECT_TRUE(is_open(raw));
}

TEST(UniqueFd, ResetClosesPreviousAndReleaseGivesUpOwnership) {
    UniqueFd fd(open_null());
    const int first = fd.get();
    fd.reset(open_null());
    EXPECT_FALSE(is_open(first));

    const int second = fd.release();
    EXPECT_FALSE(fd);
    EXPECT_TRUE(is_open(second));
    ::close(second);
}

TEST(UniqueFd, ResetToSameFdDoesNotClose) {
    UniqueFd fd(open_null());
    const int raw = fd.get();
    fd.reset(raw);
    EXPECT_TRUE(is_open(raw));
}
