#!/usr/bin/env python3
"""Local platform simulator for pipeline_agent integration tests."""

import argparse
import json
import os
import signal
import socket
import struct
import sys
import threading
import time
from pathlib import Path


MAGIC = 0xDEADBEEF
PROTO_VERSION = 11200
MSG_CONFIG = 2
DEFAULT_PLATFORM_SOCKET = "/tmp/pipeline-mock-platform.sock"
DEFAULT_CONTROL_SOCKET = "/tmp/pipeline-mock-control.sock"
DEFAULT_PACKAGE = (
    "/mnt/userdata/smart-guard-edge/recordings/default/ai-models/"
    "hardhat-detection/hardhat_detect_eic7700_1_2.pkg"
)


def varint(value):
    if value < 0:
        value &= (1 << 64) - 1
    output = bytearray()
    while value >= 0x80:
        output.append((value & 0x7F) | 0x80)
        value >>= 7
    output.append(value)
    return bytes(output)


def int_field(number, value):
    return varint(number << 3) + varint(int(value))


def bytes_field(number, value):
    if isinstance(value, str):
        value = value.encode("utf-8")
    return varint((number << 3) | 2) + varint(len(value)) + value


def float_field(number, value):
    return varint((number << 3) | 5) + struct.pack("<f", float(value))


def encode_algorithm(item):
    fields = [
        bytes_field(1, item["scenario"]),
        bytes_field(2, item["package"]),
        bytes_field(3, item.get("version", "mock-1.0")),
        float_field(4, item.get("threshold", 0.5)),
        int_field(5, item.get("alarm_level", 2)),
    ]
    if item.get("start_date"):
        fields.append(int_field(6, item["start_date"]))
    if item.get("end_date"):
        fields.append(int_field(7, item["end_date"]))
    return b"".join(fields)


def encode_stream(task, enabled):
    fields = [
        int_field(1, int(enabled)),
        bytes_field(2, task["stream_id"]),
    ]
    if enabled:
        fields.extend(
            [
                bytes_field(3, task["rtsp_url"]),
                int_field(5, task.get("reconnect_interval", 3)),
                bytes_field(6, task["snapshot_dir"]),
                bytes_field(7, task["record_dir"]),
                int_field(8, task.get("record_duration", 10)),
                int_field(9, task.get("dedup_interval", 30)),
            ]
        )
        for algorithm in task["algorithms"]:
            fields.append(bytes_field(10, encode_algorithm(algorithm)))
    return b"".join(fields)


def encode_config(agent_id, config_id, task, enabled):
    return b"".join(
        [
            bytes_field(1, agent_id),
            int_field(2, config_id),
            bytes_field(3, encode_stream(task, enabled)),
        ]
    )


def encode_envelope(sequence, config_payload):
    return b"".join(
        [
            int_field(1, PROTO_VERSION),
            int_field(2, MSG_CONFIG),
            int_field(3, sequence),
            bytes_field(11, config_payload),
        ]
    )


def read_exact(connection, length):
    output = bytearray()
    while len(output) < length:
        chunk = connection.recv(length - len(output))
        if not chunk:
            raise ConnectionError("peer closed")
        output.extend(chunk)
    return bytes(output)


def read_varint(data, offset):
    value = 0
    shift = 0
    while offset < len(data) and shift < 70:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
    raise ValueError("invalid protobuf varint")


def envelope_summary(payload):
    fields = {}
    offset = 0
    while offset < len(payload):
        key, offset = read_varint(payload, offset)
        number = key >> 3
        wire_type = key & 7
        if wire_type == 0:
            value, offset = read_varint(payload, offset)
            if number in (1, 2, 3):
                fields[number] = value
        elif wire_type == 1:
            offset += 8
        elif wire_type == 2:
            length, offset = read_varint(payload, offset)
            offset += length
        elif wire_type == 5:
            offset += 4
        else:
            break
    return {
        "version": fields.get(1),
        "type": fields.get(2),
        "seq": fields.get(3),
        "bytes": len(payload),
    }


def remove_socket(path):
    try:
        if path and os.path.exists(path):
            os.unlink(path)
    except OSError:
        pass


