# Strain Simulator

应变（Strain）设备 MQTT + Protobuf 发送/订阅模拟器，包含 **Python** 和 **C (nanopb)** 两份参考实现，以及一个加强版订阅器用于字段诊断。

主要用途：

1. **模拟真实设备** 向 MQTT broker 发送 `StrainTelemetry` 遥测（供后端接收验证）

---

## 目录结构

```
strain_simulator/
├── README.md              ← 本文件
├── strain.proto           ← 权威 schema（所有语言的生成源头）
│
├── python/                ← Python 参考实现
│   ├── mqtt_publisher.py       模拟设备发送（token 认证 → MQTT broker）
│   ├── mqtt_subscriber.py      增强版订阅器（hex 打印、缺字段清单、漏外层 fallback）
│   ├── strain_pb2.py           protoc 生成
│   └── dump_raw.py
│
└── c/                     ← C 参考实现（nanopb）
    ├── README.md               ← 给厂家看的详细说明（问题定性 + hex 对照 + 修复代码）
    ├── publisher.c             ← 主程序
    ├── strain.pb.c/.h          ← nanopb 生成
    ├── strain.options          ← values max_count:64
    ├── Makefile
    ├── send.sh                 ← 用 mosquitto_pub 发送
    ├── nanopb_runtime/         ← nanopb 0.4.9.1 runtime（7 个文件，自包含）
    └── sample_*.bin            ← 三份对照 payload
        ├── sample_good.bin             正常消息
        ├── sample_vendor_like.bin      复现厂家当前问题（漏 ch_no/sample_interval_ms）
        └── sample_bug_no_wrapper.bin   极端错误（漏外层 StrainTelemetry 信封）
```

---

## 快速开始

### Python 端

```bash
# 1) 准备环境
python3 -m venv .venv
source .venv/bin/activate
pip install paho-mqtt protobuf

# 2) 发送（默认 broker/token 在脚本里，按需改）
python python/mqtt_publisher.py --count 3

# 3) 订阅（另一个终端）
python python/mqtt_subscriber.py --topic 'strain/+/telemetry' --full
```

### C 端

```bash
cd c
make
./publisher | xxd -g1 -c 32 | head   # 本地看字节，首字节应为 0x0a
./send.sh                            # 通过 MQTT 发出去
```

更多选项见 `c/README.md`（里面的 hex 对照和问题排查流程可以**直接发给设备厂家**）。

---

## 调试开关一览（`c/publisher`）

| 开关 | 作用 | 复现场景 |
|---|---|---|
| （无） | 正常全字段 | ground truth |
| `--skip-meta` | 不填 `ch_no / sample_interval_ms` | 厂家最常见 bug——接收端看到 `chNo:0, sampleIntervalMs:0` |
| `--skip-wrapper` | 不套外层 `StrainTelemetry` 信封 | nanopb 最常见漏掉 `has_strainBatch = true` |
| `--no-temp` | 不置 `has_temperature` | optional 字段不该发的正确姿势 |
| `--no-mode` / `--no-battery` / `--no-status` | 同上 | |

---

## proto3 关键坑（给排查用）

**非 `optional` 的标量字段，值为默认值（0 / 空字符串）时不会写入 wire。**

所以设备端如果忘了给 `ch_no` 和 `sample_interval_ms` 赋值，这两个字段根本不会出现在网络字节流里，接收端解码拿到默认值 `0`——看起来是"收到 0"，实际是"根本没发"。修复就是在 encode 前赋非零值。

**optional 字段必须配套置 `has_xxx = true`，否则 nanopb 不会发。**

**子消息必须置 `has_<submsg> = true`**（如 `has_strainBatch = 1`）——这是 nanopb 最容易漏掉的一行。

---

## 重新生成 protobuf 代码

改动 `strain.proto` 之后：

```bash
# Python
protoc --python_out=python/ strain.proto

# C (nanopb)
cd c
../.venv/bin/python -m nanopb.generator.nanopb_generator \
    --output-dir=. --options-file=strain.options \
    -I.. ../strain.proto
```

---
