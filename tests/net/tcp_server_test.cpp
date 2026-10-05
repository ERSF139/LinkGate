#include <gtest/gtest.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lg/net/buffer.h"
#include "lg/net/event_loop.h"
#include "lg/net/event_loop_thread.h"
#include "lg/net/tcp_client.h"
#include "lg/net/tcp_connection.h"
#include "lg/net/tcp_server.h"
#include "net_util.h"

using lg::net::Buffer;
using lg::net::EventLoop;
using lg::net::EventLoopThread;
using lg::net::EventLoopThreadPool;
using lg::net::InetAddress;
using lg::net::TcpConnectionPtr;
using lg::net::TcpServer;
using lg::test::BlockingClient;
using namespace std::chrono_literals;

namespace {

// 在后台线程里跑一个 echo 服务器：TcpServer 和线程池都在它的 base loop 线程里创建和销毁。
class EchoServer {
public:
    explicit EchoServer(std::size_t io_threads, std::function<void(const TcpConnectionPtr&)> on_connection = {},
                        std::uint16_t port = 0)
        : thread_("echo-base"), on_connection_(std::move(on_connection)) {
        loop_ = thread_.start();
        loop_->run_in_loop_sync([this, io_threads, port] {
            pool_ = std::make_unique<EventLoopThreadPool>(loop_, io_threads, "echo-io");
            pool_->start();
            server_ = std::make_unique<TcpServer>(loop_, InetAddress(port, true), "echo", pool_.get());
            server_->set_connection_callback([this](const TcpConnectionPtr& conn) {
                if (conn->connected()) {
                    ++connected_;
                } else {
                    ++disconnected_;
                }
                if (on_connection_) on_connection_(conn);
            });
            server_->set_message_callback([](const TcpConnectionPtr& conn, Buffer* buffer) {
                conn->send(buffer->peek(), buffer->readable_bytes());
                buffer->retrieve_all();
            });
            server_->start();
            port_ = server_->listen_address().port();
        });
    }

    ~EchoServer() {
        loop_->run_in_loop_sync([this] {
            server_.reset();
            pool_.reset();
        });
    }

    std::uint16_t port() const { return port_; }
    std::size_t connections() {
        std::size_t n = 0;
        loop_->run_in_loop_sync([this, &n] { n = server_->connection_count(); });
        return n;
    }

    std::atomic<int> connected_{0};
    std::atomic<int> disconnected_{0};

private:
    EventLoopThread thread_;
    EventLoop* loop_ = nullptr;
    std::unique_ptr<EventLoopThreadPool> pool_;
    std::unique_ptr<TcpServer> server_;
    std::uint16_t port_ = 0;
    std::function<void(const TcpConnectionPtr&)> on_connection_;
};

void expect_echo(std::uint16_t port, const std::string& message) {
    BlockingClient client;
    ASSERT_TRUE(client.connect(port));
    ASSERT_TRUE(client.send_all(message.data(), message.size()));
    std::vector<std::uint8_t> reply;
    ASSERT_TRUE(client.read_exact(message.size(), &reply, 2s));
    EXPECT_EQ(std::string(reply.begin(), reply.end()), message);
}

std::uint16_t unused_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
    socklen_t len = sizeof addr;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    ::close(fd);
    return ntohs(addr.sin_port);
}

}  // namespace

TEST(TcpServer, EchoSingleThread) {
    EchoServer server(0);
    expect_echo(server.port(), "hello LinkGate");
}

TEST(TcpServer, EchoWithIoThreads) {
    EchoServer server(3);
    expect_echo(server.port(), "hello from a pool");
}