class MockPlatform:
    def __init__(self, platform_socket, control_socket, agent_id, config_id):
        self.platform_socket = platform_socket
        self.control_socket = control_socket
        self.agent_id = agent_id
        self.next_config_id = config_id
        self.sequence = 0
        self.active = {}
        self.pipeline_connection = None
        self.pipeline_lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.stop_event = threading.Event()
        self.listeners = []

    def log(self, message):
        stamp = time.strftime("%Y-%m-%d %H:%M:%S")
        print(f"{stamp} {message}", flush=True)

    def listen(self, path):
        remove_socket(path)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(path)
        os.chmod(path, 0o666)
        listener.listen(8)
        listener.settimeout(1.0)
        self.listeners.append(listener)
        return listener

    def run(self):
        platform_listener = self.listen(self.platform_socket)
        control_listener = self.listen(self.control_socket)
        self.log(f"LISTEN platform_socket={self.platform_socket}")
        self.log(f"LISTEN control_socket={self.control_socket}")
        threads = [
            threading.Thread(
                target=self.accept_pipeline,
                args=(platform_listener,),
                daemon=True,
            ),
            threading.Thread(
                target=self.accept_controls,
                args=(control_listener,),
                daemon=True,
            ),
        ]
        for thread in threads:
            thread.start()
        while not self.stop_event.wait(0.5):
            pass
        self.close()

    def close(self):
        self.stop_event.set()
        with self.pipeline_lock:
            connection = self.pipeline_connection
            self.pipeline_connection = None
        if connection is not None:
            try:
                connection.close()
            except OSError:
                pass
        for listener in self.listeners:
            try:
                listener.close()
            except OSError:
                pass
        remove_socket(self.platform_socket)
        remove_socket(self.control_socket)
        self.log("STOPPED")

    def accept_pipeline(self, listener):
        while not self.stop_event.is_set():
            try:
                connection, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with self.pipeline_lock:
                old_connection = self.pipeline_connection
                self.pipeline_connection = connection
            if old_connection is not None:
                try:
                    old_connection.close()
                except OSError:
                    pass
            self.log("PIPELINE connected")
            threading.Thread(
                target=self.receive_pipeline,
                args=(connection,),
                daemon=True,
            ).start()

    def receive_pipeline(self, connection):
        try:
            while not self.stop_event.is_set():
                header = read_exact(connection, 8)
                magic, length = struct.unpack("!II", header)
                if magic != MAGIC or length <= 0 or length > 64 * 1024 * 1024:
                    raise ValueError(
                        f"invalid frame magic=0x{magic:08x} length={length}"
                    )
                payload = read_exact(connection, length)
                self.log(f"RX {json.dumps(envelope_summary(payload))}")
        except (ConnectionError, OSError, ValueError) as error:
            self.log(f"PIPELINE disconnected reason={error}")
        finally:
            with self.pipeline_lock:
                if self.pipeline_connection is connection:
                    self.pipeline_connection = None
            try:
                connection.close()
            except OSError:
                pass

    def accept_controls(self, listener):
        while not self.stop_event.is_set():
            try:
                connection, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            threading.Thread(
                target=self.handle_control_connection,
                args=(connection,),
                daemon=True,
            ).start()

    def handle_control_connection(self, connection):
        try:
            request_file = connection.makefile("r", encoding="utf-8")
            line = request_file.readline()
            request = json.loads(line)
            response = self.handle_command(request)
        except Exception as error:
            response = {"ok": False, "error": str(error)}
        try:
            connection.sendall(
                (json.dumps(response, ensure_ascii=False) + "\n").encode("utf-8")
            )
        except OSError:
            pass
        finally:
            connection.close()

    def handle_command(self, request):
        action = request.get("action")
        if action == "status":
            with self.state_lock:
                tasks = list(self.active.values())
                with self.pipeline_lock:
                    connected = self.pipeline_connection is not None
            return {
                "ok": True,
                "pipeline_connected": connected,
                "active_count": len(tasks),
                "active_tasks": tasks,
                "sequence": self.sequence,
                "next_config_id": self.next_config_id,
            }
        if action == "shutdown":
            self.stop_event.set()
            return {"ok": True, "message": "mock platform stopping"}
        if action == "stop-all":
            with self.state_lock:
                stream_ids = list(self.active)
            results = [self.send_task({"stream_id": item}, False) for item in stream_ids]
            return {"ok": True, "results": results}
        if action == "start":
            return self.send_task(request["task"], True)
        if action == "stop":
            return self.send_task({"stream_id": request["stream_id"]}, False)
        raise ValueError(f"unsupported action: {action}")

    def send_task(self, task, enabled):
        stream_id = task["stream_id"]
        with self.state_lock:
            if not enabled:
                previous = self.active.get(stream_id, {"stream_id": stream_id})
                task = dict(previous)
            self.sequence += 1
            sequence = self.sequence
            config_id = self.next_config_id
            self.next_config_id += 1
            payload = encode_envelope(
                sequence,
                encode_config(
                    self.agent_id,
                    config_id,
                    task,
                    enabled,
                ),
            )
            frame = struct.pack("!II", MAGIC, len(payload)) + payload
            with self.pipeline_lock:
                connection = self.pipeline_connection
                if connection is None:
                    raise RuntimeError("pipeline_agent is not connected")
                connection.sendall(frame)
            if enabled:
                self.active[stream_id] = task
            else:
                self.active.pop(stream_id, None)
            active_count = len(self.active)
        action = "START" if enabled else "STOP"
        self.log(
            f"TX {action} seq={sequence} config_id={config_id} "
            f"stream={stream_id} active={active_count}"
        )
        return {
            "ok": True,
            "action": action.lower(),
            "stream_id": stream_id,
            "config_id": config_id,
            "sequence": sequence,
            "active_count": active_count,
        }


