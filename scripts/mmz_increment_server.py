#!/usr/bin/env python3
"""Minimal platform socket server for incremental stream/MMZ testing."""

import argparse
import os
import socket
import struct
import threading
import time


MAGIC = 0xDEADBEEF
PROTO_VERSION = 11200


def varint(value):
    output = bytearray()
    while value >= 0x80:
        output.append((value & 0x7F) | 0x80)
        value >>= 7
    output.append(value)
    return bytes(output)


def int_field(number, value):
    return varint(number << 3) + varint(value)


def bytes_field(number, value):
    if isinstance(value, str):
        value = value.encode()
    return varint((number << 3) | 2) + varint(len(value)) + value


def float_field(number, value):
    return varint((number << 3) | 5) + struct.pack("<f", value)


def algorithm(package_path):
    return b"".join(
        (
            bytes_field(1, "hardhat-detection"),
            bytes_field(2, package_path),
            bytes_field(3, "mmz-test"),
            float_field(4, 0.5),
            int_field(5, 2),
        )
    )


def stream(index, rtsp_base, package_path, enabled=True):
    stream_id = "mmz-test-{:02d}".format(index)
    return b"".join(
        (
            int_field(1, int(enabled)),
            bytes_field(2, stream_id),
            bytes_field(3, "{}/test{}".format(rtsp_base.rstrip("/"), index)),
            int_field(5, 3),
            bytes_field(6, "/tmp/mmz-evidence/{}/snapshots".format(stream_id)),
            bytes_field(7, "/tmp/mmz-evidence/{}/records".format(stream_id)),
            int_field(8, 10),
            int_field(9, 30),
            bytes_field(10, algorithm(package_path)),
        )
    )


def config(config_id, stream_index, rtsp_base, package_path, enabled=True):
    return b"".join(
        (
            bytes_field(1, "agent_001"),
            int_field(2, config_id),
            bytes_field(
                3, stream(stream_index, rtsp_base, package_path, enabled)
            ),
        )
    )


def envelope(seq, config_payload):
    return b"".join(
        (
            int_field(1, PROTO_VERSION),
            int_field(2, 2),
            int_field(3, seq),
            bytes_field(11, config_payload),
        )
    )


def drain(connection):
    try:
        while connection.recv(65536):
            pass
    except OSError:
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", default="/tmp/mmz-platform.sock")
    parser.add_argument("--count", type=int, default=13)
    parser.add_argument("--interval", type=float, default=12.0)
    parser.add_argument("--hold", type=float, default=20.0)
    parser.add_argument("--config-id", type=int, default=9000)
    parser.add_argument(
        "--churn-index",
        type=int,
        default=0,
        help="disable and re-enable this stream after initial startup",
    )
    parser.add_argument("--churn-delay", type=float, default=10.0)
    parser.add_argument("--rtsp-base", default="rtsp://192.168.88.91/live")
    parser.add_argument(
        "--package",
        default=(
            "/mnt/userdata/smart-guard-edge/recordings/default/ai-models/"
            "hardhat-detection/hardhat_detect_eic7700_1_2.pkg"
        ),
    )
    args = parser.parse_args()

    if os.path.exists(args.socket):
        os.unlink(args.socket)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(args.socket)
    server.listen(1)
    print("LISTEN socket={}".format(args.socket), flush=True)
    connection, _ = server.accept()
    print("CONNECTED", flush=True)
    threading.Thread(target=drain, args=(connection,), daemon=True).start()

    for index in range(1, args.count + 1):
        payload = envelope(
            index,
            config(args.config_id + index, index, args.rtsp_base, args.package),
        )
        connection.sendall(struct.pack("!II", MAGIC, len(payload)) + payload)
        print(
            "SEND time={:.3f} config_id={} active_after={}".format(
                time.time(), args.config_id + index, index
            ),
            flush=True,
        )
        time.sleep(args.interval)

    sequence = args.count
    if args.churn_index:
        if args.churn_index < 1 or args.churn_index > args.count:
            parser.error("--churn-index must be between 1 and --count")
        for enabled in (False, True):
            time.sleep(args.churn_delay)
            sequence += 1
            config_id = args.config_id + sequence
            payload = envelope(
                sequence,
                config(
                    config_id,
                    args.churn_index,
                    args.rtsp_base,
                    args.package,
                    enabled,
                ),
            )
            connection.sendall(
                struct.pack("!II", MAGIC, len(payload)) + payload
            )
            print(
                "CHURN time={:.3f} config_id={} stream={} enabled={}".format(
                    time.time(), config_id, args.churn_index, int(enabled)
                ),
                flush=True,
            )

    print("HOLD seconds={}".format(args.hold), flush=True)
    time.sleep(args.hold)
    connection.close()
    server.close()
    if os.path.exists(args.socket):
        os.unlink(args.socket)


if __name__ == "__main__":
    main()
