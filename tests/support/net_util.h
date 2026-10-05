#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace lg::test {

// 测试用的阻塞客户端，刻意不依赖 lg_net：用一套独立实现去验证网关和网络库。
class BlockingClient {
public:
    BlockingClient() = default;
    ~BlockingClient();

    BlockingClient(const BlockingClient&) = delete;
    BlockingClient& operator=(const BlockingClient&) = delete;
    BlockingClient(BlockingClient&& other) noexcept;
    BlockingClient& operator=(BlockingClient&& other) noexcept;

    bool connect(std::uint16_t port, std::chrono::milliseconds timeout = std::chrono::seconds(2));
    bool send_all(const void* data, std::size_t size);
    bool send_all(const std::vector<std::uint8_t>& bytes) { return send_all(bytes.data(), bytes.size()); }
    // 读到恰好 size 字节或超时。
    bool read_exact(std::size_t size, std::vector<std::uint8_t>* out, std::chrono::milliseconds timeout);
    // 读取当前能读到的数据，最多等 timeout。
    std::vector<std::uint8_t> read_some(std::chrono::milliseconds timeout);
    // 对端关闭（read 返回 0）或连接被重置时返回 true。
    bool wait_closed(std::chrono::milliseconds timeout);
    void set_receive_buffer(int bytes);
    void close();
    int fd() const noexcept { return fd_; }

private:
    int fd_ = -1;
};

bool wait_until(const std::function<bool()>& predicate,
                std::chrono::milliseconds timeout = std::chrono::seconds(5));

}  // namespace lg::test
