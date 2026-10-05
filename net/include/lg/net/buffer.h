#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg::net {

// 应用层缓冲区：[read_index_, write_index_) 是可读数据。
// 取走数据只移动读指针，不搬移内存；只有可写空间不够时才把剩余数据挪到开头，或者扩容。
// 因此 peek() 返回的指针在下一次 append / ensure_writable / read_fd 之前一直有效。
class Buffer {
public:
    static constexpr std::size_t kInitialSize = 1024;

    explicit Buffer(std::size_t initial_size = kInitialSize) : storage_(initial_size) {}

    std::size_t readable_bytes() const noexcept { return write_index_ - read_index_; }
    std::size_t writable_bytes() const noexcept { return storage_.size() - write_index_; }
    std::size_t capacity() const noexcept { return storage_.size(); }

    const std::uint8_t* peek() const noexcept { return storage_.data() + read_index_; }

    void retrieve(std::size_t n) noexcept;
    void retrieve_all() noexcept;

    void append(const void* data, std::size_t n);
    void ensure_writable(std::size_t n);
    std::uint8_t* begin_write() noexcept { return storage_.data() + write_index_; }
    void has_written(std::size_t n) noexcept { write_index_ += n; }

    // 释放突发流量留下的大块内存，只保留当前数据加 reserve 字节。
    void shrink(std::size_t reserve);

    // 一次 readv 读进缓冲区，外加 64 KiB 栈上临时空间，避免为了偶尔的大包预先分配大缓冲。
    // 返回值与 read 相同；出错时 *saved_errno 为 errno。
    ssize_t read_fd(int fd, int* saved_errno);

private:
    void make_space(std::size_t n);

    std::vector<std::uint8_t> storage_;
    std::size_t read_index_ = 0;
    std::size_t write_index_ = 0;
};

}  // namespace lg::net
