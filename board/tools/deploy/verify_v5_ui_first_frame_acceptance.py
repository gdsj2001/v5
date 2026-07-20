#!/usr/bin/env python3
"""Board acceptance for Main-first lazy page caches and modal first frames."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import statistics
import struct
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path


UI_SERVICE_SOURCE = Path(__file__).resolve().parents[2] / "services" / "ui"
sys.path.insert(0, str(UI_SERVICE_SOURCE))

from v5_ui_main_cache_contract import self_test_lines, validate_main_cache_trace


WIDTH = 1024
HEIGHT = 600
EXPECTED_CACHE_BUDGET_BYTES = WIDTH * HEIGHT * 4 * 13
EXPECTED_CACHE_POLICY = "main_first_navigation_lazy_v1"
REGISTERED_PAGE_COUNT = 9
FULL_RECT = (0, 0, WIDTH, HEIGHT)
MEMORY_TOLERANCE_KIB = 1024
GLOBAL_LOCKED_TOLERANCE_KIB = 2048
FIRST_VISIBLE_DEADLINE_SECONDS = 0.2
FIRST_LAZY_PAGE_DEADLINE_SECONDS = 1.0
PRE_TARGET_FEEDBACK_MAX_RATIO = 0.10
IDLE_DIRTY_MAX_FRAME_RATIO = 0.50
IDLE_DIRTY_MAX_UNION_RATIO = 0.50
IDLE_DIRTY_MAX_STREAM_HZ = 35.0
MIN_STARTUP_CPU_SAMPLES = 5
MAX_STARTUP_CPU_PEAK_PERCENT = 95.0

PAGE_STEPS = (
    ("tool", (972, 90), (912, 477)),
    ("probe", (972, 150), (972, 30)),
    ("offset", (972, 210), (942, 561)),
    ("io", (972, 270), (972, 30)),
    ("network", (861, 31), (972, 30)),
    ("program", (611, 466), (454, 544)),
    ("mdi", (611, 516), (969, 541)),
)
SETTINGS_OPEN = (972, 330)
SETTINGS_KEYBOARD_CELL = (423, 330)
SETTINGS_KEYBOARD_ESC = (626, 453)
SETTINGS_READ_DRIVE = (787, 253)
POPUP_CLOSE = (736, 458)
SETTINGS_SAVE_RESTART = (948, 25)


class AcceptanceError(RuntimeError):
    pass


class FirstVisibleTimeout(AcceptanceError):
    pass


def websocket_accept(key: str) -> str:
    source = (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
    return base64.b64encode(hashlib.sha1(source).digest()).decode("ascii")


class WebSocket:
    def __init__(self, host: str, port: int, path: str, timeout: float = 5.0):
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        self.sock.sendall(request)
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise AcceptanceError("websocket handshake closed")
            response += chunk
        header, self.buffer = response.split(b"\r\n\r\n", 1)
        text = header.decode("iso-8859-1", errors="replace")
        if "101 Switching Protocols" not in text or websocket_accept(key) not in text:
            raise AcceptanceError(f"websocket handshake failed path={path}")

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def _recv_exact(self, size: int) -> bytes:
        while len(self.buffer) < size:
            chunk = self.sock.recv(max(4096, size - len(self.buffer)))
            if not chunk:
                raise EOFError("websocket closed")
            self.buffer += chunk
        payload, self.buffer = self.buffer[:size], self.buffer[size:]
        return payload

    def recv(self) -> tuple[int, bytes]:
        first = self._recv_exact(2)
        opcode = first[0] & 0x0F
        masked = (first[1] & 0x80) != 0
        length = first[1] & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._recv_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._recv_exact(8))[0]
        mask = self._recv_exact(4) if masked else b""
        payload = self._recv_exact(length)
        if mask:
            payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
        return opcode, payload

    def send(self, opcode: int, payload: bytes) -> None:
        mask = os.urandom(4)
        length = len(payload)
        if length < 126:
            header = bytes([0x80 | opcode, 0x80 | length])
        elif length <= 0xFFFF:
            header = bytes([0x80 | opcode, 0x80 | 126]) + struct.pack("!H", length)
        else:
            header = bytes([0x80 | opcode, 0x80 | 127]) + struct.pack("!Q", length)
        masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def send_json(self, payload: dict) -> None:
        self.send(0x1, json.dumps(payload, separators=(",", ":")).encode("utf-8"))

    def recv_json(self) -> dict:
        while True:
            opcode, payload = self.recv()
            if opcode == 0x9:
                self.send(0xA, payload)
                continue
            if opcode == 0x8:
                raise EOFError("websocket close")
            if opcode != 0x1:
                continue
            message = json.loads(payload.decode("utf-8"))
            if not isinstance(message, dict):
                raise AcceptanceError("websocket JSON payload is not an object")
            return message


class RemoteInput:
    def __init__(self, host: str, port: int, cycle: int):
        self.ws = WebSocket(host, port, "/remote/input")
        self.session_id = f"v5-first-frame-{cycle}-{int(time.time() * 1000)}"
        self.seq = 0
        self.ws.send_json({
            "type": "control_request",
            "session_id": self.session_id,
            "source": "v5_ui_first_frame_acceptance",
            "client_time_ms": int(time.time() * 1000),
        })
        grant = self.ws.recv_json()
        if grant.get("type") != "control_grant" or not grant.get("accepted"):
            raise AcceptanceError(f"remote input control rejected: {grant}")

    def close(self) -> None:
        self.ws.close()

    def pointer(self, phase: str, x: int, y: int) -> dict:
        self.seq += 1
        self.ws.send_json({
            "type": "pointer_event",
            "session_id": self.session_id,
            "source": "v5_ui_first_frame_acceptance",
            "seq": self.seq,
            "phase": phase,
            "x": x,
            "y": y,
            "button": "left",
            "client_time_ms": int(time.time() * 1000),
        })
        ack = self.ws.recv_json()
        if not ack.get("accepted"):
            raise AcceptanceError(f"pointer {phase} rejected at ({x},{y}): {ack}")
        return ack

    def click(self, point: tuple[int, int]) -> dict:
        down_ack = self.pointer("down", point[0], point[1])
        time.sleep(0.03)
        up_ack = self.pointer("up", point[0], point[1])
        return {
            "down_ack": down_ack,
            "up_ack": up_ack,
            "release_seq": int(up_ack.get("seq") or 0),
            "release_ack_monotonic_ns": time.monotonic_ns(),
        }


class StreamObserver(threading.Thread):
    def __init__(self, host: str, port: int):
        super().__init__(daemon=True)
        self.ws = WebSocket(host, port, "/remote/stream")
        self.stop_requested = threading.Event()
        self.lock = threading.Lock()
        self.frames: list[dict] = []
        self.error = ""
        self.pending_metadata: dict | None = None

    def _set_error(self, message: str) -> None:
        with self.lock:
            if not self.error:
                self.error = message

    def run(self) -> None:
        pending: dict | None = None
        try:
            while not self.stop_requested.is_set():
                try:
                    opcode, payload = self.ws.recv()
                except socket.timeout:
                    continue
                if opcode == 0x9:
                    self.ws.send(0xA, payload)
                elif opcode == 0x8:
                    if not self.stop_requested.is_set():
                        raise EOFError("websocket close")
                    break
                elif opcode == 0x1:
                    message = json.loads(payload.decode("utf-8"))
                    if not isinstance(message, dict):
                        raise ValueError("stream metadata is not an object")
                    if pending is not None:
                        raise ValueError("stream metadata replaced before binary payload")
                    pending = message
                    with self.lock:
                        self.pending_metadata = dict(message)
                elif opcode == 0x2:
                    if pending is None:
                        raise ValueError("stream binary payload has no metadata")
                    item = dict(pending)
                    item["payload_bytes"] = len(payload)
                    item["payload_sha256"] = hashlib.sha256(payload).hexdigest()
                    item["payload_nonuniform"] = len(set(payload[: min(len(payload), 65536)])) > 1
                    with self.lock:
                        self.frames.append(item)
                        self.pending_metadata = None
                    pending = None
        except (EOFError, OSError, ValueError, json.JSONDecodeError) as exc:
            if not self.stop_requested.is_set():
                self._set_error(f"{type(exc).__name__}: {exc}")

    def close(self) -> None:
        self.stop_requested.set()
        self.ws.close()
        self.join(timeout=2.0)

    def snapshot(self) -> list[dict]:
        with self.lock:
            return [dict(item) for item in self.frames]

    def snapshot_state(self) -> tuple[list[dict], str, dict | None]:
        with self.lock:
            frames = [dict(item) for item in self.frames]
            pending = dict(self.pending_metadata) if self.pending_metadata is not None else None
            return frames, self.error, pending


REMOTE_SAMPLE = r'''
import json, os, time
from pathlib import Path

pid = int(Path("/run/8ax/v5_ui_shell.pid").read_text().strip())
status = {}
for line in Path(f"/proc/{pid}/status").read_text(errors="replace").splitlines():
    if ":" in line:
        key, value = line.split(":", 1)
        status[key] = value.strip()
stat_text = Path(f"/proc/{pid}/stat").read_text()
tail = stat_text[stat_text.rfind(")") + 2:].split()
ticks = int(tail[11]) + int(tail[12])
tasks = []
for task in sorted(Path(f"/proc/{pid}/task").glob("[0-9]*"), key=lambda p: int(p.name)):
    cpus = ""
    for line in (task / "status").read_text(errors="replace").splitlines():
        if line.startswith("Cpus_allowed_list:"):
            cpus = line.split(":", 1)[1].strip()
            break
    tasks.append({"tid": int(task.name), "cpus_allowed_list": cpus})
locked = 0
for line in Path("/proc/meminfo").read_text().splitlines():
    if line.startswith("Locked:"):
        locked = int(line.split()[1])
        break
def kib(name):
    text = status.get(name, "0 kB").split()
    return int(text[0]) if text else 0
print(json.dumps({
    "pid": pid,
    "monotonic_ns": time.monotonic_ns(),
    "ticks": ticks,
    "clk_tck": os.sysconf("SC_CLK_TCK"),
    "vmrss_kib": kib("VmRSS"),
    "vmlck_kib": kib("VmLck"),
    "global_locked_kib": locked,
    "cpus_allowed_list": status.get("Cpus_allowed_list", ""),
    "tasks": tasks,
}, separators=(",", ":")))
'''


REMOTE_RESTART_MONITOR = r'''
import hashlib, json, os, subprocess, time, urllib.error, urllib.request
from pathlib import Path

port = int(os.environ.get("V5_ACCEPT_PORT", "18080"))
interval = 0.02
before_ready = None
try:
    before_ready = json.loads(Path("/run/8ax_v5_product_ui/ui_ready.json").read_text())
except Exception:
    pass

def status_sample(pid):
    try:
        rows = Path(f"/proc/{pid}/status").read_text(errors="replace").splitlines()
        stat_text = Path(f"/proc/{pid}/stat").read_text()
    except OSError:
        return None
    fields = {}
    for line in rows:
        if ":" in line:
            key, value = line.split(":", 1)
            fields[key] = value.strip()
    tail = stat_text[stat_text.rfind(")") + 2:].split()
    def kib(name):
        value = fields.get(name, "0 kB").split()
        return int(value[0]) if value else 0
    return {
        "t": time.monotonic(),
        "pid": pid,
        "ticks": int(tail[11]) + int(tail[12]),
        "vmrss_kib": kib("VmRSS"),
        "vmlck_kib": kib("VmLck"),
        "cpus": fields.get("Cpus_allowed_list", ""),
    }

def cpu1_sample():
    try:
        for line in Path("/proc/stat").read_text().splitlines():
            fields = line.split()
            if fields and fields[0] == "cpu1":
                values = [int(value) for value in fields[1:9]]
                if len(values) < 4:
                    return None
                idle = values[3] + (values[4] if len(values) > 4 else 0)
                total = sum(values)
                return {"t": time.monotonic(), "busy": total - idle, "total": total}
    except (OSError, ValueError):
        pass
    return None

def framebuffer_hash():
    try:
        stride = int(Path("/sys/class/graphics/fb0/stride").read_text().strip())
        mode = Path("/sys/class/graphics/fb0/modes").read_text().splitlines()[0]
        height = int(mode.split("x", 1)[1].split("p", 1)[0])
        with open("/dev/fb0", "rb", buffering=0) as fp:
            return hashlib.sha256(fp.read(stride * height)).hexdigest()
    except (OSError, ValueError, IndexError):
        return ""

def gate_status():
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/remote/info", timeout=0.15) as response:
            response.read()
            return int(response.status)
    except urllib.error.HTTPError as exc:
        return int(exc.code)
    except Exception:
        return 0

fb_before = framebuffer_hash()
proc = subprocess.Popen(["/etc/init.d/v5-ui-relay", "restart"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
samples = []
cpu1_samples = []
gate = []
last_gate = None
deadline = time.monotonic() + 180.0
while time.monotonic() < deadline:
    pid = 0
    try:
        pid = int(Path("/run/8ax/v5_ui_shell.pid").read_text().strip())
    except Exception:
        pass
    if pid:
        sample = status_sample(pid)
        if sample:
            samples.append(sample)
    cpu1 = cpu1_sample()
    if cpu1:
        cpu1_samples.append(cpu1)
    status = gate_status()
    if status != last_gate:
        gate.append({"t": time.monotonic(), "status": status})
        last_gate = status
    ready = None
    try:
        ready = json.loads(Path("/run/8ax_v5_product_ui/ui_ready.json").read_text())
    except Exception:
        pass
    if proc.poll() is not None and ready and ready.get("ready"):
        break
    time.sleep(interval)
if proc.poll() is None:
    proc.kill()
output = proc.communicate(timeout=5)[0]
ready = json.loads(Path("/run/8ax_v5_product_ui/ui_ready.json").read_text())
target_pid = int(ready["ui_pid"])
target = [item for item in samples if item["pid"] == target_pid]
ready_monotonic = int(ready.get("created_monotonic_ns") or 0) / 1000000000.0
pre_ready_target = [item for item in target if not ready_monotonic or item["t"] <= ready_monotonic + interval]
pre_ready_cpu1 = [item for item in cpu1_samples if not ready_monotonic or item["t"] <= ready_monotonic + interval]
clk = os.sysconf("SC_CLK_TCK")
cpu_windows = []
for first, second in zip(pre_ready_target, pre_ready_target[1:]):
    wall = second["t"] - first["t"]
    if wall > 0:
        cpu_windows.append(max(0.0, ((second["ticks"] - first["ticks"]) / clk) / wall * 100.0))
consecutive = maximum = 0
for value in cpu_windows:
    if value >= 95.0:
        consecutive += 1
        maximum = max(maximum, consecutive)
    else:
        consecutive = 0
cpu1_windows = []
for first, second in zip(pre_ready_cpu1, pre_ready_cpu1[1:]):
    total = second["total"] - first["total"]
    busy = second["busy"] - first["busy"]
    if total > 0:
        cpu1_windows.append(max(0.0, min(100.0, busy / total * 100.0)))
cpu1_consecutive = cpu1_maximum = 0
for value in cpu1_windows:
    if value >= 95.0:
        cpu1_consecutive += 1
        cpu1_maximum = max(cpu1_maximum, cpu1_consecutive)
    else:
        cpu1_consecutive = 0
main_cache_trace = [
    line for line in Path("/run/8ax/v5_ui_boot.log").read_text(errors="replace").splitlines()
    if line.startswith("V5_UI_MAIN_CACHE ")
]
print(json.dumps({
    "returncode": proc.returncode,
    "output_tail": output.splitlines()[-30:],
    "before_boot_id": before_ready.get("boot_id") if isinstance(before_ready, dict) else None,
    "before_ui_instance_id": before_ready.get("ui_instance_id") if isinstance(before_ready, dict) else None,
    "ready": ready,
    "sample_count": len(pre_ready_target),
    "cpu_window_count": len(cpu_windows),
    "cpu_peak_percent": round(max(cpu_windows) if cpu_windows else 0.0, 1),
    "cpu_sustained_ge95_windows": maximum,
    "cpu1_sample_count": len(pre_ready_cpu1),
    "cpu1_window_count": len(cpu1_windows),
    "cpu1_peak_percent": round(max(cpu1_windows) if cpu1_windows else 0.0, 1),
    "cpu1_sustained_ge95_windows": cpu1_maximum,
    "vmrss_min_kib": min((item["vmrss_kib"] for item in pre_ready_target), default=0),
    "vmrss_max_kib": max((item["vmrss_kib"] for item in pre_ready_target), default=0),
    "vmlck_min_kib": min((item["vmlck_kib"] for item in pre_ready_target), default=0),
    "vmlck_max_kib": max((item["vmlck_kib"] for item in pre_ready_target), default=0),
    "cpu_lists": sorted(set(item["cpus"] for item in pre_ready_target)),
    "gate_status_transitions": gate,
    "ui_main_cache_trace": main_cache_trace,
    "framebuffer_sha256_before": fb_before,
    "framebuffer_sha256_after": framebuffer_hash(),
}, separators=(",", ":")))
'''


def ssh_base(args: argparse.Namespace) -> list[str]:
    command = [
        "ssh", "-o", "BatchMode=yes", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=5",
        "-p", str(args.ssh_port),
    ]
    if args.ssh_key:
        command.extend(["-i", args.ssh_key, "-o", "IdentitiesOnly=yes"])
    command.append(args.ssh)
    return command


def ssh_python(args: argparse.Namespace, script: str, env: dict[str, str] | None = None, timeout: float = 240.0) -> dict:
    remote = "python3 -"
    if env:
        prefix = " ".join(f"{key}={json.dumps(value)}" for key, value in env.items())
        remote = f"{prefix} python3 -"
    proc = subprocess.run(
        ssh_base(args) + [remote],
        input=script,
        text=True,
        capture_output=True,
        timeout=timeout,
    )
    if proc.returncode != 0:
        raise AcceptanceError(f"remote Python failed rc={proc.returncode}: {proc.stderr.strip() or proc.stdout.strip()}")
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise AcceptanceError("remote Python returned no JSON")
    try:
        payload = json.loads(lines[-1])
    except json.JSONDecodeError as exc:
        raise AcceptanceError(f"remote Python returned invalid JSON: {lines[-1]}") from exc
    if not isinstance(payload, dict):
        raise AcceptanceError("remote Python result is not an object")
    return payload


def http_json(args: argparse.Namespace, path: str, timeout: float = 3.0) -> dict:
    url = f"http://{args.relay_host}:{args.port}{path}"
    with urllib.request.urlopen(url, timeout=timeout) as response:
        payload = json.load(response)
    if not isinstance(payload, dict):
        raise AcceptanceError(f"HTTP JSON is not an object path={path}")
    return payload


def diagnostics(args: argparse.Namespace) -> dict:
    return http_json(args, "/remote/diagnostics")


def wait_ready(args: argparse.Namespace, previous_ui_instance_id: str | None = None,
               timeout: float = 180.0) -> dict:
    deadline = time.monotonic() + timeout
    last_error = "not_checked"
    while time.monotonic() < deadline:
        try:
            info = http_json(args, "/remote/info", timeout=1.0)
            ready = info.get("ready_metadata")
            if (isinstance(ready, dict) and ready.get("ready") and
                    ready.get("ui_instance_id") != previous_ui_instance_id):
                validate_ready_metadata(ready)
                return ready
            last_error = f"stale_or_missing_ready:{ready!r}"
        except (OSError, urllib.error.URLError, AcceptanceError) as exc:
            last_error = str(exc)
        time.sleep(0.2)
    raise AcceptanceError(
        f"UI ready timeout previous_ui_instance_id={previous_ui_instance_id!r} "
        f"last_error={last_error}"
    )


def validate_ready_metadata(ready: dict) -> None:
    if ready.get("schema") != "v5.ui_ready.v1" or ready.get("ready") is not True:
        raise AcceptanceError(f"bad ready metadata schema: {ready}")
    if int(ready.get("cache_budget_bytes") or 0) != EXPECTED_CACHE_BUDGET_BYTES:
        raise AcceptanceError(f"cache budget mismatch: {ready.get('cache_budget_bytes')}")
    if ready.get("cache_policy") != EXPECTED_CACHE_POLICY:
        raise AcceptanceError(f"cache policy mismatch: {ready.get('cache_policy')!r}")
    if int(ready.get("cache_page_count") or 0) != 1:
        raise AcceptanceError(f"boot cache page count mismatch: {ready.get('cache_page_count')!r}")
    if int(ready.get("cache_registered_page_count") or 0) != REGISTERED_PAGE_COUNT:
        raise AcceptanceError(
            f"registered page count mismatch: {ready.get('cache_registered_page_count')!r}"
        )
    main_cache = ready.get("main_cache")
    if not isinstance(main_cache, dict) or main_cache.get("page") != "main":
        raise AcceptanceError(f"Main cache metadata mismatch: {main_cache!r}")
    if ready.get("cpus_allowed_list") != "1":
        raise AcceptanceError(f"UI startup affinity is not CPU1: {ready.get('cpus_allowed_list')!r}")
    for field in ("boot_id", "ui_instance_id"):
        value = str(ready.get(field) or "").strip().lower()
        try:
            canonical = str(uuid.UUID(value))
        except (ValueError, AttributeError) as exc:
            raise AcceptanceError(f"ready metadata {field} is invalid: {value!r}") from exc
        if value != canonical:
            raise AcceptanceError(f"ready metadata {field} is not canonical: {value!r}")
    try:
        ui_pid = int(ready.get("ui_pid") or 0)
        ui_start_ticks = int(ready.get("ui_start_ticks") or 0)
    except (TypeError, ValueError) as exc:
        raise AcceptanceError("ready metadata UI process identity is not numeric") from exc
    if ui_pid <= 0 or ui_start_ticks <= 0:
        raise AcceptanceError(
            f"ready metadata UI process identity is invalid pid={ready.get('ui_pid')!r} "
            f"start_ticks={ready.get('ui_start_ticks')!r}"
        )
    first = ready.get("first_frame")
    if not isinstance(first, dict) or tuple(int(first.get(key, -1)) for key in ("x", "y", "w", "h")) != FULL_RECT:
        raise AcceptanceError(f"formal first frame is not a full main blit: {first}")
    first_frame_id = int(first.get("frame_id") or 0)
    first_base_frame_id = int(first.get("base_frame_id") if first.get("base_frame_id") is not None else -1)
    current_frame_id = int(ready.get("current_frame_id") or 0)
    if first_frame_id <= 1 or first_base_frame_id != 1 or current_frame_id < first_frame_id:
        raise AcceptanceError(
            f"formal first frame identity/base ordering invalid first={first!r} current_frame_id={current_frame_id}"
        )


def sample_ui(args: argparse.Namespace) -> dict:
    sample = ssh_python(args, REMOTE_SAMPLE, timeout=10.0)
    if sample.get("cpus_allowed_list") != "1":
        raise AcceptanceError(f"UI process affinity changed: {sample}")
    bad_tasks = [item for item in sample.get("tasks", []) if item.get("cpus_allowed_list") != "1"]
    if bad_tasks:
        raise AcceptanceError(f"UI task escaped CPU1: {bad_tasks}")
    return sample


def cpu_percent(first: dict, second: dict) -> float:
    elapsed = (int(second["monotonic_ns"]) - int(first["monotonic_ns"])) / 1_000_000_000.0
    if elapsed <= 0 or int(first["pid"]) != int(second["pid"]):
        return 0.0
    ticks = int(second["ticks"]) - int(first["ticks"])
    return max(0.0, (ticks / int(second["clk_tck"])) / elapsed * 100.0)


def recent_events_after(diag: dict, frame_id: int) -> list[dict]:
    events = diag.get("recent_dirty_events")
    if not isinstance(events, list):
        return []
    result = [item for item in events if isinstance(item, dict) and int(item.get("frame_id") or 0) > frame_id]
    return sorted(result, key=lambda item: int(item.get("frame_id") or 0))


def is_full_event(event: dict) -> bool:
    return tuple(int(event.get(key, -1)) for key in ("x", "y", "w", "h")) == FULL_RECT


def checked_rect(rect: dict, label: str) -> tuple[int, int, int, int]:
    try:
        x, y, w, h = (int(rect.get(key, -1)) for key in ("x", "y", "w", "h"))
    except (AttributeError, TypeError, ValueError) as exc:
        raise AcceptanceError(f"invalid dirty rect label={label} rect={rect!r}") from exc
    if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > WIDTH or y + h > HEIGHT:
        raise AcceptanceError(f"out-of-bounds dirty rect label={label} rect={(x, y, w, h)}")
    return x, y, w, h


def rect_union_area(rects: list[dict], label: str) -> int:
    normalized = [checked_rect(rect, label) for rect in rects]
    if not normalized:
        return 0
    x_edges = sorted({edge for x, _, w, _ in normalized for edge in (x, x + w)})
    area = 0
    for left, right in zip(x_edges, x_edges[1:]):
        intervals = sorted(
            (y, y + h) for x, y, w, h in normalized if x < right and x + w > left
        )
        if not intervals:
            continue
        covered = 0
        start, end = intervals[0]
        for next_start, next_end in intervals[1:]:
            if next_start > end:
                covered += end - start
                start, end = next_start, next_end
            else:
                end = max(end, next_end)
        covered += end - start
        area += (right - left) * covered
    return area


def validate_dirty_chain(events: list[dict], base_frame_id: int, label: str) -> int:
    expected_base = int(base_frame_id)
    index = 0
    while index < len(events):
        try:
            frame_id = int(events[index].get("frame_id") or 0)
        except (AttributeError, TypeError, ValueError) as exc:
            raise AcceptanceError(f"invalid dirty identity label={label} index={index} event={events[index]!r}") from exc
        group_end = index + 1
        while group_end < len(events):
            try:
                if int(events[group_end].get("frame_id") or 0) != frame_id:
                    break
            except (AttributeError, TypeError, ValueError) as exc:
                raise AcceptanceError(
                    f"invalid dirty identity label={label} index={group_end} event={events[group_end]!r}"
                ) from exc
            group_end += 1
        for event_index in range(index, group_end):
            event = events[event_index]
            try:
                event_base = int(event.get("base_frame_id") if event.get("base_frame_id") is not None else -1)
            except (AttributeError, TypeError, ValueError) as exc:
                raise AcceptanceError(
                    f"invalid dirty identity label={label} index={event_index} event={event!r}"
                ) from exc
            if event_base != expected_base or frame_id <= event_base:
                raise AcceptanceError(
                    f"dirty base continuity failed label={label} index={event_index} "
                    f"expected_base={expected_base} event={event!r}"
                )
            checked_rect(event, f"{label}[{event_index}]")
        expected_base = frame_id
        index = group_end
    return expected_base


def validate_first_visible_events(
    events: list[dict],
    base_frame_id: int,
    elapsed_ms: float,
    label: str,
    deadline_seconds: float = FIRST_VISIBLE_DEADLINE_SECONDS,
) -> dict | None:
    if not events:
        return None
    validate_dirty_chain(events, base_frame_id, label)
    full_index = next((index for index, event in enumerate(events) if is_full_event(event)), None)
    if full_index is None:
        return None
    feedback = events[:full_index]
    feedback_area = rect_union_area(feedback, f"{label}.pre_target_feedback")
    if feedback_area > int(WIDTH * HEIGHT * PRE_TARGET_FEEDBACK_MAX_RATIO):
        raise AcceptanceError(
            f"large frame appeared before target full label={label} "
            f"feedback_area={feedback_area} limit_ratio={PRE_TARGET_FEEDBACK_MAX_RATIO}"
        )
    if elapsed_ms > deadline_seconds * 1000.0:
        raise AcceptanceError(
            f"target full missed first-visible deadline label={label} elapsed_ms={elapsed_ms:.1f}"
        )
    result = dict(events[full_index])
    result.update({
        "acceptance_label": label,
        "observed_elapsed_ms": round(max(0.0, elapsed_ms), 3),
        "pre_target_dirty_count": len(feedback),
        "pre_target_dirty_union_pixels": feedback_area,
        "identity_scope": "frame_chain_only",
        "identity_gap": "dirty protocol has no pointer_seq/page/cache_slot",
    })
    return result


def wait_full_event(
    args: argparse.Namespace,
    after_frame_id: int,
    label: str,
    timeout: float = FIRST_VISIBLE_DEADLINE_SECONDS,
    release_ack_monotonic_ns: int | None = None,
) -> dict:
    started_ns = release_ack_monotonic_ns or time.monotonic_ns()
    deadline_ns = started_ns + int(timeout * 1_000_000_000)
    while True:
        diag = diagnostics(args)
        events = recent_events_after(diag, after_frame_id)
        elapsed_ms = max(0.0, (time.monotonic_ns() - started_ns) / 1_000_000.0)
        result = validate_first_visible_events(
            events,
            after_frame_id,
            elapsed_ms,
            label,
            timeout,
        )
        if result is not None:
            result["release_ack_monotonic_ns"] = started_ns
            return result
        if time.monotonic_ns() >= deadline_ns:
            break
        time.sleep(0.01)
    raise FirstVisibleTimeout(
        f"missing full cache/popup frame within {timeout:.1f}s "
        f"label={label} after_frame_id={after_frame_id}"
    )


def click_for_full(
    args: argparse.Namespace,
    remote_input: RemoteInput,
    point: tuple[int, int],
    label: str,
    memory_samples: list[dict],
    cpu_windows: list[float],
    timeout: float = FIRST_VISIBLE_DEADLINE_SECONDS,
) -> dict:
    before = sample_ui(args)
    before_diag = diagnostics(args)
    before_frame_id = int(before_diag.get("frame_id") or 0)
    receipt = remote_input.click(point)
    event = wait_full_event(
        args,
        before_frame_id,
        label,
        timeout=timeout,
        release_ack_monotonic_ns=int(receipt["release_ack_monotonic_ns"]),
    )
    event["release_seq"] = int(receipt["release_seq"])
    after = sample_ui(args)
    memory_samples.append(after)
    cpu_windows.append(cpu_percent(before, after))
    return event


def stream_frame_rects(frame: dict, label: str) -> list[dict]:
    if frame.get("type") == "full_frame":
        return [{"x": 0, "y": 0, "w": WIDTH, "h": HEIGHT}]
    rects = frame.get("rects")
    if frame.get("type") != "dirty_rects" or not isinstance(rects, list) or not rects:
        raise AcceptanceError(f"invalid stream frame metadata label={label} frame={frame!r}")
    return rects


def validate_stream_frames(frames: list[dict], label: str) -> None:
    if not frames:
        raise AcceptanceError(f"stream produced no frames label={label}")
    previous_frame_id = 0
    previous_monotonic_ns = 0
    for index, frame in enumerate(frames):
        context = f"{label}[{index}]"
        try:
            frame_id = int(frame.get("frame_id") or 0)
            base_frame_id = int(frame.get("base_frame_id") if frame.get("base_frame_id") is not None else -1)
            monotonic_ns = int(frame.get("monotonic_ns") or 0)
            width = int(frame.get("width") or 0)
            height = int(frame.get("height") or 0)
            stride = int(frame.get("stride") or 0)
            payload_bytes = int(frame.get("payload_bytes") or 0)
        except (AttributeError, TypeError, ValueError) as exc:
            raise AcceptanceError(f"invalid stream identity context={context} frame={frame!r}") from exc
        if width != WIDTH or height != HEIGHT or stride != WIDTH * 4 or frame.get("format") != "bgra32":
            raise AcceptanceError(f"stream geometry/format mismatch context={context} frame={frame!r}")
        if monotonic_ns <= previous_monotonic_ns:
            raise AcceptanceError(f"stream monotonic identity regressed context={context} frame={frame!r}")
        rects = stream_frame_rects(frame, context)
        expected_payload = sum(rect[2] * rect[3] * 4 for rect in (checked_rect(item, context) for item in rects))
        if payload_bytes != expected_payload:
            raise AcceptanceError(
                f"stream payload size mismatch context={context} actual={payload_bytes} expected={expected_payload}"
            )
        if index == 0:
            if frame.get("type") != "full_frame" or base_frame_id != 0 or frame_id <= 0:
                raise AcceptanceError(f"stream initial full identity invalid context={context} frame={frame!r}")
        else:
            if frame.get("type") != "dirty_rects":
                raise AcceptanceError(f"unexpected repair/full frame context={context} frame={frame!r}")
            if base_frame_id != previous_frame_id or frame_id <= base_frame_id:
                raise AcceptanceError(
                    f"stream base continuity failed context={context} expected_base={previous_frame_id} frame={frame!r}"
                )
            if int(frame.get("dirty_count") or 0) != len(rects):
                raise AcceptanceError(f"stream dirty_count mismatch context={context} frame={frame!r}")
        previous_frame_id = frame_id
        previous_monotonic_ns = monotonic_ns


def wait_stream_initialized(stream: StreamObserver, label: str, timeout: float = 3.0) -> list[dict]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frames, error, pending = stream.snapshot_state()
        if error:
            raise AcceptanceError(f"stream observer failed label={label} error={error}")
        if frames:
            validate_stream_frames(frames, label)
            return frames
        if pending is not None:
            time.sleep(0.01)
            continue
        time.sleep(0.01)
    raise AcceptanceError(f"stream initial full timeout label={label}")


def wait_stream_frame_at_least(
    stream: StreamObserver,
    frame_id: int,
    label: str,
    timeout: float = FIRST_VISIBLE_DEADLINE_SECONDS,
) -> list[dict]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frames, error, _ = stream.snapshot_state()
        if error:
            raise AcceptanceError(f"stream observer failed label={label} error={error}")
        if frames and int(frames[-1].get("frame_id") or 0) >= frame_id:
            return frames
        time.sleep(0.005)
    raise AcceptanceError(f"stream did not reach frame_id={frame_id} label={label}")


def validate_stream_observer_state(
    frames: list[dict],
    error: str,
    pending: dict | None,
    label: str,
) -> None:
    if error:
        raise AcceptanceError(f"stream observer failed label={label} error={error}")
    if pending is not None:
        raise AcceptanceError(f"stream metadata has no binary payload label={label} pending={pending!r}")
    validate_stream_frames(frames, label)


def validated_stream_snapshot(stream: StreamObserver, label: str) -> list[dict]:
    deadline = time.monotonic() + FIRST_VISIBLE_DEADLINE_SECONDS
    while True:
        frames, error, pending = stream.snapshot_state()
        if error:
            raise AcceptanceError(f"stream observer failed label={label} error={error}")
        if pending is None or time.monotonic() >= deadline:
            break
        time.sleep(0.005)
    validate_stream_observer_state(frames, error, pending, label)
    return frames


def validate_idle_stream_window(frames: list[dict], seconds: float, label: str) -> dict:
    if seconds <= 0:
        raise AcceptanceError(f"idle window duration is invalid label={label} seconds={seconds}")
    if any(frame.get("type") == "full_frame" for frame in frames):
        raise AcceptanceError(f"full stream frame detected during idle label={label} frames={frames!r}")
    max_frames = int(seconds * IDLE_DIRTY_MAX_STREAM_HZ) + 2
    if len(frames) > max_frames:
        raise AcceptanceError(
            f"idle dirty stream frequency exceeded label={label} frames={len(frames)} "
            f"seconds={seconds:.3f} limit_hz={IDLE_DIRTY_MAX_STREAM_HZ}"
        )
    rects: list[dict] = []
    for index, frame in enumerate(frames):
        frame_rects = stream_frame_rects(frame, f"{label}[{index}]")
        frame_area = rect_union_area(frame_rects, f"{label}[{index}]")
        if frame_area > int(WIDTH * HEIGHT * IDLE_DIRTY_MAX_FRAME_RATIO):
            raise AcceptanceError(
                f"idle dirty frame too large label={label} index={index} "
                f"area={frame_area} limit_ratio={IDLE_DIRTY_MAX_FRAME_RATIO}"
            )
        rects.extend(frame_rects)
    union_area = rect_union_area(rects, f"{label}.union")
    if union_area > int(WIDTH * HEIGHT * IDLE_DIRTY_MAX_UNION_RATIO):
        raise AcceptanceError(
            f"idle dirty union too large label={label} area={union_area} "
            f"limit_ratio={IDLE_DIRTY_MAX_UNION_RATIO}"
        )
    return {
        "seconds": round(seconds, 3),
        "stream_frame_count": len(frames),
        "stream_hz": round(len(frames) / seconds, 3),
        "dirty_union_pixels": union_area,
        "dirty_union_ratio": round(union_area / (WIDTH * HEIGHT), 6),
    }


def verify_idle_dirty_is_partial(
    args: argparse.Namespace,
    stream: StreamObserver,
    seconds: float = 0.6,
    label: str = "idle",
) -> dict:
    start = diagnostics(args)
    frame_id = int(start.get("frame_id") or 0)
    stream_before = wait_stream_frame_at_least(stream, frame_id, f"{label}.settle")
    started = time.monotonic()
    time.sleep(seconds)
    elapsed = time.monotonic() - started
    end = diagnostics(args)
    events = recent_events_after(end, frame_id)
    if events:
        validate_dirty_chain(events, frame_id, f"{label}.diagnostics")
    full = [item for item in events if is_full_event(item)]
    if full:
        raise AcceptanceError(f"periodic full-screen refresh detected during idle window: {full}")
    diagnostic_union = rect_union_area(events, f"{label}.diagnostics")
    if diagnostic_union > int(WIDTH * HEIGHT * IDLE_DIRTY_MAX_UNION_RATIO):
        raise AcceptanceError(
            f"raw idle dirty union too large label={label} area={diagnostic_union} "
            f"limit_ratio={IDLE_DIRTY_MAX_UNION_RATIO}"
        )
    stream_after = validated_stream_snapshot(stream, f"{label}.stream")
    if stream_after[: len(stream_before)] != stream_before:
        raise AcceptanceError(f"stream history changed before idle marker label={label}")
    evidence = validate_idle_stream_window(stream_after[len(stream_before) :], elapsed, label)
    evidence.update({
        "diagnostic_event_count": len(events),
        "diagnostic_union_pixels": diagnostic_union,
    })
    return evidence


def verify_memory_baseline(first: dict, last: dict, label: str) -> None:
    if int(first["pid"]) != int(last["pid"]):
        raise AcceptanceError(f"memory baseline PID changed inside cycle label={label}")
    if int(last["vmrss_kib"]) > int(first["vmrss_kib"]) + MEMORY_TOLERANCE_KIB:
        raise AcceptanceError(f"VmRSS staircase label={label} first={first['vmrss_kib']} last={last['vmrss_kib']} KiB")
    if int(last["vmlck_kib"]) > int(first["vmlck_kib"]) + MEMORY_TOLERANCE_KIB:
        raise AcceptanceError(f"VmLck staircase label={label} first={first['vmlck_kib']} last={last['vmlck_kib']} KiB")
    if int(last["global_locked_kib"]) > int(first["global_locked_kib"]) + GLOBAL_LOCKED_TOLERANCE_KIB:
        raise AcceptanceError(
            f"global Locked staircase label={label} first={first['global_locked_kib']} "
            f"last={last['global_locked_kib']} KiB"
        )


def verify_memory_series(samples: list[dict], label: str) -> None:
    if len(samples) < 2:
        raise AcceptanceError(f"memory series is incomplete label={label} samples={len(samples)}")
    pids = {int(sample.get("pid") or 0) for sample in samples}
    if len(pids) != 1 or 0 in pids:
        raise AcceptanceError(f"memory series PID changed label={label} pids={sorted(pids)}")
    verify_memory_baseline(samples[0], samples[-1], label)
    for field in ("vmrss_kib", "vmlck_kib", "global_locked_kib"):
        values = [int(sample[field]) for sample in samples]
        tolerance = GLOBAL_LOCKED_TOLERANCE_KIB if field == "global_locked_kib" else MEMORY_TOLERANCE_KIB
        transient_tolerance = tolerance * 2
        if max(values) > values[0] + transient_tolerance:
            raise AcceptanceError(
                f"memory transient high-water exceeded label={label} field={field} "
                f"values={values} tolerance={transient_tolerance}"
            )


def validate_startup_cpu_evidence(result: dict) -> None:
    if result.get("cpu_lists") != ["1"]:
        raise AcceptanceError(f"pre-render executed outside CPU1: {result.get('cpu_lists')}")
    if int(result.get("sample_count") or 0) < MIN_STARTUP_CPU_SAMPLES:
        raise AcceptanceError(f"pre-render UI CPU sampling was incomplete: {result}")
    if int(result.get("cpu_window_count") or 0) < MIN_STARTUP_CPU_SAMPLES - 1:
        raise AcceptanceError(f"pre-render UI CPU window evidence was incomplete: {result}")
    if float(result.get("cpu_peak_percent") or 0.0) > MAX_STARTUP_CPU_PEAK_PERCENT:
        raise AcceptanceError(f"pre-render UI CPU peak exceeded gate: {result}")
    if int(result.get("cpu_sustained_ge95_windows") or 0) >= 3:
        raise AcceptanceError(f"pre-render sustained CPU saturation: {result}")
    if int(result.get("cpu1_sample_count") or 0) < MIN_STARTUP_CPU_SAMPLES:
        raise AcceptanceError(f"pre-render CPU1 /proc/stat sampling was incomplete: {result}")
    if int(result.get("cpu1_window_count") or 0) < MIN_STARTUP_CPU_SAMPLES - 1:
        raise AcceptanceError(f"pre-render CPU1 window evidence was incomplete: {result}")
    if float(result.get("cpu1_peak_percent") or 0.0) > MAX_STARTUP_CPU_PEAK_PERCENT:
        raise AcceptanceError(f"pre-render CPU1 peak exceeded gate: {result}")
    if int(result.get("cpu1_sustained_ge95_windows") or 0) >= 3:
        raise AcceptanceError(f"pre-render sustained CPU1 saturation: {result}")


def run_startup_acceptance(args: argparse.Namespace) -> dict:
    result = ssh_python(
        args,
        REMOTE_RESTART_MONITOR,
        env={"V5_ACCEPT_PORT": str(args.port)},
        timeout=240.0,
    )
    if int(result.get("returncode") or 0) != 0:
        raise AcceptanceError(f"v5-ui-relay restart failed: {result}")
    ready = result.get("ready")
    if not isinstance(ready, dict):
        raise AcceptanceError(f"startup monitor returned no ready metadata: {result}")
    validate_ready_metadata(ready)
    before_boot_id = str(result.get("before_boot_id") or "")
    before_ui_instance_id = str(result.get("before_ui_instance_id") or "")
    if not before_boot_id or before_boot_id != str(ready["boot_id"]):
        raise AcceptanceError(
            f"UI service restart crossed kernel boot boundary before={before_boot_id!r} "
            f"after={ready.get('boot_id')!r}"
        )
    if (not before_ui_instance_id or
            before_ui_instance_id == str(ready["ui_instance_id"])):
        raise AcceptanceError(
            f"UI service restart retained stale instance id={before_ui_instance_id!r}"
        )
    main_cache_trace = result.get("ui_main_cache_trace")
    if not isinstance(main_cache_trace, list) or not all(isinstance(line, str) for line in main_cache_trace):
        raise AcceptanceError(f"startup monitor returned no Main cache trace: {main_cache_trace!r}")
    try:
        validate_main_cache_trace(main_cache_trace)
    except ValueError as exc:
        raise AcceptanceError(f"startup Main cache trace invalid: {exc}") from exc
    validate_startup_cpu_evidence(result)
    statuses = [int(item.get("status") or 0) for item in result.get("gate_status_transitions", [])]
    if 503 not in statuses or 200 not in statuses or statuses.index(503) > max(index for index, value in enumerate(statuses) if value == 200):
        raise AcceptanceError(f"remote ready gate did not show 503->200 lifecycle: {statuses}")
    if result.get("framebuffer_sha256_after") in ("", hashlib.sha256(b"").hexdigest()):
        raise AcceptanceError("physical framebuffer was not readable after formal first-frame blit")
    return result


def run_cycle(args: argparse.Namespace, cycle: int, ready: dict) -> tuple[dict, dict]:
    remote_input = RemoteInput(args.relay_host, args.port, cycle)
    stream = StreamObserver(args.relay_host, args.port)
    stream.start()
    memory_samples: list[dict] = []
    cpu_windows: list[float] = []
    first_visible_events: list[dict] = []
    dirty_windows: list[dict] = []
    wait_stream_initialized(stream, f"cycle={cycle}")
    baseline = sample_ui(args)
    if int(baseline["pid"]) != int(ready.get("ui_pid") or 0):
        raise AcceptanceError(
            f"UI PID changed before same-process cycle={cycle} "
            f"ready_pid={ready.get('ui_pid')} actual_pid={baseline['pid']}"
        )
    memory_samples.append(baseline)
    try:
        for page, open_point, return_point in PAGE_STEPS:
            first_visible_events.append(
                click_for_full(
                    args, remote_input, open_point, f"cycle={cycle} open={page}", memory_samples, cpu_windows,
                    timeout=FIRST_LAZY_PAGE_DEADLINE_SECONDS if cycle == 1 else FIRST_VISIBLE_DEADLINE_SECONDS,
                )
            )
            dirty_window = verify_idle_dirty_is_partial(
                args, stream, args.idle_seconds, f"cycle={cycle} page={page}"
            )
            dirty_window["page"] = page
            dirty_windows.append(dirty_window)
            first_visible_events.append(
                click_for_full(
                    args, remote_input, return_point, f"cycle={cycle} return={page}", memory_samples, cpu_windows
                )
            )

        first_visible_events.append(
            click_for_full(
                args, remote_input, SETTINGS_OPEN, f"cycle={cycle} open=settings", memory_samples, cpu_windows,
                timeout=FIRST_LAZY_PAGE_DEADLINE_SECONDS if cycle == 1 else FIRST_VISIBLE_DEADLINE_SECONDS,
            )
        )
        first_visible_events.append(
            click_for_full(
                args,
                remote_input,
                SETTINGS_KEYBOARD_CELL,
                f"cycle={cycle} keyboard=open",
                memory_samples,
                cpu_windows,
            )
        )
        first_visible_events.append(
            click_for_full(
                args,
                remote_input,
                SETTINGS_KEYBOARD_ESC,
                f"cycle={cycle} keyboard=close",
                memory_samples,
                cpu_windows,
            )
        )

        first_visible_events.append(
            click_for_full(
                args,
                remote_input,
                SETTINGS_READ_DRIVE,
                f"cycle={cycle} popup=open",
                memory_samples,
                cpu_windows,
            )
        )
        popup_closed = False
        for attempt in range(30):
            before = int(diagnostics(args).get("frame_id") or 0)
            receipt = remote_input.click(POPUP_CLOSE)
            try:
                close_event = wait_full_event(
                    args,
                    before,
                    f"cycle={cycle} popup=close attempt={attempt + 1}",
                    release_ack_monotonic_ns=int(receipt["release_ack_monotonic_ns"]),
                )
                close_event["release_seq"] = int(receipt["release_seq"])
                first_visible_events.append(close_event)
            except FirstVisibleTimeout:
                time.sleep(0.5)
                continue
            probe_open_before = int(diagnostics(args).get("frame_id") or 0)
            open_receipt = remote_input.click(SETTINGS_KEYBOARD_CELL)
            probe_open_event = wait_full_event(
                args,
                probe_open_before,
                f"cycle={cycle} popup=closed keyboard_probe=open",
                release_ack_monotonic_ns=int(open_receipt["release_ack_monotonic_ns"]),
            )
            probe_open_event["release_seq"] = int(open_receipt["release_seq"])
            first_visible_events.append(probe_open_event)
            probe_close_before = int(diagnostics(args).get("frame_id") or 0)
            close_receipt = remote_input.click(SETTINGS_KEYBOARD_ESC)
            probe_close_event = wait_full_event(
                args,
                probe_close_before,
                f"cycle={cycle} popup=closed keyboard_probe=close",
                release_ack_monotonic_ns=int(close_receipt["release_ack_monotonic_ns"]),
            )
            probe_close_event["release_seq"] = int(close_receipt["release_seq"])
            first_visible_events.append(probe_close_event)
            popup_closed = True
            break
        if not popup_closed:
            raise AcceptanceError(f"settings progress popup did not restore its capture cycle={cycle}")

        time.sleep(1.0)
        settled = sample_ui(args)
        memory_samples.append(settled)
        verify_memory_series(memory_samples, f"cycle={cycle}")
        diag_at_end = diagnostics(args)
        metrics = diag_at_end.get("metrics") or {}
        if int(metrics.get("stream_repair_full_frames") or 0) != 0:
            raise AcceptanceError(f"full-frame repair occurred during cycle={cycle}: {metrics}")
        frames = validated_stream_snapshot(stream, f"cycle={cycle}")
        if not frames or frames[0].get("type") != "full_frame" or not frames[0].get("payload_nonuniform"):
            raise AcceptanceError(f"remote first visible frame invalid cycle={cycle}: {frames[:1]}")
    finally:
        stream.close()
        remote_input.close()

    cycle_result = {
        "cycle": cycle,
        "boot_id": str(ready["boot_id"]),
        "ui_instance_id": str(ready["ui_instance_id"]),
        "ui_pid": int(baseline["pid"]),
        "memory_baseline": baseline,
        "memory_settled": settled,
        "memory_samples": memory_samples,
        "cpu_action_peak_percent": round(max(cpu_windows) if cpu_windows else 0.0, 1),
        "first_visible_events": first_visible_events,
        "dirty_windows": dirty_windows,
        "stream_frames": frames,
        "relay_metrics": metrics,
    }
    return cycle_result, ready


def run_final_restart(args: argparse.Namespace, ready: dict) -> dict:
    previous_boot_id = str(ready["boot_id"])
    previous_ui_instance_id = str(ready["ui_instance_id"])
    previous_pid = int(ready["ui_pid"])
    remote_input = RemoteInput(args.relay_host, args.port, args.cycles + 1)
    try:
        receipt = remote_input.click(SETTINGS_SAVE_RESTART)
    finally:
        remote_input.close()
    next_ready = wait_ready(
        args, previous_ui_instance_id, timeout=args.restart_timeout)
    if int(next_ready.get("ui_pid") or 0) == previous_pid:
        raise AcceptanceError(f"final save/restart retained the old UI PID={previous_pid}")
    return {
        "previous_boot_id": previous_boot_id,
        "previous_ui_instance_id": previous_ui_instance_id,
        "previous_ui_pid": previous_pid,
        "release_seq": int(receipt["release_seq"]),
        "next_boot_id": str(next_ready["boot_id"]),
        "next_ui_instance_id": str(next_ready["ui_instance_id"]),
        "next_ui_pid": int(next_ready["ui_pid"]),
    }


def memory_trend(values: list[int]) -> dict:
    count = len(values)
    x_mean = (count - 1) / 2.0
    y_mean = sum(values) / count
    denominator = sum((index - x_mean) ** 2 for index in range(count))
    slope = (
        sum((index - x_mean) * (value - y_mean) for index, value in enumerate(values)) / denominator
        if denominator > 0
        else 0.0
    )
    first_median = float(statistics.median(values[:3]))
    last_median = float(statistics.median(values[-3:]))
    return {
        "values": values,
        "first_window_median": first_median,
        "last_window_median": last_median,
        "median_growth": last_median - first_median,
        "slope_kib_per_cycle": slope,
        "slope_span_kib": slope * (count - 1),
        "high_water_growth": max(values) - max(values[:3]),
        "endpoint_growth": values[-1] - values[0],
    }


def verify_cross_cycle_memory(cycles: list[dict]) -> dict:
    if len(cycles) < 10:
        raise AcceptanceError(f"same-process memory acceptance requires at least 10 cycles, got {len(cycles)}")
    cycle_pids = {int(item.get("ui_pid") or 0) for item in cycles}
    boot_ids = {str(item.get("boot_id") or "") for item in cycles}
    ui_instance_ids = {str(item.get("ui_instance_id") or "") for item in cycles}
    if (len(cycle_pids) != 1 or 0 in cycle_pids or
            len(boot_ids) != 1 or "" in boot_ids or
            len(ui_instance_ids) != 1 or "" in ui_instance_ids):
        raise AcceptanceError(
            f"10-round memory evidence crossed process/boot boundary "
            f"pids={sorted(cycle_pids)} boot_ids={sorted(boot_ids)} "
            f"ui_instance_ids={sorted(ui_instance_ids)}"
        )
    expected_pid = next(iter(cycle_pids))
    for item in cycles:
        sample_pids = {int(sample.get("pid") or 0) for sample in item.get("memory_samples", [])}
        if sample_pids != {expected_pid}:
            raise AcceptanceError(
                f"cycle memory samples crossed PID cycle={item.get('cycle')} "
                f"expected={expected_pid} actual={sorted(sample_pids)}"
            )
    evidence = {
        "ui_pid": expected_pid,
        "boot_id": next(iter(boot_ids)),
        "ui_instance_id": next(iter(ui_instance_ids)),
        "fields": {},
    }
    for field in ("vmrss_kib", "vmlck_kib", "global_locked_kib"):
        values = [int(item["memory_settled"][field]) for item in cycles]
        tolerance = GLOBAL_LOCKED_TOLERANCE_KIB if field == "global_locked_kib" else MEMORY_TOLERANCE_KIB
        trend = memory_trend(values)
        evidence["fields"][field] = dict(trend, tolerance_kib=tolerance)
        breaches = {
            "endpoint_growth": trend["endpoint_growth"],
            "median_growth": trend["median_growth"],
            "slope_span": trend["slope_span_kib"],
            "high_water_growth": trend["high_water_growth"],
        }
        if any(float(value) > tolerance for value in breaches.values()):
            raise AcceptanceError(
                f"cross-cycle {field} staircase/noise-masked growth values={values} "
                f"breaches={breaches} tolerance={tolerance}"
            )
    return evidence


def expect_acceptance_error(label: str, action) -> None:
    try:
        action()
    except AcceptanceError:
        return
    raise AssertionError(f"negative self-test unexpectedly passed: {label}")


def sample_stream_frames() -> list[dict]:
    return [
        {
            "type": "full_frame",
            "frame_id": 10,
            "base_frame_id": 0,
            "monotonic_ns": 100,
            "width": WIDTH,
            "height": HEIGHT,
            "stride": WIDTH * 4,
            "format": "bgra32",
            "dirty_count": 0,
            "rects": [],
            "payload_bytes": WIDTH * HEIGHT * 4,
            "payload_nonuniform": True,
        },
        {
            "type": "dirty_rects",
            "frame_id": 12,
            "base_frame_id": 10,
            "monotonic_ns": 200,
            "width": WIDTH,
            "height": HEIGHT,
            "stride": WIDTH * 4,
            "format": "bgra32",
            "dirty_count": 1,
            "rects": [{"x": 10, "y": 20, "w": 30, "h": 40, "codec": "raw"}],
            "payload_bytes": 30 * 40 * 4,
        },
    ]


def sample_memory_cycles(values: list[int], pid: int = 77) -> list[dict]:
    cycles = []
    for index, value in enumerate(values, 1):
        sample = {
            "pid": pid,
            "vmrss_kib": value,
            "vmlck_kib": 32000,
            "global_locked_kib": 64000,
        }
        cycles.append({
            "cycle": index,
            "boot_id": "same-boot",
            "ui_instance_id": "same-ui-instance",
            "ui_pid": pid,
            "memory_settled": dict(sample),
            "memory_samples": [dict(sample)],
        })
    return cycles


def self_test() -> int:
    ready = {
        "schema": "v5.ui_ready.v1",
        "ready": True,
        "boot_id": "11111111-1111-4111-8111-111111111111",
        "ui_instance_id": "22222222-2222-4222-8222-222222222222",
        "ui_pid": 77,
        "ui_start_ticks": 12345,
        "cache_budget_bytes": EXPECTED_CACHE_BUDGET_BYTES,
        "cache_policy": EXPECTED_CACHE_POLICY,
        "cache_page_count": 1,
        "cache_registered_page_count": REGISTERED_PAGE_COUNT,
        "main_cache": {"page": "main", "slot": 0},
        "cpus_allowed_list": "1",
        "first_frame": {
            "frame_id": 2,
            "base_frame_id": 1,
            "x": 0,
            "y": 0,
            "w": WIDTH,
            "h": HEIGHT,
        },
        "current_frame_id": 2,
    }
    validate_ready_metadata(ready)
    for field, value in (("boot_id", "not-a-uuid"), ("ui_instance_id", ""),
                         ("ui_start_ticks", 0)):
        invalid_ready = dict(ready, **{field: value})
        try:
            validate_ready_metadata(invalid_ready)
        except AcceptanceError:
            pass
        else:
            raise AssertionError(f"invalid ready identity accepted field={field}")
    validate_main_cache_trace(self_test_lines())
    compile(REMOTE_RESTART_MONITOR, "REMOTE_RESTART_MONITOR", "exec")
    assert '"before_ui_instance_id"' in REMOTE_RESTART_MONITOR
    assert '"cpu_window_count"' in REMOTE_RESTART_MONITOR
    assert '"cpu1_peak_percent"' in REMOTE_RESTART_MONITOR
    assert '"cpu1_sustained_ge95_windows"' in REMOTE_RESTART_MONITOR
    assert is_full_event({"x": 0, "y": 0, "w": WIDTH, "h": HEIGHT})
    assert not is_full_event({"x": 1, "y": 0, "w": WIDTH - 1, "h": HEIGHT})
    assert rect_union_area(
        [{"x": 0, "y": 0, "w": 10, "h": 10}, {"x": 5, "y": 0, "w": 10, "h": 10}],
        "self_test.union",
    ) == 150
    assert validate_dirty_chain(
        [
            {"frame_id": 11, "base_frame_id": 10, "x": 0, "y": 0, "w": 10, "h": 10},
            {"frame_id": 11, "base_frame_id": 10, "x": 100, "y": 100, "w": 10, "h": 10},
            {"frame_id": 12, "base_frame_id": 11, "x": 200, "y": 200, "w": 10, "h": 10},
        ],
        10,
        "self_test.multi_rect_frame",
    ) == 12

    first_visible = validate_first_visible_events(
        [
            {"frame_id": 11, "base_frame_id": 10, "x": 10, "y": 10, "w": 20, "h": 20},
            {"frame_id": 12, "base_frame_id": 11, "x": 0, "y": 0, "w": WIDTH, "h": HEIGHT},
        ],
        10,
        150.0,
        "self_test.first_visible",
    )
    assert first_visible is not None and first_visible["identity_scope"] == "frame_chain_only"
    expect_acceptance_error(
        "first_visible_base_gap",
        lambda: validate_first_visible_events(
            [{"frame_id": 12, "base_frame_id": 9, "x": 0, "y": 0, "w": WIDTH, "h": HEIGHT}],
            10,
            10.0,
            "self_test.base_gap",
        ),
    )
    expect_acceptance_error(
        "first_visible_late",
        lambda: validate_first_visible_events(
            [{"frame_id": 11, "base_frame_id": 10, "x": 0, "y": 0, "w": WIDTH, "h": HEIGHT}],
            10,
            201.0,
            "self_test.late",
        ),
    )
    expect_acceptance_error(
        "large_pre_target_feedback",
        lambda: validate_first_visible_events(
            [
                {"frame_id": 11, "base_frame_id": 10, "x": 0, "y": 0, "w": 800, "h": 100},
                {"frame_id": 12, "base_frame_id": 11, "x": 0, "y": 0, "w": WIDTH, "h": HEIGHT},
            ],
            10,
            10.0,
            "self_test.large_feedback",
        ),
    )

    frames = sample_stream_frames()
    validate_stream_frames(frames, "self_test.stream")
    validate_stream_observer_state(frames, "", None, "self_test.stream_state")
    expect_acceptance_error(
        "stream_observer_error",
        lambda: validate_stream_observer_state(
            frames, "ValueError: malformed metadata", None, "self_test.stream_error"
        ),
    )
    expect_acceptance_error(
        "stream_pending_payload",
        lambda: validate_stream_observer_state(
            frames, "", {"type": "dirty_rects"}, "self_test.stream_pending"
        ),
    )
    broken_stream = [dict(item) for item in frames]
    broken_stream[1]["base_frame_id"] = 9
    expect_acceptance_error(
        "stream_base_gap", lambda: validate_stream_frames(broken_stream, "self_test.stream_gap")
    )
    validate_idle_stream_window(frames[1:], 0.1, "self_test.idle")
    expect_acceptance_error(
        "idle_frequency",
        lambda: validate_idle_stream_window(frames[1:] * 6, 0.1, "self_test.idle_frequency"),
    )

    verify_cross_cycle_memory(sample_memory_cycles([1000] * 10))
    expect_acceptance_error(
        "memory_noise_masked_staircase",
        lambda: verify_cross_cycle_memory(
            sample_memory_cycles([1000, 950, 900, 2200, 900, 2300, 900, 2400, 900, 1900])
        ),
    )
    changed_pid = sample_memory_cycles([1000] * 10)
    changed_pid[-1]["ui_pid"] = 88
    changed_pid[-1]["memory_samples"][0]["pid"] = 88
    expect_acceptance_error(
        "memory_pid_change", lambda: verify_cross_cycle_memory(changed_pid)
    )
    changed_instance = sample_memory_cycles([1000] * 10)
    changed_instance[-1]["ui_instance_id"] = "different-ui-instance"
    expect_acceptance_error(
        "memory_ui_instance_change", lambda: verify_cross_cycle_memory(changed_instance)
    )
    valid_cpu = {
        "cpu_lists": ["1"],
        "sample_count": 5,
        "cpu_window_count": 4,
        "cpu_peak_percent": 80.0,
        "cpu_sustained_ge95_windows": 0,
        "cpu1_sample_count": 5,
        "cpu1_window_count": 4,
        "cpu1_peak_percent": 85.0,
        "cpu1_sustained_ge95_windows": 0,
    }
    validate_startup_cpu_evidence(valid_cpu)
    low_samples = dict(valid_cpu, sample_count=4)
    expect_acceptance_error(
        "startup_cpu_sample_floor", lambda: validate_startup_cpu_evidence(low_samples)
    )
    peak_cpu = dict(valid_cpu, cpu1_peak_percent=95.1)
    expect_acceptance_error(
        "startup_cpu_peak_gate", lambda: validate_startup_cpu_evidence(peak_cpu)
    )
    print("verify_v5_ui_first_frame_acceptance self-test PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify v5 UI first-frame cache behavior on the real board/operator path.")
    parser.add_argument("--apply", action="store_true", help="restart the board UI and execute the 10-round operator-path cycle")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--ssh", default=os.environ.get("V5_BOARD_SSH", ""))
    parser.add_argument("--ssh-port", type=int, default=int(os.environ.get("V5_BOARD_SSH_PORT", "22")))
    parser.add_argument("--ssh-key", default=os.environ.get("V5_BOARD_SSH_KEY", ""))
    parser.add_argument("--relay-host", default=os.environ.get("V5_BOARD_HOST", "192.168.1.221"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("V5_UI_REMOTE_PORT", "18080")))
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--idle-seconds", type=float, default=0.6)
    parser.add_argument("--restart-timeout", type=float, default=180.0)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.cycles < 10:
        parser.error("--cycles must be at least 10 for cache/memory staircase acceptance")
    if args.idle_seconds < 0.5:
        parser.error("--idle-seconds must be at least 0.5 for a meaningful dirty-rate window")
    if not args.apply:
        print("dry-run v5 UI first-frame acceptance:")
        print("  restart v5-ui-relay under a 20ms /proc CPU/RSS/VmLck sampler")
        print("  require exact main/settings/tool/probe/offset/io/network/program/mdi boot queue")
        print("  require CPU1 affinity, /proc/stat CPU1 saturation evidence, 503 ready gate, and one full main first frame")
        print(f"  execute {args.cycles} same-PID remote-pointer rounds across 9 pages, popup, and keyboard")
        print("  require release-to-full <=200ms, contiguous frame bases, partial dirty union/rate, and stable RSS/Locked")
        print("  perform one save/restart only after the same-PID 10-round memory gate")
        print("  --apply requires exclusive vm_board.lock ownership and V5_BOARD_SSH")
        return 0
    if not args.ssh:
        parser.error("--ssh or V5_BOARD_SSH is required with --apply")

    build_root = Path(os.environ.get("V5_BUILD_ROOT", Path.home() / "v5-build"))
    evidence_root = Path(os.environ.get("V5_EVIDENCE_ROOT", build_root / "evidence"))
    out = args.out or (evidence_root / "ui_first_frame" / f"acceptance_{time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())}.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    result: dict = {
        "schema": "v5.ui_first_frame.acceptance.v1",
        "started_realtime_ns": time.time_ns(),
        "cycles_required": args.cycles,
        "startup": None,
        "cycles": [],
        "same_pid_memory": None,
        "final_restart": None,
        "ok": False,
    }
    try:
        startup = run_startup_acceptance(args)
        result["startup"] = startup
        ready = startup["ready"]
        for cycle in range(1, args.cycles + 1):
            cycle_result, ready = run_cycle(args, cycle, ready)
            result["cycles"].append(cycle_result)
            out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        result["same_pid_memory"] = verify_cross_cycle_memory(result["cycles"])
        result["final_restart"] = run_final_restart(args, ready)
        result["ok"] = True
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        result["finished_realtime_ns"] = time.time_ns()
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
