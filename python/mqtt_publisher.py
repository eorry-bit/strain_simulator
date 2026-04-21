#!/usr/bin/env python3
"""模拟设备：用 access token 连接 MQTT，发送 StrainTelemetry protobuf 数据。"""

import argparse
import math
import random
import time

import paho.mqtt.client as mqtt

import strain_pb2


BROKER_HOST = "gw.mqtt.inteagle.com"
BROKER_PORT = 1883
DEFAULT_SN = "gx8ZRDRACv2W1G5PQMOB"
DEFAULT_TOKEN = "gx8ZRDRACv2W1G5PQMOB"

def build_telemetry(ch_no: int, sample_interval_ms: int, n_values: int) -> bytes:
    telem = strain_pb2.StrainTelemetry()
    b = telem.strainBatch
    b.timestamp = int(time.time() * 1000)
    b.ch_no = ch_no
    b.sample_interval_ms = sample_interval_ms
    base = random.uniform(-50.0, 50.0)
    for i in range(n_values):
        b.values.append(base + 5.0 * math.sin(i / 8.0) + random.uniform(-0.5, 0.5))
    b.temperature = round(random.uniform(20.0, 35.0), 2)
    b.battery = random.randint(60, 100)
    b.mode = 1
    b.status = 0
    return telem.SerializeToString()


def on_connect(client, userdata, flags, rc, properties=None):
    userdata["rc"] = rc
    if rc == 0:
        print(f"[+] connected as token={userdata['token'][:6]}...", flush=True)
    else:
        print(f"[!] connect failed rc={rc}", flush=True)


def on_publish(client, userdata, mid, *args, **kwargs):
    userdata["acked"].add(mid)
    print(f"[+] puback mid={mid}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=BROKER_HOST)
    ap.add_argument("--port", type=int, default=BROKER_PORT)
    ap.add_argument("--token", default=DEFAULT_TOKEN, help="access token (as MQTT username)")
    ap.add_argument("--sn", default=DEFAULT_SN)
    ap.add_argument("--topic", default=None, help="override topic (default: strain/<sn>/telemetry)")
    ap.add_argument("--count", type=int, default=3, help="number of messages to publish")
    ap.add_argument("--interval", type=float, default=1.0, help="seconds between messages")
    ap.add_argument("--ch-no", type=int, default=4)
    ap.add_argument("--sample-interval-ms", type=int, default=10)
    ap.add_argument("-n", "--values", type=int, default=32, help="values per batch")
    ap.add_argument("--qos", type=int, default=1)
    args = ap.parse_args()

    topic = args.topic or f"strain/{args.sn}/telemetry"

    userdata = {"token": args.token, "rc": None, "acked": set()}
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                             client_id=f"strain-pub-{int(time.time())}",
                             userdata=userdata)
    except AttributeError:
        client = mqtt.Client(client_id=f"strain-pub-{int(time.time())}", userdata=userdata)

    client.username_pw_set(args.token)
    client.on_connect = on_connect
    client.on_publish = on_publish

    print(f"[*] connecting {args.host}:{args.port}  topic={topic}", flush=True)
    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()

    for _ in range(1):
        if userdata["rc"] is not None:
            break
        time.sleep(0.1)
    if userdata["rc"] != 0:
        print("[!] giving up; connect failed", flush=True)
        client.loop_stop()
        return 1

    for i in range(args.count):
        payload = build_telemetry(args.ch_no, args.sample_interval_ms, args.values)
        info = client.publish(topic, payload, qos=args.qos)
        print(f"[>] #{i+1} mid={info.mid}  {len(payload)} bytes", flush=True)
        info.wait_for_publish(timeout=5)
        if i < args.count - 1:
            time.sleep(args.interval)

    time.sleep(1)
    client.loop_stop()
    client.disconnect()
    print(f"[*] done. acked={len(userdata['acked'])}/{args.count}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
