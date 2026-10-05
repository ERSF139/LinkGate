add_library(lg_net STATIC
  src/acceptor.cpp
  src/buffer.cpp
  src/channel.cpp
  src/connector.cpp
  src/epoll_poller.cpp
  src/event_loop.cpp
  src/event_loop_thread.cpp
  src/inet_address.cpp
  src/log.cpp
  src/socket_ops.cpp
  src/tcp_client.cpp
  src/tcp_connection.cpp
  src/tcp_server.cpp
  src/timer_queue.cpp
)
add_library(lg::net ALIAS lg_net)

target_include_directories(lg_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lg_net PUBLIC Threads::Threads PRIVATE lg_warnings)
