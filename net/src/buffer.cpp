#include "lg/net/buffer.h"

#include <sys/uio.h>

#include <cassert>
#include <cerrno>
#include <cstring>

namespace lg::net {

void Buffer::retrieve(std::size_t n) noexcept {
    assert(n <= readable_bytes());
    if (n < readable_bytes()) {
        read_index_ += n;
    } else {
        retrieve_all();
    }
}

void Buffer::retrieve_all() noexcept {
    read_index_ = 0;
    write_index_ = 0;
}

void Buffer::append(const void* data, std::size_t n) {
    if (n == 0) {
        return;
    }
    ensure_writable(n);
    std::memcpy(begin_write(), data, n);
    has_written(n);
}

void Buffer::ensure_writable(std::size_t n) {
    if (writable_bytes() < n) {
        make_space(n);
    }
}

void Buffer::make_space(std::size_t n) {
    const std::size_t readable = readable_bytes();
    if (read_index_ + writable_bytes() >= n) {
        std::memmove(storage_.data(), storage_.data() + read_index_, readable);
        read_index_ = 0;
        write_index_ = readable;
    } else {
        storage_.resize(write_index_ + n);
    }
}

void Buffer::shrink(std::size_t reserve) {
    std::vector<std::uint8_t> fresh(readable_bytes() + reserve);
    std::memcpy(fresh.data(), peek(), readable_bytes());
    write_index_ = readable_bytes();
    read_index_ = 0;
    storage_.swap(fresh);
}

ssize_t Buffer::read_fd(int fd, int* saved_errno) {
    std::uint8_t extra[65536];
    const std::size_t writable = writable_bytes();

    iovec vec[2];
    vec[0].iov_base = begin_write();
    vec[0].iov_len = writable;
    vec[1].iov_base = extra;
    vec[1].iov_len = sizeof extra;
    const int iov_count = writable < sizeof extra ? 2 : 1;

    const ssize_t n = ::readv(fd, vec, iov_count);
    if (n < 0) {
        *saved_errno = errno;
        return n;
    }
    const auto received = static_cast<std::size_t>(n);
    if (received <= writable) {
        write_index_ += received;
    } else {
        write_index_ = storage_.size();
        append(extra, received - writable);
    }
    return n;
}

}  // namespace lg::net
