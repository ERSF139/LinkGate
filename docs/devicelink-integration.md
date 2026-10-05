# 接入 DeviceLink

LinkGate 不需要修改 DeviceLink 客户端：客户端照常连接 `网关地址:9000`，收到的就是网关转发的原始帧和网关自己的心跳。需要改的只有模拟器（让设备主动连网关）；另外附带一个解析器修复。

## 1. 两个补丁

补丁在 `integration/devicelink/`，在 DeviceLink 仓库里用 `git am` 应用，会生成两个以你的名义署名的提交：

```bash
cd <DeviceLink 目录>
git am <LinkGate 目录>/integration/devicelink/*.patch
```

| 补丁 | 内容 |
|---|---|
| `0001-fix-common-reject-fake-frame-headers-...` | `FrameParser` 读完 6 字节帧头就检查版本、类型和"长度是否与类型匹配"，假帧头不再让解析器等上最多 4104 字节；新增 2 个 Qt Test 用例 |
| `0002-feat-simulator-add-LinkGate-gateway-mode` | 模拟器新增"LinkGate 网关"分组：每台设备一条连接连到网关的设备端口，每秒心跳，断线指数退避重连；原来的监听模式不变 |

两个补丁都在 Qt 6 下编译通过，DeviceLink 原有的 5 个测试程序和新增用例全部通过；模拟器的网关模式已经和 `linkgate` 端到端联调过（5 台设备的采样全部到达订阅者）。

## 2. 联调步骤（CentOS 虚拟机）

```bash
# 终端 1：网关
cd ~/code/LinkGate
cmake --preset release && cmake --build --preset release -j
./build/release/gateway/linkgate --io-threads=2

# 终端 2：DeviceLink（已应用补丁）
cd <DeviceLink 目录>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/bin/simulator &        # 点"全部启动"，再在"LinkGate 网关"分组里点"连接网关"（127.0.0.1:9100）
./build/bin/client &           # 连接 127.0.0.1:9000
```

预期：

- 模拟器"已连上的设备"显示 `5 / 5`；
- 客户端表格里 5 台设备上线，曲线实时刷新，"注入异常"后告警照常触发；
- 网关日志每 10 秒输出一行统计，`samples_in` 和 `samples_forwarded` 持续增长；
- 关掉客户端再打开，客户端自己重连；关掉网关，模拟器按 1→2→4→8→16 秒退避重连。

## 3. 可以顺手录的演示

1. 先用直连模式（模拟器监听 9000，客户端直连）展示原来的拓扑。
2. 启动 `linkgate`，模拟器切到网关模式，客户端改连网关，功能完全一样。
3. 再开一个客户端，两个客户端同时显示同样的数据，但每台设备仍只有一条连接。
4. 另开终端跑 `loadgen --devices=200 --rate=1 --first-device-id=1000 --duration=20`，客户端表格里会多出 200 台设备。设备数不要开得太大：客户端会为每台设备建一行、一条曲线，界面会被拖慢；这时网关的背压（D-003）会开始丢弃发给它的数据，统计里的 `sub_dropped` 会增长，这本身也是一个可以讲的现象。

## 4. README 交叉引用

在 DeviceLink 的 README 里加一句，例如：

> 大规模部署时可以通过 [LinkGate](<LinkGate 仓库地址>) 网关接入：设备与客户端都只连网关，连接数从 N×M 降为 N+M。