def request_control(path, payload):
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        connection.connect(path)
        connection.sendall(
            (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
        )
        response_file = connection.makefile("r", encoding="utf-8")
        response = json.loads(response_file.readline())
    finally:
        connection.close()
    print(json.dumps(response, ensure_ascii=False, indent=2))
    return 0 if response.get("ok") else 1


def parse_algorithm(value):
    parts = value.split(",")
    identity = parts[0].split("=", 1)
    if len(identity) != 2 or not identity[0] or not identity[1]:
        raise argparse.ArgumentTypeError(
            "算法格式必须为 场景编码=pkg路径[,阈值[,告警等级]]"
        )
    try:
        threshold = float(parts[1]) if len(parts) >= 2 else 0.5
        alarm_level = int(parts[2]) if len(parts) >= 3 else 2
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    if len(parts) > 3 or not 0.0 <= threshold <= 1.0:
        raise argparse.ArgumentTypeError("阈值必须在 [0,1] 内")
    if alarm_level not in (1, 2, 3, 4):
        raise argparse.ArgumentTypeError("告警等级必须为 1、2、3 或 4")
    return {
        "scenario": identity[0],
        "package": identity[1],
        "threshold": threshold,
        "alarm_level": alarm_level,
        "version": "mock-1.0",
    }


def add_control_option(parser):
    parser.add_argument(
        "--control-socket",
        default=DEFAULT_CONTROL_SOCKET,
        help=f"模拟器控制 socket，默认 {DEFAULT_CONTROL_SOCKET}",
    )


def build_parser():
    parser = argparse.ArgumentParser(
        description="模拟平台向 pipeline_agent 下发单路任务启动、停止指令"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    serve = subparsers.add_parser("serve", help="启动模拟平台服务")
    serve.add_argument(
        "--platform-socket",
        default=DEFAULT_PLATFORM_SOCKET,
        help=f"pipeline_agent 连接的 socket，默认 {DEFAULT_PLATFORM_SOCKET}",
    )
    add_control_option(serve)
    serve.add_argument("--agent-id", default="agent_001")
    serve.add_argument("--config-id", type=int, default=9000)

    start = subparsers.add_parser("start", help="启动或更新一路任务")
    add_control_option(start)
    start.add_argument("--stream-id", required=True)
    start.add_argument("--rtsp-url", required=True)
    start.add_argument(
        "--algorithm",
        action="append",
        type=parse_algorithm,
        help="可重复：场景编码=pkg路径[,阈值[,告警等级]]",
    )
    start.add_argument("--scenario", default="hardhat-detection")
    start.add_argument("--package", default=DEFAULT_PACKAGE)
    start.add_argument("--threshold", type=float, default=0.5)
    start.add_argument("--alarm-level", type=int, default=2)
    start.add_argument("--snapshot-dir")
    start.add_argument("--record-dir")
    start.add_argument("--record-duration", type=int, default=10)
    start.add_argument("--dedup-interval", type=int, default=30)
    start.add_argument("--reconnect-interval", type=int, default=3)

    stop = subparsers.add_parser("stop", help="停止一路任务")
    add_control_option(stop)
    stop.add_argument("--stream-id", required=True)

    for name, help_text in (
        ("stop-all", "停止全部模拟任务"),
        ("status", "查看模拟器期望状态"),
        ("shutdown", "停止模拟平台服务"),
    ):
        item = subparsers.add_parser(name, help=help_text)
        add_control_option(item)
    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    if args.command == "serve":
        server = MockPlatform(
            args.platform_socket,
            args.control_socket,
            args.agent_id,
            args.config_id,
        )

        def stop_server(_signum, _frame):
            server.stop_event.set()

        signal.signal(signal.SIGINT, stop_server)
        signal.signal(signal.SIGTERM, stop_server)
        server.run()
        return 0

    if args.command == "start":
        if not 0.0 <= args.threshold <= 1.0:
            parser.error("--threshold 必须在 [0,1] 内")
        if args.alarm_level not in (1, 2, 3, 4):
            parser.error("--alarm-level 必须为 1、2、3 或 4")
        if args.record_duration < 0 or args.dedup_interval < 0:
            parser.error("录像时长和告警去重时间不能为负数")
        if args.reconnect_interval < 0:
            parser.error("重连间隔不能为负数")
        algorithms = args.algorithm or [
            {
                "scenario": args.scenario,
                "package": args.package,
                "threshold": args.threshold,
                "alarm_level": args.alarm_level,
                "version": "mock-1.0",
            }
        ]
        base = f"/tmp/pipeline-mock-evidence/{args.stream_id}"
        task = {
            "stream_id": args.stream_id,
            "rtsp_url": args.rtsp_url,
            "snapshot_dir": args.snapshot_dir or f"{base}/snapshots",
            "record_dir": args.record_dir or f"{base}/records",
            "record_duration": args.record_duration,
            "dedup_interval": args.dedup_interval,
            "reconnect_interval": args.reconnect_interval,
            "algorithms": algorithms,
        }
        return request_control(
            args.control_socket,
            {"action": "start", "task": task},
        )

    action = args.command
    payload = {"action": action}
    if action == "stop":
        payload["stream_id"] = args.stream_id
    return request_control(args.control_socket, payload)


if __name__ == "__main__":
    sys.exit(main())
