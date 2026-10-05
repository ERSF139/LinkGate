#pragma once

#include <unistd.h>

namespace lg::net {

// 独占一个文件描述符：只能移动，不能拷贝；析构时关闭。
class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ >= 0; }

    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    // Linux 上 close 即使返回 EINTR，fd 也已经释放；重试会关掉别人刚拿到的同号 fd。
    void reset(int fd = -1) noexcept {
        if (fd == fd_) {
            return;
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

}  // namespace lg::net
