# Strain 设备 Protobuf 参考实现（C / nanopb）

这个目录是一份 **可编译、可运行** 的 C 参考设备模拟器。已在接收端（我们的 MQTT broker）验证过**所有字段都能被正确接收**（`timestamp / ch_no / sample_interval_ms / values / temperature / battery / mode / status`）。

---

## TL;DR：问题定性

我们对比了你们设备发来的消息和本参考实现发出去的消息，结论是：**wire 格式、schema、broker 链路都是对的，差别只在 C 代码里有没有给字段赋值**。

你们现在发的消息接收端解出是这样：

```json
{ "timestamp":"...", "chNo":0, "sampleIntervalMs":0, "values":[...], "temperature":0.0, "battery":11, "status":0 }
```

本参考实现发出去接收端解出是这样：

```json
{ "timestamp":"...", "chNo":4, "sampleIntervalMs":10, "values":[...], "temperature":24.7, "battery":89, "mode":1, "status":0 }
```

关键区别：**`chNo=0, sampleIntervalMs=0, mode 缺失`**。

---

## 为什么你们的 chNo 和 sampleIntervalMs 都是 0

这**不是丢失**，而是**没有发**。

**proto3 规则**（关键）：

> 非 `optional` 的标量字段，**值等于默认值（数字为 0、字符串为空）时，protobuf 不会把它写入 wire**。接收端解码时如果字段缺失，就用默认值填，所以看起来像 `0`。

`ch_no` 和 `sample_interval_ms` 在我们的 `.proto` 里不是 optional，所以：

- 如果你们 C 代码里这两个字段没有赋值（默认 0）→ **根本不会发送这两个字段**
- 接收端解码时找不到这两个字段 → 展示成 `chNo:0, sampleIntervalMs:0`

**修复方法就一句话：在 encode 之前给它们赋非零值。**

---

## Wire hex 对照（铁证）

对照 `sample_good.bin`（本参考发出）和 `sample_vendor_like.bin`（复现你们的消息）：

```
# sample_good.bin  (正确)
0a 99 01 08 c9 c6 e7 fe da 33 10 04 18 0a 22 80 01 ...
^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^ ^^^^^ ^^^^^ ^^^^^^^^
外层信封  field 1 timestamp    ch_no sample  values
          (varint)             = 4    = 10

# sample_vendor_like.bin  (你们当前)
0a 93 01 08 cb c6 e7 fe da 33 22 80 01 ...
^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^
外层信封  field 1 timestamp    values（直接跳过）
```

`sample_vendor_like.bin` 里**明显少了 `10 04` 和 `18 0a`** 这两段——这就是"没赋值"在 wire 上的表现。

---

## 修改你们 C 代码只需要 3 行

以 nanopb 为例，encode 之前：

```c
/* 1) 外层信封必须标记存在（nanopb 常见遗漏） */
msg.has_strainBatch = true;

/* 2) 关键：给 ch_no 和 sample_interval_ms 赋真值 */
msg.strainBatch.ch_no              = 4;      // 你们实际通道号
msg.strainBatch.sample_interval_ms = 10;     // 实际采样间隔 ms

/* 3) optional 字段要同时置 has_xxx */
msg.strainBatch.has_mode   = true;  msg.strainBatch.mode   = 1;
msg.strainBatch.has_status = true;  msg.strainBatch.status = 0;

/* 4) 温度没读到就不要置 has_temperature，别填 0 */
if (temp_sensor_ok) {
    msg.strainBatch.has_temperature = true;
    msg.strainBatch.temperature     = read_temp();
}
/* 否则保持 has_temperature = false，这样接收端就不会展示 temperature:0.0 */
```

---

## 怎么跑这份参考实现

两个可执行文件：

| 可执行 | 作用 | 依赖 |
|---|---|---|
| `publisher_mqtt` | **推荐**。单体，内置 MQTT 客户端，直接发到 broker。跨 Windows / Linux | 仅 gcc / MSVC / MinGW |
| `publisher`      | 只输出 protobuf 字节到 stdout，配合 `send.sh` 走 `mosquitto_pub` | gcc + `mosquitto-clients` |

### 编译（Linux / macOS）

```bash
cd c
make
# 生成 publisher 和 publisher_mqtt
```

### 编译（Windows）

#### MinGW (推荐，命令和 Linux 一致)

```cmd
cd c
mingw32-make
:: 生成 publisher.exe 和 publisher_mqtt.exe
```

如果没有 `mingw32-make`，也可以直接命令行手动编译 `publisher_mqtt.exe`：

