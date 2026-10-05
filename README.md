# LinkGate

LinkGate 是 [DeviceLink](https://github.com/ERSF139/DeviceLink) 的 Linux 中心接入网关。DeviceLink 采用设备端监听、客户端直连的模式（模拟器把 5 台设备放在同一个端口上）；在真实部署中，每台设备都是独立的网络端点，N 台设备、M 个客户端需要 N×M 条连接，每台设备还要给每个客户端各发一份采样和心跳。LinkGate 改为设备和客户端都只连接网关，连接数降为 N+M，并在网关上集中完成帧校验、扇出转发、设备在线判断和背压控制。

```text
设备 ×N ──▶ 9100 ┌──────────┐ 9000 ──▶ DeviceLink 客户端 ×M（无需修改）
                 │ LinkGate │
                 └──────────┘
```

纯 C++17，不依赖 Qt。网络层基于 epoll 自己实现（主从 Reactor，one loop per thread），协议层是一个零依赖、跨平台的库，与 DeviceLink 的线上格式逐字节兼容。

## 已实现

- **devproto 协议库**：与 DeviceLink 线上格式完全兼容（用 DeviceLink 导出的黄金帧逐字节验证）。无缓冲的流式解析器：不拷贝、不持有数据；读完 6 字节帧头就拒绝假帧头（版本、类型、长度与类型不匹配），等待中的字节永远少于最长合法帧 44 字节；CRC 失败按字节滑动重同步。零依赖，Windows 和 Linux 都能编译。
- **lg_net 网络库**：非阻塞 socket + epoll（LT）、`EventLoop` / `Channel` / `EpollPoller`、eventfd 跨线程唤醒、timerfd 定时器队列、应用层收发缓冲（`readv` + 栈上临时空间）、RAII 文件描述符 `UniqueFd`、`TcpServer`（fd 耗尽时用预留 fd 清理积压）、带指数退避重连的 `TcpClient`、IO 线程池。
- **网关**：设备端口和订阅端口共享一组 IO 线程，每个线程一个 Shard，一次读事件解析出的帧打成一个批次跨线程共享；订阅者高/低水位背压和停滞断开；引用计数时间轮做设备空闲检测（只有合法帧才续期，慢速发送假帧头的连接会被踢掉）；按首个 Sample 绑定设备 ID、同 ID 新连接顶掉旧连接；网关自己给订阅者发心跳；signalfd 优雅退出；按线程分片、缓存行对齐的单写者计数器。
- **质量**：75 个 GoogleTest 用例（协议、网络库、网关端到端），覆盖 Debug / ASan+UBSan / TSan 三套构建；随机性质测试 + libFuzzer 模糊测试，检查解析器的守恒与有界等待不变式；`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`。
- **压测工具**：`loadgen` 模拟上万台设备，同时作为订阅者统计吞吐、丢失率和端到端延迟分位数；`scripts/run_loadtest.sh` 输出 IO 线程扩展曲线。
- **DeviceLink 接入**：`integration/devicelink/` 下两个补丁——模拟器的网关模式（每台设备一条连接），以及把"尽早拒绝"补进 DeviceLink 自己的 `FrameParser`。

## 计划中

- 用 perf 定位热点并优化（候选：CRC 改查表法，见 docs/perf.md 第 5 节）
- 在自己的机器上完成压测，填写 docs/perf.md 的结果表

## 构建

依赖：GCC 11+ 或 Clang 14+、CMake 3.25+。GoogleTest 和 Google Benchmark 在配置时自动下载。CentOS Stream 10：

```bash
sudo dnf install -y gcc-c++ cmake git gdb libasan libubsan libtsan
```

所有配置都写在 `CMakePresets.json` 里：

| 预设 | 用途 |
|---|---|
| `debug` | 日常开发 |
| `release` | `-O2 -g`，压测和 perf |
| `asan` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `tsan` | ThreadSanitizer |
| `fuzz` | libFuzzer（需要 clang） |
| `bench` | Google Benchmark 微基准 |

```bash
cmake --preset debug
cmake --build --preset debug -j
ctest --preset debug
```

把 `debug` 换成 `asan` 或 `tsan` 即可在对应的 sanitizer 下跑全部测试。TSan 如果报 `unexpected memory mapping`，执行 `sudo sysctl vm.mmap_rnd_bits=28`。

Windows 上 `LG_BUILD_NET` 自动关闭，只编译 `devproto` 和它的测试。

## 运行

```bash
./build/release/gateway/linkgate --io-threads=4
./build/release/gateway/linkgate --help
```

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--bind` | `0.0.0.0` | 监听地址 |
| `--device-port` | 9100 | 设备端口 |
| `--subscriber-port` | 9000 | 订阅端口（DeviceLink 客户端默认就连 9000） |
| `--io-threads` | 4 | IO 线程数，0 表示单线程 |
| `--idle-timeout-ms` | 10000 | 设备这么久没发合法帧就断开 |
| `--tick-ms` | 1000 | 空闲时间轮的粒度 |
| `--heartbeat-ms` | 1000 | 给订阅者发心跳的间隔 |
| `--high-water` / `--low-water` | 4 MiB / 1 MiB | 订阅者背压的高低水位 |
| `--stall-timeout-ms` | 10000 | 订阅者连续暂停这么久就断开 |
| `--stats-interval-ms` | 10000 | 周期统计日志，0 关闭 |
| `--log-level` | `info` | `debug` / `info` / `warn` / `error` |

和 DeviceLink 联调见 [docs/devicelink-integration.md](docs/devicelink-integration.md)。

## 压测

```bash
ulimit -n 65535
sudo scripts/sysctl-tuning.sh --apply
cmake --preset release && cmake --build --preset release -j
scripts/run_loadtest.sh 10000 1        # 10000 台设备，每台 1 Hz，IO 线程 1/2/4 各跑一次
```

方法、结果表和优化方向见 [docs/perf.md](docs/perf.md)。

## 模糊测试与微基准

```bash
sudo dnf install -y clang compiler-rt          # Ubuntu：clang libclang-rt-18-dev
cmake --preset fuzz && cmake --build --preset fuzz -j
scripts/make_fuzz_corpus.py                    # 从黄金帧生成种子语料到 fuzz/corpus
./build/fuzz/fuzz/parser_fuzz -max_total_time=300 fuzz/corpus

cmake --preset bench && cmake --build --preset bench -j
./build/bench/bench/devproto_bench
```

## 目录结构

```text
LinkGate/
├── CMakeLists.txt / CMakePresets.json
├── cmake/                  Warnings（lg_warnings 接口库）、Sanitizers、Fuzzing
├── devproto/               协议库：constants、byte_order、crc16、sample、frame、parser
├── net/                    网络库：unique_fd、socket_ops、inet_address、buffer、channel、epoll_poller、
│                           event_loop、timer_queue、timing_wheel、acceptor、tcp_connection、tcp_server、
│                           connector、tcp_client、event_loop_thread、log
├── gateway/                网关：config、stats、hub（Shard/Hub）、device_registry、signal_watcher、gateway、main
├── tools/                  env_check、echo_blocking（阻塞式 echo 对照实验）、loadgen（压测）
├── tests/                  GoogleTest：devproto/、net/、gateway/，support/（黄金帧、解析不变式、阻塞客户端）
│   └── data/golden/        DeviceLink 导出的黄金帧
├── fuzz/                   libFuzzer 目标
├── bench/                  Google Benchmark 微基准
├── scripts/                sysctl-tuning、run_loadtest、make_fuzz_corpus
├── integration/devicelink/ DeviceLink 的两个补丁
└── docs/                   protocol、architecture、decisions、perf、bugs、devicelink-integration
```

## 文档

- [docs/protocol.md](docs/protocol.md)：线上格式规格，devproto 的唯一实现依据
- [docs/architecture.md](docs/architecture.md)：模块分层、线程模型、一个 Sample 的路径、关闭顺序
- [docs/decisions.md](docs/decisions.md)：设计决策记录 D-001 ~ D-009
- [docs/bugs.md](docs/bugs.md)：测试、TSan 和代码审查抓到的问题
- [docs/perf.md](docs/perf.md)：压测方法与结果
- [docs/devicelink-integration.md](docs/devicelink-integration.md)：与 DeviceLink 联调
