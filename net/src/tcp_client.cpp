#include "lg/net/tcp_client.h"

#include "lg/net/connector.h"
#include "lg/net/event_loop.h"
#include "lg/net/socket_ops.h"
#include "lg/net/tcp_connection.h"

namespace lg::net {

TcpClient::TcpClient(EventLoop* loop, const InetAddress& server, std::string name)
    : state_(std::make_shared<State>()) {
    state_->loop = loop;
    state_->name = std::move(name);
    state_->connector = std::make_shared<Connector>(loop, server);
    state_->connector->set_new_connection_callback(
        [weak = std::weak_ptr<State>(state_)](UniqueFd socket) { on_connected(weak, std::move(socket)); });
}

TcpClient::~TcpClient() {
    const std::shared_ptr<State> state = state_;
    state->wanted.store(false, std::memory_order_release);
    state->retry.store(false, std::memory_order_release);
    auto teardown = [state] {
        state->connector->stop();
        TcpConnectionPtr conn;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            conn.swap(state->connection);
        }
        if (conn) {
            conn->force_close();
        }
    };
    EventLoop* loop = state->loop;
    if (loop->is_in_loop_thread() || !loop->is_running()) {
        teardown();
    } else {
        loop->run_in_loop_sync(teardown);
    }
}

void TcpClient::connect() {
    state_->wanted.store(true, std::memory_order_release);
    state_->connector->start();
}

void TcpClient::disconnect() {
    state_->wanted.store(false, std::memory_order_release);
    state_->connector->stop();
    if (const TcpConnectionPtr conn = connection()) {
        conn->shutdown();
    }
}

TcpConnectionPtr TcpClient::connection() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->connection;
}

void TcpClient::on_connected(const std::weak_ptr<State>& weak_state, UniqueFd socket) {
    const std::shared_ptr<State> state = weak_state.lock();
    if (!state || !state->wanted.load(std::memory_order_acquire)) {
        return;
    }
    const InetAddress local = sockets::local_address(socket.get());
    const InetAddress peer = sockets::peer_address(socket.get());
    const std::uint64_t id = ++state->next_id;
    auto conn = std::make_shared<TcpConnection>(state->loop, state->name + "#" + std::to_string(id), id,
                                                std::move(socket), local, peer);
    conn->set_connection_callback(state->connection_callback);
    conn->set_message_callback(state->message_callback);
    conn->set_write_complete_callback(state->write_complete_callback);
    conn->set_close_callback([weak_state](const TcpConnectionPtr& c) { on_closed(weak_state, c); });
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->connection = conn;
    }
    conn->connection_established();
}

void TcpClient::on_closed(const std::weak_ptr<State>& weak_state, const TcpConnectionPtr& conn) {
    conn->loop()->queue_in_loop([conn] { conn->connection_destroyed(); });
    const std::shared_ptr<State> state = weak_state.lock();
    if (!state) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->connection == conn) {
            state->connection.reset();
        }
    }
    if (state->retry.load(std::memory_order_acquire) && state->wanted.load(std::memory_order_acquire)) {
        state->connector->restart();
    }
}

}  // namespace lg::net