TEST(TcpServer, ManyConcurrentClients) {
    EchoServer server(4);
    std::vector<std::thread> threads;
    std::atomic<int> ok{0};
    for (int i = 0; i < 32; ++i) {
        threads.emplace_back([&server, &ok, i] {
            BlockingClient client;
            if (!client.connect(server.port())) return;
            const std::string message = "client-" + std::to_string(i) + std::string(1000, 'x');
            if (!client.send_all(message.data(), message.size())) return;
            std::vector<std::uint8_t> reply;
            if (client.read_exact(message.size(), &reply, 3s) && std::string(reply.begin(), reply.end()) == message) {
                ++ok;
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(ok.load(), 32);
    EXPECT_TRUE(lg::test::wait_until([&] { return server.connections() == 0; }));
    EXPECT_EQ(server.disconnected_.load(), 32);
}

TEST(TcpServer, ClientCloseRemovesConnection) {
    EchoServer server(2);
    {
        BlockingClient client;
        ASSERT_TRUE(client.connect(server.port()));
        ASSERT_TRUE(lg::test::wait_until([&] { return server.connections() == 1; }));
    }
    EXPECT_TRUE(lg::test::wait_until([&] { return server.connections() == 0; }));
}

TEST(TcpServer, ForceCloseFromAnotherThread) {
    std::atomic<bool> got{false};
    TcpConnectionPtr captured;
    std::mutex mutex;
    EchoServer server(2, [&](const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            std::lock_guard<std::mutex> lock(mutex);
            captured = conn;
            got = true;
        }
    });
    BlockingClient client;
    ASSERT_TRUE(client.connect(server.port()));
    ASSERT_TRUE(lg::test::wait_until([&] { return got.load(); }));
    {
        std::lock_guard<std::mutex> lock(mutex);
        captured->force_close();
        captured.reset();
    }
    EXPECT_TRUE(client.wait_closed(2s));
}

TEST(TcpServer, HighWaterMarkCallbackFiresForSlowReader) {
    std::atomic<std::size_t> reported{0};
    EchoServer server(1, [&](const TcpConnectionPtr& conn) {
        if (!conn->connected()) return;
        conn->set_high_water_mark_callback(
            [&reported](const TcpConnectionPtr&, std::size_t size) { reported = size; }, 256 * 1024);
        const std::vector<std::uint8_t> blob(8 * 1024 * 1024, 0x42);
        conn->send(blob.data(), blob.size());
    });
    BlockingClient client;
    client.set_receive_buffer(4096);
    ASSERT_TRUE(client.connect(server.port()));
    EXPECT_TRUE(lg::test::wait_until([&] { return reported.load() >= 256 * 1024; }));
}

TEST(TcpClient, EchoThroughServer) {
    EchoServer server(1);
    EventLoopThread client_thread("client");
    EventLoop* loop = client_thread.start();

    std::atomic<bool> connected{false};
    std::string received;
    std::mutex mutex;
    auto client = std::make_unique<lg::net::TcpClient>(loop, InetAddress(server.port(), true), "test-client");
    client->set_connection_callback([&](const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            connected = true;
            conn->send("ping", 4);
        }
    });
    client->set_message_callback([&](const TcpConnectionPtr&, Buffer* buffer) {
        std::lock_guard<std::mutex> lock(mutex);
        received.append(reinterpret_cast<const char*>(buffer->peek()), buffer->readable_bytes());
        buffer->retrieve_all();
    });
    client->connect();
    ASSERT_TRUE(lg::test::wait_until([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return received == "ping";
    }));
    client.reset();
}

TEST(TcpClient, RetriesUntilServerAppears) {
    const std::uint16_t port = unused_port();
    EventLoopThread client_thread("client");
    EventLoop* loop = client_thread.start();

    std::atomic<bool> connected{false};
    auto client = std::make_unique<lg::net::TcpClient>(loop, InetAddress(port, true), "retry-client");
    client->set_connection_callback([&](const TcpConnectionPtr& conn) {
        if (conn->connected()) connected = true;
    });
    client->enable_retry(true);
    client->connect();
    std::this_thread::sleep_for(300ms);
    EXPECT_FALSE(connected.load());

    EchoServer server(0, {}, port);
    EXPECT_TRUE(lg::test::wait_until([&] { return connected.load(); }, 5s));
    client.reset();
}
