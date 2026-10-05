# 测试和工具抓到的问题

每条记录：怎么发现的、现象、根因、修复、用来防止回归的测试。

---

## B-001 一个比高水位还大的批次，让积压为 0 的订阅者也被判为"过慢"

- **发现方式**：网关集成测试 `Gateway.SlowSubscriberIsPausedThenDisconnected` 失败——订阅者反复"暂停—恢复"，停滞超时永远不触发。日志里出现 `backlog 0 bytes exceeds high water`。
- **根因**：背压判断写成了 `积压 + 本批大小 > 高水位`。设备一次读事件最多能解析出 64 KiB 以上的 Sample，测试把高水位设成 64 KiB，于是每个批次本身就超过高水位：订阅者被暂停 → 下一次投递时积压是 0，恢复 → 新批次又超过高水位，再次暂停。数据一条都没发出去，停滞计时也一直被重置。
- **修复**：只看已经积压的字节 `积压 >= 高水位`。输出缓冲的上限因此变成"高水位 + 一个批次"（D-003 的代价里写明了）。
- **回归测试**：`Gateway.SlowSubscriberIsPausedThenDisconnected`。

## B-002 `EventLoop::quit()` 在 loop 已经析构之后还在写它的 eventfd

- **发现方式**：TSan 构建（`ctest --preset tsan`）在 `Gateway.SingleThreadModeWorks` 报 data race：主线程在 `EventLoop::wakeup()` 里读 `wakeup_fd_`，网关线程同时在 `~EventLoop` 里关闭它。
- **根因**：`quit()` 先把 `quit_` 置为 true，再写 eventfd 唤醒。loop 线程可能恰好在这两步之间醒来（被别的事件唤醒），看到 `quit_` 后立刻退出 `loop()`、析构 `EventLoop`；调用 `quit()` 的线程随后写的是一个已经关闭（甚至可能已被复用）的 fd。`queue_in_loop()` 在锁外唤醒也有同样的问题。
- **修复**：`quit()` 和 `queue_in_loop()` 都在持有 `mutex_` 的情况下完成唤醒；`~EventLoop` 第一件事是拿一次 `mutex_`，保证所有正在唤醒它的线程都已经离开。
- **回归测试**：`ctest --preset tsan` 下的整个网关测试集（每个测试都会从测试线程 `quit()` 网关线程上的 loop）。

## B-003 关闭时 IO 线程往已经退出的 loop 投递转发任务

- **发现方式**：修 B-002 时代码审查发现的同类问题，目前没有测试能稳定复现。
- **根因**：线程池析构时逐个 quit + join。loop 0 已经退出并析构时，loop 1 可能还在处理某台设备的数据，`Hub::broadcast` 会往 loop 0 `queue_in_loop`。
- **修复**：`~Gateway` 先调用 `Hub::stop()`（之后 `broadcast` 和设备消息处理都直接返回），再在每个 IO loop 上 `run_in_loop_sync` 一次作为屏障，确保读到旧值、正在投递的线程都已结束，然后才析构服务器和线程池。
- **回归测试**：暂无确定性测试；`ctest --preset tsan` 的网关测试覆盖了关闭路径。

## B-004 `force_close` 可能把已经关闭的连接状态改回 Disconnecting

- **发现方式**：代码审查。
- **根因**：最初的写法是"读状态 → 如果是 Connected 就写 Disconnecting"。如果另一个线程调用 `force_close` 的同时，IO 线程里 `handle_close` 已经把状态写成 Disconnected，这次写入会把它改回 Disconnecting，随后排队的关闭任务会再执行一次 `handle_close`，回调被调用两次。
- **修复**：用 `compare_exchange` 只允许 `Connected → Disconnecting` 这一个转换。
- **回归测试**：`TcpServer.ForceCloseFromAnotherThread`。

---

## 模板

```markdown
## B-00x 一句话描述现象

- **发现方式**：哪个测试 / 哪个工具（TSan、ASan、libFuzzer、差分测试、压测）
- **根因**：
- **修复**：
- **回归测试**：
```
