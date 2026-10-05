# LinkGate 架构

## 1. 拓扑

```text
直连（DeviceLink 现状）                    网关（LinkGate）

 设备1 ──┬── 客户端1                       设备1 ──┐                 ┌── 客户端1
 设备2 ──┼── 客户端2                       设备2 ──┼──▶ LinkGate ──┼── 客户端2
  ...    └── ...                             ...   ┘   (9100)  (9000) └── ...
 N 台设备 × M 个客户端 = N×M 条连接         N + M 条连接
```

设备主动连接网关的设备端口（默认 9100），DeviceLink 客户端连接订阅端口（默认 9000，客户端无需修改）。网关校验每一帧，把合法的 Sample 原样转发给所有订阅者，并自己每秒给订阅者发心跳。

## 2. 模块分层

```text
            ┌──────────────┐
            │   linkgate   │  gateway/main.cpp：参数解析、signalfd、启动网关
            └──────┬───────┘
            ┌──────▼───────┐
            │  lg_gateway  │  Gateway、Hub/Shard、DeviceRegistry、背压、空闲检测、统计
            └──┬────────┬──┘
       ┌───────▼──┐  ┌──▼────────┐
       │  lg_net  │  │ devproto  │
       │ (Linux)  │  │ (零依赖)   │
       └──────────┘  └───────────┘
```

- `devproto` 只依赖标准库，Windows（MinGW/MSVC）和 Linux 都能编译，DeviceLink 也可以直接使用。它不认识 socket。
- `lg_net` 只依赖 Linux 系统调用和标准库，不认识协议。
- `lg_gateway` 把两者粘起来。换一种协议，`lg_net` 一行都不用改；协议的测试也完全不需要建立网络连接。

## 3. lg_net：Reactor

```text
EventLoop (每线程一个)
 ├── EpollPoller        epoll_wait，LT，fd → Channel*
 ├── Channel            一个 fd 的关注事件 + 回调，不拥有 fd
 ├── TimerQueue         timerfd + 按到期时间排序的集合
 ├── wakeup eventfd     别的线程投递任务后写它，唤醒 epoll_wait
 └── pending functors   run_in_loop / queue_in_loop 投递的任务

TcpServer (base loop)
 ├── Acceptor           监听 socket，accept4 到 EAGAIN，fd 耗尽时用预留 fd 清积压
 └── connections        id → TcpConnectionPtr，只在 base loop 线程里改

TcpConnection (某个 IO loop)
 ├── UniqueFd + Channel
 ├── input Buffer       readv + 64 KiB 栈上临时空间
 └── output Buffer      写不完的数据；只在非空时关注 EPOLLOUT
```

一次读事件：`epoll_wait` 返回 → `Channel::handle_event`（通过 `tie` 持有连接的强引用）→ `TcpConnection::handle_read` → 用户的消息回调。

## 4. 线程模型（D-002）

```text
 main 线程 = base loop                   IO 线程 0..N-1 = IO loop
 ┌─────────────────────────┐              ┌────────────────────────────────┐
 │ Acceptor(设备端口)        │  新连接轮询   │ 设备连接、订阅者连接              │
 │ Acceptor(订阅端口)        │ ───────────▶ │ Shard i：                       │
 │ SignalWatcher(signalfd)  │              │   订阅者表、时间轮、计数器          │
 │ 统计日志定时器             │              │   心跳定时器、时间轮 tick          │
 └─────────────────────────┘              └────────────────────────────────┘
```

- 一条连接的所有状态只在它所属的 IO 线程里读写，连接本身不加锁。
- 跨线程共享的可变状态只有两个：设备 ID 注册表（互斥锁）和计数器（单写者原子变量，读统计时汇总）。
- `EventLoop::queue_in_loop` 和 `quit` 在锁内写 eventfd，`~EventLoop` 先拿一次锁，保证别的线程离开后才析构（bugs.md B-002）。

## 5. 一个 Sample 的路径

```text
设备 socket 可读（IO 线程 j）
 → TcpConnection::handle_read：readv 进 input Buffer
 → Gateway::on_device_message
     循环 StreamParser::parse(buffer.peek(), readable)
       合法 Sample：首个 Sample 绑定设备 ID（D-009），把整帧字节追加进 Batch
       合法 Heartbeat：只计数
       buffer.retrieve(consumed)       —— 只移动读指针，不搬内存
     有合法帧 → 时间轮续期（D-005）
 → Hub::broadcast(Shard j, batch)
     Shard j 有订阅者 → 直接 deliver
     Shard k 有订阅者 → loop k queue_in_loop(deliver)，每个线程一次
 → Shard::deliver：对每个订阅者检查背压（D-003），然后 TcpConnection::send
```

帧从设备到订阅者只拷贝一次（从 input Buffer 拷进 Batch），之后在所有线程间共享同一份字节。

## 6. 连接生命周期

```text
accept → TcpServer::new_connection（base 线程）
       → io_loop->run_in_loop(connection_established)：注册 Channel，回调 up
对端关闭 / 出错 / force_close
       → handle_close（IO 线程）：状态置 Disconnected，回调 down，再回调 close
       → TcpServer::remove_connection：queue_in_loop(connection_destroyed) 注销 Channel；
         只持有服务器内部 Core 的 weak_ptr，服务器已析构时不会访问悬空的 this
```

`TcpConnection` 总是由 `shared_ptr` 持有：关闭发生在 IO 线程的回调里，而服务器、业务层、排队的任务可能还在别处引用它。

## 7. 关闭顺序

收到 SIGINT/SIGTERM：

1. `SignalWatcher` 回调 `loop.quit()`，base loop 返回。
2. `~Gateway`：`Hub::stop()` 停止跨线程转发 → 每个 Shard 在自己的线程里同步停止定时器（同时是屏障）。
3. 两个 `TcpServer` 析构：关闭监听 socket，给每条连接投递 `connection_destroyed`。
4. 线程池析构：逐个 quit + join；每个 loop 退出前把排队的任务执行完。
5. `Hub` 最后析构（它被排队的转发任务引用）。

`Gateway` 的成员声明顺序就是按这个析构顺序排的（gateway.h 里有注释）。
