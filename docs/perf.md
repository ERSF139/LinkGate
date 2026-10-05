# 性能测试

## 1. 方法

- 网关用 `release` 预设（`RelWithDebInfo`：`-O2 -g`，带符号，perf 能看到函数名）。
- 压测工具 `loadgen` 同时扮演设备和订阅者：
  - 每台模拟设备一条连接，按固定频率发 Sample；
  - 订阅者统计收到的样本数、丢失率，以及端到端延迟的 P50 / P90 / P99 / P99.9 / 最大值。
- 延迟测量：设备把发送时刻（`steady_clock` 纳秒）写进 Sample 的 `vibration` 字段，订阅者收到后用当前时刻相减。设备和订阅者在同一进程里，时钟一致。这只是压测约定。
- 设备连接按 `--ramp` 的速率逐批建立；全部建立完成后先预热 `--warmup` 秒，再统计 `--duration` 秒。
- 每组参数至少跑 3 次取中位数，并记录机器配置、内核版本、网关和 loadgen 是否同机。

## 2. 准备

```bash
ulimit -n 65535                 # 每个 shell 都要设置；网关和 loadgen 启动时也会尝试自己提升
sudo scripts/sysctl-tuning.sh --apply
cmake --preset release && cmake --build --preset release -j
```

## 3. 运行

单次：

```bash
./build/release/gateway/linkgate --io-threads=4 &
./build/release/tools/loadgen --devices=10000 --rate=1 --subscribers=1 --threads=2 --duration=30
```

IO 线程扩展曲线（自动遍历 1、2、4 个 IO 线程）：

```bash
scripts/run_loadtest.sh 10000 1       # 10000 台设备，每台 1 Hz
scripts/run_loadtest.sh 5000 10       # 5000 台设备，每台 10 Hz
```

热点分析：

```bash
perf record -g -p $(pgrep -x linkgate) -- sleep 15
perf report --no-children
```

## 4. 结果

写进简历的数字必须来自这里。

机器：VMware 虚拟机，CentOS Stream 10，2 vCPU，3.5 GiB 内存，GCC 14.4，`release` 预设。网关与 loadgen 同机，loadgen 2 个线程、1 个订阅者。网关 CPU 是 `ps` 给出的整轮平均值（含建连阶段），内存是常驻内存 RSS。

### 4.1 连接规模基线：5000 台设备 × 1 Hz（2026-10-05）

| IO 线程 | 输入 样本/s | 输出 样本/s | 丢失率 | P50 µs | P90 µs | P99 µs | P99.9 µs | 网关 CPU | 网关 RSS |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 5000 | 5000 | 0% | 211 | 503 | 1465 | 3122 | 4.9% | 21 MB |
| 2 | 5000 | 5000 | 0% | 241 | 654 | 1663 | 2459 | 6.1% | 22 MB |
| 4 | 5000 | 5000 | 0% | 229 | 663 | 1608 | 2502 | 7.4% | 22 MB |

结论：5000 条长连接、每秒 5000 帧远未到网关的上限，吞吐由负载决定，三种线程数没有差别。机器只有 2 个核且 loadgen 同机占用 2 个线程，轻负载下多开 IO 线程只增加跨线程唤醒的开销，CPU 反而略高。

### 4.2 吞吐上限（待测）

`scripts/run_loadtest.sh 2000 <10|25|50|100> 15`，找出开始丢包或 P99 明显上升的那一档。

| 设备数 × 频率 | IO 线程 | 输入 样本/s | 丢失率 | P50 µs | P99 µs | 网关 CPU |
|---|---|---|---|---|---|---|
| | | | | | | |

### 4.3 连接数上限（待测）

`scripts/run_loadtest.sh 20000 1 15`（内存不够时改 10000）。

| 设备数 | IO 线程 | 连上的设备 | 丢失率 | P99 µs | 网关 RSS |
|---|---|---|---|---|---|
| | | | | | |

### 参考：开发环境冒烟测试（不可直接引用）

在 4 vCPU 的云端开发虚拟机上，网关和 loadgen 同机运行：5000 台设备 × 10 Hz，2 个订阅者，网关 4 个 IO 线程，loadgen 2 个线程。输入约 5 万样本/秒、输出约 10 万样本/秒，丢失率 0%，P50 约 0.5 ms，P99 约 1.6 ms。这组数字只用来确认工具链正常，换一台机器结果会完全不同。

## 5. 已知的优化方向

`bench` 预设的微基准（`./build/bench/bench/devproto_bench`）显示：按位计算的 CRC-16 吞吐约 115 MiB/s，而流式解析的吞吐和它几乎相同——解析器的时间基本都花在 CRC 上。256 项查表法通常能快好几倍。

这是一个完整的优化练习：先用 perf 在网关上确认 `crc16_modbus` 是热点，再改成查表法（`crc16_test.cpp` 的三组测试向量保证结果不变），最后记录优化前后的微基准和压测数据。
