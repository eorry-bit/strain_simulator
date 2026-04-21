#!/usr/bin/env python3
"""抓一条原始 payload，列出所有 protobuf 字段（不依赖 schema）。"""

import argparse
import struct

import paho.mqtt.client as mqtt


WIRE_TYPES = {0: "varint", 1: "fixed64", 2: "len", 5: "fixed32"}


def read_varint(buf, i):
    shift = 0
    result = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, i
        shift += 7


def decode_fields(buf, depth=0):
    """yield (field_no, wire_type, value, raw_bytes) recursively for len fields."""
    i = 0
    pad = "  " * depth
    while i < len(buf):
        tag, i = read_varint(buf, i)
        field_no = tag >> 3
        wt = tag & 7
        if wt == 0:
            v, i = read_varint(buf, i)
            print(f"{pad}field {field_no} varint  = {v}")
        elif wt == 1:
            v = struct.unpack_from("<Q", buf, i)[0]
            fv = struct.unpack_from("<d", buf, i)[0]
            print(f"{pad}field {field_no} fixed64 = {v} (double={fv})")
            i += 8
        elif wt == 2:
            ln, i = read_varint(buf, i)
            chunk = buf[i:i + ln]
            i += ln
            # try as sub-message
            try:
                print(f"{pad}field {field_no} len({ln}) -- trying as submessage:")
                decode_fields(chunk, depth + 1)
            except Exception:
                print(f"{pad}field {field_no} len({ln}) raw={chunk.hex()}")
                try:
                    print(f"{pad}  utf8: {chunk.decode('utf-8')}")
                except UnicodeDecodeError:
                    # maybe packed floats
                    if ln % 4 == 0:
                        floats = struct.unpack(f"<{ln // 4}f", chunk)
                        print(f"{pad}  as floats[{ln // 4}]: {floats[:6]}...")
        elif wt == 5:
            v = struct.unpack_from("<I", buf, i)[0]
            fv = struct.unpack_from("<f", buf, i)[0]
            print(f"{pad}field {field_no} fixed32 = {v} (float={fv})")
            i += 4
        else:
            print(f"{pad}field {field_no} unknown wiretype {wt}")
            return


def on_message(client, userdata, msg):
    print(f"\n=== {msg.topic}  {len(msg.payload)} bytes ===")
    print(f"hex: {msg.payload.hex()}")
    print("--- fields ---")
    try:
        decode_fields(msg.payload)
    except Exception as e:
        print(f"decode error: {e}")
    userdata["count"] += 1
    if userdata["count"] >= userdata["max"]:
        client.disconnect()


def on_connect(c, u, f, rc, p=None):
    c.subscribe(u["topic"], qos=0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="47.100.64.192")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--username", default="jbtest")
    ap.add_argument("--password", default="test")
    ap.add_argument("--topic", default="strain/0C73B34565FC51CE/telemetry")
    ap.add_argument("-n", type=int, default=2)
    args = ap.parse_args()

    try:
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                        userdata={"topic": args.topic, "count": 0, "max": args.n})
    except AttributeError:
        c = mqtt.Client(userdata={"topic": args.topic, "count": 0, "max": args.n})
    c.username_pw_set(args.username, args.password)
    c.on_connect = on_connect
    c.on_message = on_message
    c.connect(args.host, args.port, 30)
    c.loop_forever()


if __name__ == "__main__":
    main()
