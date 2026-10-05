#include "lg/net/tcp_server.h"

#include "lg/net/acceptor.h"
#include "lg/net/event_loop.h"
#include "lg/net/event_loop_thread.h"
#include "lg/net/log.h"
#include "lg/net/socket_ops.h"
#include "lg/net/tcp_connection.h"

namespace lg::net {

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listen_addr, std::string name,
                     EventLoopThreadPool* pool, bool reuse_port)
    : loop_(loop),
      name_(std::move(name)),
      pool_(pool),
      acceptor_(std::make_unique<Acceptor>(loop, listen_addr, reuse_port)),
      core_(std::make_shared<Core>(Core{loop, {}})) {
    acceptor_->set_new_connection_callback(
        [this](UniqueFd socket, const InetAddress& peer) { new_connection(std::move(socket), peer); });
}

TcpServer::~TcpServer() {
    loop_->assert_in_loop_thread();
    acceptor_.reset();
    for (auto& entry : core_->connections) {
        TcpConnectionPtr conn = std::move(entry.second);
        conn->loop()->run_in_loop([conn] { conn->connection_destroyed(); });
    }
    core_->connections.clear();
}

void TcpServer::start() {
    loop_->assert_in_loop_thread();
    if (!started_.exchange(true)) {
        acceptor_->listen();
    }
}

InetAddress TcpServer::listen_address() const noexcept { return acceptor_->local_address(); }

std::size_t TcpServer::connection_count() const noexcept { return core_->connections.size(); }

void TcpServer::new_connection(UniqueFd socket, const InetAddress& peer) {
    loop_->assert_in_loop_thread();
    EventLoop* io_loop = pool_ != nullptr ? pool_->next_loop() : loop_;
    const std::uint64_t id = ++next_connection_id_;
    const InetAddress local = sockets::local_address(socket.get());
    auto conn = std::make_shared<TcpConnection>(io_loop, name_ + "#" + std::to_string(id), id,
                                                std::move(socket), local, peer);
    core_->connections[id] = conn;
    conn->set_connection_callback(connection_callback_);
    conn->set_message_callback(message_callback_);
    conn->set_write_complete_callback(write_complete_callback_);
    conn->set_close_callback([weak_core = std::weak_ptr<Core>(core_)](const TcpConnectionPtr& c) {
        remove_connection(weak_core, c);
    });
    io_loop->run_in_loop([conn] { conn->connection_established(); });
}

void TcpServer::remove_connection(const std::weak_ptr<Core>& weak_core, const TcpConnectionPtr& conn) {
    // 无论服务器还在不在，Channel 都要在连接自己的 loop 里注销。
    conn->loop()->queue_in_loop([conn] { conn->connection_destroyed(); });
    if (const std::shared_ptr<Core> core = weak_core.lock()) {
        core->loop->run_in_loop([core, conn] { core->connections.erase(conn->id()); });
    }
}

}  // namespace lg::net
