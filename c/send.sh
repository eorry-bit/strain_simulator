#!/usr/bin/env bash
# 用 C publisher 生成 protobuf payload，通过 mosquitto_pub 发出去。
#
# 用法: ./send.sh [publisher args...]
#   常用:
#     ./send.sh                     # 正常消息（有外层信封，全字段）
#     ./send.sh --skip-wrapper      # 复现"厂家漏外层"的 bug
#     ./send.sh --no-mode --no-status   # 模拟只发部分 optional
set -euo pipefail

HOST="${MQTT_HOST:-gw.mqtt.inteagle.com}"
PORT="${MQTT_PORT:-1883}"
TOKEN="${MQTT_TOKEN:-2rdavok650n1c9o6xyai}"
SN="${MQTT_SN:-2rdavok650n1c9o6xyai}"
TOPIC="${MQTT_TOPIC:-strain/${SN}/telemetry}"

cd "$(dirname "$0")"
[ -x ./publisher ] || make

echo "[send.sh] host=$HOST topic=$TOPIC" >&2
./publisher "$@" | mosquitto_pub -h "$HOST" -p "$PORT" -u "$TOKEN" -t "$TOPIC" -q 1 -s
echo "[send.sh] done" >&2
