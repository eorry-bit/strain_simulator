#!/usr/bin/env python3
"""订阅 strain/<sn>/telemetry，解析 StrainTelemetry protobuf 消息。"""

import argparse
import datetime as dt
import json
import sys

import paho.mqtt.client as mqtt
from google.protobuf.json_format import MessageToDict

import strain_pb2


BROKER_HOST = "120.79.219.96"
BROKER_PORT = 1883
DEFAULT_TOPIC = "strain/0C73B34565FC51CE/telemetry"


def format_ts(ms: int) -> str:
    try:
        return dt.datetime.fromtimestamp(ms / 1000).isoformat(timespec="milliseconds")
    except (OverflowError, OSError, ValueError):
        return str(ms)


def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        topic = userdata["topic"]
        print(f"[+] connected, subscribing {topic}", flush=True)
        client.subscribe(topic, qos=0)
    else:
        print(f"[!] connect failed rc={rc}", file=sys.stderr, flush=True)


def on_disconnect(client, userdata, *args, **kwargs):
    print(f"[!] disconnected args={args}", file=sys.stderr, flush=True)


def on_subscribe(client, userdata, mid, reason_codes, properties=None):
    print(f"[+] subscribed mid={mid} codes={reason_codes}", flush=True)


def on_log(client, userdata, level, buf):
    print(f"[log] {buf}", flush=True)


def on_message(client, userdata, msg):
    payload = msg.payload
    print(f"\n[>] {msg.topic}  {len(payload)} bytes")
    print(f"    hex[0:64]: {payload[:64].hex()}{'...' if len(payload) > 64 else ''}")
    if payload:
        first = payload[0]
        field_num, wire = first >> 3, first & 7
        wire_name = {0: "varint", 1: "64bit", 2: "len-delim", 5: "32bit"}.get(wire, f"wire={wire}")
        expect = "0a (field=1 len-delim, StrainBatch wrapper)"
        print(f"    first byte: 0x{first:02x}  ->  field={field_num} {wire_name}   expected {expect}")

    telem = strain_pb2.StrainTelemetry()
    parse_err = None
    try:
        telem.ParseFromString(payload)
    except Exception as e:
        parse_err = e

    # 若异常或信封为空，尝试直接按 StrainBatch 解（厂家常见 bug：漏外层 has_strainBatch）
    if parse_err is not None or not telem.HasField("strainBatch"):
        reason = parse_err or "StrainTelemetry.strainBatch 为空（很可能厂家漏了外层信封）"
        print(f"[!] outer parse not usable: {reason}")
        print(f"    fallback: try parsing payload as bare StrainBatch...")
        b_only = strain_pb2.StrainTelemetry.StrainBatch()
        try:
            b_only.ParseFromString(payload)
            print("    [OK] parsed as bare StrainBatch:")
            print(json.dumps(MessageToDict(b_only, preserving_proto_field_name=True),
                             ensure_ascii=False, indent=2))
        except Exception as e2:
            print(f"    [X] also failed as StrainBatch: {e2}")
        return

    b = telem.strainBatch
    missing = []
    for f in b.DESCRIPTOR.fields:
        if f.is_repeated:
            if len(getattr(b, f.name)) == 0:
                missing.append(f.name)
            continue
        if f.has_presence:
            if not b.HasField(f.name):
                missing.append(f.name)
        else:
            if getattr(b, f.name) == f.default_value:
                missing.append(f.name)

    summary = {
        "timestamp": f"{b.timestamp} ({format_ts(b.timestamp)})",
        "ch_no": b.ch_no,
        "sample_interval_ms": b.sample_interval_ms,
        "values_count": len(b.values),
        "values_preview": list(b.values[:8]) + (["..."] if len(b.values) > 8 else []),
    }
    for optional in ("temperature", "battery", "mode", "status"):
        if b.HasField(optional):
            summary[optional] = getattr(b, optional)

    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if missing:
        print(f"[!] fields NOT present / default-valued: {missing}")

    if userdata.get("dump_full"):
        print("-- full message --")
        print(json.dumps(MessageToDict(telem, preserving_proto_field_name=True),
                         ensure_ascii=False, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--topic", default=DEFAULT_TOPIC)
    parser.add_argument("--host", default=BROKER_HOST)
    parser.add_argument("--port", type=int, default=BROKER_PORT)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--full", action="store_true", help="print full decoded message")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    try:
        client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"strain-sub-{dt.datetime.now():%H%M%S}",
            userdata={"topic": args.topic, "dump_full": args.full},
        )
    except AttributeError:
        client = mqtt.Client(
            client_id=f"strain-sub-{dt.datetime.now():%H%M%S}",
            userdata={"topic": args.topic, "dump_full": args.full},
        )

    if args.username:
        client.username_pw_set(args.username, args.password)

    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.on_subscribe = on_subscribe
    if args.verbose:
        client.on_log = on_log

    client.connect(args.host, args.port, keepalive=30)
    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\n[+] bye")
        client.disconnect()


if __name__ == "__main__":
    main()