```cmd
gcc -O2 -Wall -std=c11 -I. -Inanopb_runtime -DPB_ENABLE_MALLOC=0 ^
    publisher_mqtt.c mqtt_mini.c strain.pb.c ^
    nanopb_runtime\pb_common.c nanopb_runtime\pb_encode.c nanopb_runtime\pb_decode.c ^
    -lm -lws2_32 -o publisher_mqtt.exe
```

#### MSVC (Visual Studio 开发者命令行)

```cmd
cd c
cl /O2 /I. /Inanopb_runtime /DPB_ENABLE_MALLOC=0 ^
   publisher_mqtt.c mqtt_mini.c strain.pb.c ^
   nanopb_runtime\pb_common.c nanopb_runtime\pb_encode.c nanopb_runtime\pb_decode.c ^
   /link ws2_32.lib
```

### 运行：发送正常消息

```bash
./publisher_mqtt --count 3
# 默认连 gw.mqtt.inteagle.com:1883, topic=strain/<token>/telemetry
```

常用参数：

```bash
./publisher_mqtt --host <broker> --port 1883 \
                 --token <access_token> --sn <device_sn> \
                 --ch-no 4 --interval-ms 10 --values 32 \
                 --count 10 --sleep-ms 1000
```

### 运行：复现各种错误场景（诊断用）

```bash
./publisher_mqtt --skip-meta       # 不填 ch_no/sample_interval_ms -> 接收端看到 0
./publisher_mqtt --skip-wrapper    # 漏掉 has_strainBatch -> 接收端完全解不出
./publisher_mqtt --no-mode         # 漏置 has_mode -> 接收端看不到 mode 字段
./publisher_mqtt --no-temp         # 温度没接就别发，避免接收端显示 0.0
```

### 本地看字节（不发网络）

```bash
./publisher | xxd -g1 -c 32 | head
# 第一行应以 0a 开头（外层信封正确）
```

也可以把字节导成文件对比：

```bash
./publisher > my_test.bin
xxd -g1 -c 32 my_test.bin | head -5
```

### 用 send.sh + mosquitto_pub（旧流程，Linux 可用）

```bash
./send.sh                   # 等价于 publisher_mqtt 默认参数
./send.sh --skip-meta       # 传给 publisher 的参数
# ...
```

需先 `sudo apt install mosquitto-clients`。

---

## 目录内容

```
c/
├── README.md                       ← 本文件
├── publisher_mqtt.c                ← 【推荐】单体 MQTT publisher
├── publisher.c                     ← 纯 protobuf 编码器（输出到 stdout）
├── mqtt_mini.c / mqtt_mini.h       ← 内置迷你 MQTT 客户端（Winsock/POSIX 兼容）
├── strain.pb.c / strain.pb.h       ← nanopb 生成（已含，勿手改）
├── strain.options                  ← nanopb 生成选项（values max_count:64）
├── Makefile                        ← 自动识别 Windows/Unix
├── send.sh                         ← 用 mosquitto_pub 的旧脚本（可选）
├── nanopb_runtime/                 ← nanopb 0.4.9.1 runtime（自包含）
│   ├── pb.h, pb_common.c/h, pb_encode.c/h, pb_decode.c/h
└── 样本 payload（可直接二进制 diff）:
    ├── sample_good.bin         （所有字段齐全，156 bytes）
    ├── sample_vendor_like.bin  （复现你们当前的消息，缺 ch_no/sample_interval_ms）
    └── sample_bug_no_wrapper.bin（极端错误：完全没套外层）
```

上一级目录还有：

- `../strain.proto` —— 权威 schema，用它 `protoc` 重新生成代码应和 `strain.pb.h` 一致
- `../python/` —— Python 参考实现（publisher / subscriber，可和 C 版对照）

---

## 你们改完后如何自查

1. 跑你们自己的 publisher 一次，把字节 dump 出来：
   ```c
   // 在 pb_encode 之后、发送之前加：
   fprintf(stderr, "len=%zu, first byte=0x%02x\n", stream.bytes_written, buf[0]);
   for (size_t i = 0; i < stream.bytes_written; i++) fprintf(stderr, "%02x", buf[i]);
   ```
2. 对照 `sample_good.bin`：
   - 第一个字节应是 `0x0a`（外层信封存在）
   - 头 30 个字节里应该能找到 `10 xx`（ch_no）和 `18 xx`（sample_interval_ms）
   - 若这两个 tag 都没有 → 还是没赋值；都有 → wire 对了
3. 在我们订阅器看解出来的 JSON，确认：
   - `chNo` 非 0
   - `sampleIntervalMs` 非 0
   - 需要的 optional 字段（mode/temperature）都在且值合理

---

如有疑问请回复并附你们的 **C 代码片段（encode 之前对 strainBatch 结构体的赋值那几行）** + **一条消息的 hex dump**，我们逐字节对照。
