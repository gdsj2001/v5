#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import hashlib
import ipaddress
import json
import mmap
import os
import select
import socket
import socketserver
import struct
import sys
import threading
import time
import urllib.parse
from collections import deque
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from typing import Union


PROTOCOL_VERSION = "8ax-remote-ui/1"
PIXEL_FORMAT = "bgra32"
RUN_DIR = Path("/run/8ax_v5_product_ui")
FRAMEBUFFER_NAME = "remote_framebuffer.bgra"
DIRTY_FIFO_NAME = "remote_dirty"
INPUT_FIFO_NAME = "remote_input"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class PayloadViews:
    def __init__(self, parts: list[memoryview], total_length: int):
        self.parts = parts
        self.total_length = total_length

    def __len__(self) -> int:
        return self.total_length

    def __bool__(self) -> bool:
        return self.total_length > 0


FramePayload = Union[bytes, bytearray, memoryview, PayloadViews]
STREAM_TARGET_FPS = 30
STREAM_COALESCE_SECONDS = 1.0 / STREAM_TARGET_FPS
DIRTY_EVENT_HISTORY_LIMIT = 512
MAX_DIRTY_RECTS_PER_FRAME = 64
LARGE_DIRTY_PIXEL_RATIO = 0.75
LARGE_DIRTY_MIN_INTERVAL_SECONDS = 0.10
STREAM_IDLE_PING_SECONDS = 15.0
DIRTY_FIFO_EMPTY_SLEEP_SECONDS = 0.02


def now_ms() -> int:
    return int(time.time() * 1000)


def parse_allow_cidrs(text: str):
    networks = []
    for item in (text or "").split(","):
        token = item.strip()
        if not token:
            continue
        if token == "*":
            return None
        networks.append(ipaddress.ip_network(token, strict=False))
    return networks


def peer_allowed(peer: str, networks) -> bool:
    if networks is None:
        return True
    try:
        addr = ipaddress.ip_address(peer)
    except ValueError:
        return False
    return any(addr in network for network in networks)


def system_metrics() -> dict:
    memory_used, memory_total = memory_used_total()
    disk_used, disk_total = disk_used_total()
    return {
        "cpu0_percent": cpu_percent("cpu0"),
        "cpu1_percent": cpu_percent("cpu1"),
        "memory_percent": percent_used(memory_used, memory_total),
        "disk_percent": percent_used(disk_used, disk_total),
        "memory_used_bytes": memory_used,
        "memory_total_bytes": memory_total,
        "disk_used_bytes": disk_used,
        "disk_total_bytes": disk_total,
    }


def cpu_samples_snapshot() -> dict:
    samples = {}
    for name in ("cpu0", "cpu1"):
        sample = read_cpu_sample(name)
        if sample is None:
            samples[name] = None
        else:
            total, idle = sample
            samples[name] = {"total": total, "idle": idle}
    return samples


def read_cpu_sample(name: str) -> tuple[int, int] | None:
    try:
        for line in Path("/proc/stat").read_text(encoding="ascii").splitlines():
            parts = line.split()
            if parts and parts[0] == name:
                values = [int(part) for part in parts[1:]]
                total = sum(values)
                idle = values[3] + (values[4] if len(values) > 4 else 0)
                if total <= 0:
                    return None
                return total, idle
    except OSError:
        return None
    return None


def read_proc_stat_ticks(path: Path) -> int | None:
    try:
        text = path.read_text(encoding="ascii", errors="ignore")
    except OSError:
        return None
    end = text.rfind(")")
    if end < 0:
        return None
    fields = text[end + 2:].split()
    if len(fields) <= 12:
        return None
    try:
        return int(fields[11]) + int(fields[12])
    except ValueError:
        return None


def read_status_field(path: Path, field: str) -> str:
    try:
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith(field + ":"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return ""


def process_diagnostics() -> dict:
    pid = os.getpid()
    task_dir = Path(f"/proc/{pid}/task")
    threads = []
    try:
        tasks = sorted(task_dir.glob("[0-9]*"), key=lambda item: int(item.name))
    except OSError:
        tasks = []
    for task in tasks:
        try:
            tid = int(task.name)
        except ValueError:
            continue
        try:
            comm = (task / "comm").read_text(encoding="utf-8", errors="ignore").strip()
        except OSError:
            comm = ""
        threads.append({
            "tid": tid,
            "comm": comm,
            "cpu_ticks": read_proc_stat_ticks(task / "stat"),
            "cpus_allowed_list": read_status_field(task / "status", "Cpus_allowed_list"),
        })
    return {
        "pid": pid,
        "cpu_ticks": read_proc_stat_ticks(Path(f"/proc/{pid}/stat")),
        "cpus_allowed_list": read_status_field(Path(f"/proc/{pid}/status"), "Cpus_allowed_list"),
        "threads": threads,
    }


class CpuUsageSampler:
    def __init__(self, sample_reader=read_cpu_sample):
        self._sample_reader = sample_reader
        self._previous: dict[str, tuple[int, int]] = {}
        self._lock = threading.Lock()

    def percent(self, name: str):
        sample = self._sample_reader(name)
        if sample is None:
            return None
        total, idle = sample
        with self._lock:
            previous = self._previous.get(name)
            self._previous[name] = sample
        if previous is None:
            return None
        previous_total, previous_idle = previous
        total_delta = total - previous_total
        idle_delta = idle - previous_idle
        if total_delta <= 0 or idle_delta < 0:
            return None
        busy = (1.0 - (idle_delta / total_delta)) * 100.0
        return round(max(0.0, min(100.0, busy)), 1)


_CPU_USAGE = CpuUsageSampler()


def cpu_percent(name: str):
    return _CPU_USAGE.percent(name)


def memory_used_total() -> tuple[int | None, int | None]:
    total = None
    available = None
    try:
        for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
            if line.startswith("MemTotal:"):
                total = int(line.split()[1]) * 1024
            elif line.startswith("MemAvailable:"):
                available = int(line.split()[1]) * 1024
    except OSError:
        return None, None
    if total is None:
        return None, None
    used = max(0, total - (available or 0))
    return used, total


def percent_used(used: int | None, total: int | None):
    if not used or not total:
        return None
    return round((used / total) * 100.0, 1)


def disk_used_total() -> tuple[int | None, int | None]:
    try:
        st = os.statvfs("/")
    except OSError:
        return None, None
    total = st.f_blocks * st.f_frsize
    free = st.f_bfree * st.f_frsize
    return max(0, total - free), total


class FrameState:
    def __init__(self, run_dir: Path, width: int, height: int):
        self.run_dir = run_dir
        self.width = width
        self.height = height
        self.stride = width * 4
        self.frame_id = 1
        self.latest_event: dict | None = None
        self.dirty_events = deque(maxlen=DIRTY_EVENT_HISTORY_LIMIT)
        self.condition = threading.Condition()
        self.metrics_lock = threading.Lock()
        self.metrics = {
            "full_frame_requests": 0,
            "stream_sessions": 0,
            "stream_active_sessions": 0,
            "stream_initial_full_frames": 0,
            "stream_repair_full_frames": 0,
            "stream_repair_missing_dirty_events": 0,
            "stream_idle_pings": 0,
            "stream_disconnects": 0,
            "stream_send_failures": 0,
            "dirty_events": 0,
            "dirty_coalesced_events": 0,
            "dirty_rect_frames": 0,
            "dirty_payload_bytes": 0,
            "dirty_payload_rows": 0,
            "dirty_payload_rects": 0,
            "dirty_payload_contiguous_frames": 0,
            "dirty_payload_union_frames": 0,
            "dirty_payload_union_source_rects": 0,
            "dirty_large_frames": 0,
            "dirty_large_pixels": 0,
            "dirty_large_throttle_sleeps": 0,
            "dirty_large_throttle_ms": 0,
            "dirty_fifo_empty_reads": 0,
            "framebuffer_mmap_refreshes": 0,
            "input_sessions": 0,
            "input_active_sessions": 0,
            "input_disconnects": 0,
            "input_messages": 0,
            "input_accepted": 0,
            "input_rejected": 0,
        }
        self._framebuffer_mmap: mmap.mmap | None = None
        self._framebuffer_key: tuple[int, int, int] | None = None

    @property
    def framebuffer_path(self) -> Path:
        return self.run_dir / FRAMEBUFFER_NAME

    @property
    def dirty_fifo_path(self) -> Path:
        return self.run_dir / DIRTY_FIFO_NAME

    @property
    def input_fifo_path(self) -> Path:
        return self.run_dir / INPUT_FIFO_NAME

    @property
    def frame_size(self) -> int:
        return self.stride * self.height

    def close_framebuffer_map(self) -> None:
        if self._framebuffer_mmap is not None:
            try:
                self._framebuffer_mmap.close()
            except BufferError:
                return
            except OSError:
                pass
        self._framebuffer_mmap = None
        self._framebuffer_key = None

    def framebuffer_map(self) -> mmap.mmap | None:
        try:
            st = self.framebuffer_path.stat()
        except OSError:
            self.close_framebuffer_map()
            return None
        if st.st_size < self.frame_size:
            self.close_framebuffer_map()
            return None
        key = (int(st.st_dev), int(st.st_ino), int(st.st_size))
        if self._framebuffer_mmap is not None and self._framebuffer_key == key:
            return self._framebuffer_mmap
        self.close_framebuffer_map()
        try:
            fd = os.open(self.framebuffer_path, os.O_RDONLY | getattr(os, "O_CLOEXEC", 0))
        except OSError:
            return None
        try:
            mapped = mmap.mmap(fd, self.frame_size, access=mmap.ACCESS_READ)
        except OSError:
            os.close(fd)
            return None
        os.close(fd)
        self._framebuffer_mmap = mapped
        self._framebuffer_key = key
        self.mark_metric("framebuffer_mmap_refreshes")
        return mapped

    def full_frame(self) -> tuple[int, bytes] | None:
        mapped = self.framebuffer_map()
        if mapped is None:
            return None
        data = mapped[:self.frame_size]
        if len(data) != self.frame_size:
            return None
        with self.condition:
            frame_id = max(1, self.frame_id)
        return frame_id, data

    def mark_metric(self, name: str, delta: int = 1) -> None:
        with self.metrics_lock:
            self.metrics[name] = int(self.metrics.get(name, 0)) + delta

    def decrement_metric_floor(self, name: str) -> None:
        with self.metrics_lock:
            self.metrics[name] = max(0, int(self.metrics.get(name, 0)) - 1)

    def metrics_snapshot(self) -> dict:
        with self.metrics_lock:
            return dict(self.metrics)

    def _single_union_rect(self, rects: list[dict]) -> dict | None:
        if not rects:
            return None
        x1 = self.width
        y1 = self.height
        x2 = 0
        y2 = 0
        for rect in rects:
            x = int(rect["x"])
            y = int(rect["y"])
            w = int(rect["w"])
            h = int(rect["h"])
            x1 = min(x1, x)
            y1 = min(y1, y)
            x2 = max(x2, x + w)
            y2 = max(y2, y + h)
        if x1 >= x2 or y1 >= y2:
            return None
        return {"x": x1, "y": y1, "w": x2 - x1, "h": y2 - y1, "codec": "raw"}

    def normalized_dirty_rects(self, event: dict) -> list[dict] | None:
        source_rects = event.get("rects")
        if source_rects is None:
            source_rects = [event]
        rects = []
        for source in source_rects:
            x = int(source["x"])
            y = int(source["y"])
            w = int(source["w"])
            h = int(source["h"])
            if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > self.width or y + h > self.height:
                return None
            rects.append({"x": x, "y": y, "w": w, "h": h, "codec": "raw"})
        if len(rects) > MAX_DIRTY_RECTS_PER_FRAME:
            union = self._single_union_rect(rects)
            self.mark_metric("dirty_payload_union_frames")
            self.mark_metric("dirty_payload_union_source_rects", len(rects))
            return [union] if union else None
        return rects

    def dirty_area_pixels(self, event: dict) -> int:
        source_rects = event.get("rects")
        if source_rects is None:
            source_rects = [event]
        rects = []
        for source in source_rects:
            rects.append({
                "x": int(source["x"]),
                "y": int(source["y"]),
                "w": int(source["w"]),
                "h": int(source["h"]),
            })
        if len(rects) > MAX_DIRTY_RECTS_PER_FRAME:
            union = self._single_union_rect(rects)
            if union is None:
                return 0
            return max(0, int(union["w"])) * max(0, int(union["h"]))
        return sum(max(0, int(rect["w"])) * max(0, int(rect["h"])) for rect in rects)

    def is_large_dirty(self, event: dict) -> bool:
        frame_pixels = max(1, self.width * self.height)
        threshold = max(1, int(frame_pixels * LARGE_DIRTY_PIXEL_RATIO))
        return self.dirty_area_pixels(event) >= threshold

    def throttle_large_dirty(self, event: dict, last_sent_at: float) -> float:
        if not self.is_large_dirty(event):
            return last_sent_at
        pixels = self.dirty_area_pixels(event)
        self.mark_metric("dirty_large_frames")
        self.mark_metric("dirty_large_pixels", pixels)
        if last_sent_at > 0.0:
            delay = LARGE_DIRTY_MIN_INTERVAL_SECONDS - (time.monotonic() - last_sent_at)
            if delay > 0.0:
                self.mark_metric("dirty_large_throttle_sleeps")
                self.mark_metric("dirty_large_throttle_ms", int(round(delay * 1000.0)))
                time.sleep(delay)
        return time.monotonic()

    def dirty_payload(self, event: dict) -> tuple[FramePayload, list[dict]] | None:
        rects = self.normalized_dirty_rects(event)
        if not rects:
            return None
        mapped = self.framebuffer_map()
        if mapped is None:
            return None
        total_bytes = sum(int(rect["w"]) * int(rect["h"]) * 4 for rect in rects)
        total_rows = sum(int(rect["h"]) for rect in rects)
        src = memoryview(mapped)
        payload_parts: list[memoryview] = []
        for rect in rects:
            x = int(rect["x"])
            y = int(rect["y"])
            w = int(rect["w"])
            h = int(rect["h"])
            row_bytes = w * 4
            if x == 0 and w == self.width:
                start = (y * self.stride) + (x * 4)
                end = start + (row_bytes * h)
                if end > self.frame_size:
                    return None
                payload_parts.append(src[start:end])
                self.mark_metric("dirty_payload_contiguous_frames")
                continue
            for row in range(h):
                src_start = ((y + row) * self.stride) + (x * 4)
                src_end = src_start + row_bytes
                if src_end > self.frame_size:
                    return None
                payload_parts.append(src[src_start:src_end])
        self.mark_metric("dirty_payload_bytes", total_bytes)
        self.mark_metric("dirty_payload_rows", total_rows)
        self.mark_metric("dirty_payload_rects", len(rects))
        return PayloadViews(payload_parts, total_bytes), rects

    def publish_dirty(self, event: dict) -> None:
        with self.condition:
            if int(event["frame_id"]) >= self.frame_id:
                self.frame_id = int(event["frame_id"])
            stored = dict(event)
            self.latest_event = stored
            self.dirty_events.append(stored)
            self.condition.notify_all()

    def wait_dirty_batch_after(self, frame_id: int, timeout: float, coalesce_seconds: float) -> dict | None:
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                if any(int(event["frame_id"]) > frame_id for event in self.dirty_events):
                    break
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.condition.wait(min(remaining, 1.0))
            settle_deadline = time.monotonic() + max(0.0, coalesce_seconds)
            while True:
                remaining = settle_deadline - time.monotonic()
                if remaining <= 0:
                    break
                self.condition.wait(remaining)
            events = [dict(event) for event in self.dirty_events if int(event["frame_id"]) > frame_id]
        return self.coalesce_dirty_events(frame_id, events)

    def coalesce_dirty_events(self, base_frame_id: int, events: list[dict]) -> dict | None:
        if not events:
            return None
        events.sort(key=lambda event: int(event["frame_id"]))
        expected_base = int(base_frame_id)
        latest_frame = expected_base
        rects = []
        merged = 0
        for event in events:
            event_frame = int(event["frame_id"])
            event_base = int(event["base_frame_id"])
            if event_frame <= expected_base:
                continue
            if event_base != expected_base:
                return {
                    "needs_full": True,
                    "frame_id": max(latest_frame, event_frame),
                    "base_frame_id": base_frame_id,
                    "reason": "missing_dirty_event",
                }
            x = int(event["x"])
            y = int(event["y"])
            w = int(event["w"])
            h = int(event["h"])
            rects.append({"x": x, "y": y, "w": w, "h": h, "codec": "raw"})
            expected_base = event_frame
            latest_frame = event_frame
            merged += 1
        if merged == 0:
            return None
        return {
            "frame_id": latest_frame,
            "base_frame_id": int(base_frame_id),
            "rects": rects,
            "merged_events": merged,
        }


class DirtyReader(threading.Thread):
    def __init__(self, state: FrameState):
        super().__init__(daemon=True)
        self.state = state
        self.stop_requested = threading.Event()

    def run(self) -> None:
        self.state.run_dir.mkdir(parents=True, exist_ok=True)
        try:
            os.mkfifo(self.state.dirty_fifo_path, 0o600)
        except FileExistsError:
            pass
        fd = os.open(self.state.dirty_fifo_path, os.O_RDWR | os.O_NONBLOCK)
        buffer = b""
        try:
            while not self.stop_requested.is_set():
                ready, _, _ = select.select([fd], [], [], 0.5)
                if not ready:
                    continue
                try:
                    data = os.read(fd, 4096)
                except BlockingIOError:
                    time.sleep(DIRTY_FIFO_EMPTY_SLEEP_SECONDS)
                    continue
                if not data:
                    self.state.mark_metric("dirty_fifo_empty_reads")
                    time.sleep(DIRTY_FIFO_EMPTY_SLEEP_SECONDS)
                    continue
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    self.handle_line(line.decode("ascii", errors="ignore").strip())
        finally:
            os.close(fd)

    def handle_line(self, line: str) -> None:
        parts = line.split()
        if len(parts) < 6:
            return
        try:
            frame_id, base_frame_id, x, y, w, h = [int(part) for part in parts[:6]]
        except ValueError:
            return
        if frame_id <= 0 or base_frame_id < 0 or w <= 0 or h <= 0:
            return
        if x < 0 or y < 0 or x + w > self.state.width or y + h > self.state.height:
            return
        self.state.mark_metric("dirty_events")
        self.state.publish_dirty({
            "frame_id": frame_id,
            "base_frame_id": base_frame_id,
            "x": x,
            "y": y,
            "w": w,
            "h": h,
        })


def ws_accept_value(key: str) -> str:
    digest = hashlib.sha1((key + WS_GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def payload_parts(payload: FramePayload) -> list[memoryview]:
    if isinstance(payload, PayloadViews):
        return payload.parts
    return [memoryview(payload)]


def send_payload(sock: socket.socket, payload: FramePayload) -> None:
    if not payload:
        return
    parts = payload_parts(payload)
    if len(parts) == 1:
        sock.sendall(parts[0])
        return
    if not hasattr(sock, "sendmsg"):
        for part in parts:
            if part:
                sock.sendall(part)
        return
    views = [part for part in parts if part]
    max_iov = 256
    if "SC_IOV_MAX" in os.sysconf_names:
        max_iov = min(int(os.sysconf("SC_IOV_MAX")), max_iov)
    while views:
        chunk = views[:max_iov]
        sent = sock.sendmsg(chunk)
        if sent <= 0:
            raise ConnectionError("socket sendmsg returned no progress")
        remaining = sent
        while views and remaining >= len(views[0]):
            remaining -= len(views[0])
            views.pop(0)
        if views and remaining > 0:
            views[0] = views[0][remaining:]


def send_ws_frame(sock: socket.socket, opcode: int, payload: FramePayload) -> None:
    first = bytes([0x80 | opcode])
    size = len(payload)
    if size < 126:
        header = first + bytes([size])
    elif size <= 65535:
        header = first + bytes([126]) + struct.pack("!H", size)
    else:
        header = first + bytes([127]) + struct.pack("!Q", size)
    sock.sendall(header)
    send_payload(sock, payload)


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("websocket closed")
        data.extend(chunk)
    return bytes(data)


def recv_ws_frame(sock: socket.socket) -> tuple[int, bytes]:
    header = recv_exact(sock, 2)
    opcode = header[0] & 0x0F
    masked = (header[1] & 0x80) != 0
    size = header[1] & 0x7F
    if size == 126:
        size = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", recv_exact(sock, 8))[0]
    if not masked:
        raise ConnectionError("client websocket frame is not masked")
    mask = recv_exact(sock, 4)
    payload = bytearray(recv_exact(sock, size))
    for index, value in enumerate(payload):
        payload[index] = value ^ mask[index & 3]
    return opcode, bytes(payload)


def frame_metadata(kind: str, frame_id: int, base_frame_id: int, width: int, height: int, stride: int, rects: list[dict]) -> dict:
    return {
        "type": kind,
        "frame_id": frame_id,
        "base_frame_id": base_frame_id,
        "monotonic_ns": time.monotonic_ns(),
        "width": width,
        "height": height,
        "stride": stride,
        "format": PIXEL_FORMAT,
        "dirty_count": len(rects),
        "rects": rects,
    }


class RemoteRelayHandler(BaseHTTPRequestHandler):
    server_version = "V5RemoteUiRelay/1"

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    @property
    def state(self) -> FrameState:
        return self.server.state

    def check_peer(self) -> bool:
        peer = self.client_address[0]
        if peer_allowed(peer, self.server.allow_networks):
            return True
        self.write_json(403, {"ok": False, "error": "remote_peer_not_allowed"})
        return False

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path.startswith("/remote/") and not self.check_peer():
            return
        if parsed.path == "/remote/info":
            self.handle_info()
        elif parsed.path == "/remote/frame/full":
            self.handle_full_frame()
        elif parsed.path == "/remote/stream" and self.is_ws_request():
            self.handle_stream()
        elif parsed.path == "/remote/input" and self.is_ws_request():
            self.handle_input()
        elif parsed.path == "/remote/diagnostics":
            self.write_json(200, {
                "schema": "re.v5.remote_diagnostics.v1",
                "protocol_version": PROTOCOL_VERSION,
                "framebuffer": str(self.state.framebuffer_path),
                "dirty_fifo": str(self.state.dirty_fifo_path),
                "input_fifo": str(self.state.input_fifo_path),
                "frame_id": self.state.frame_id,
                "metrics": self.state.metrics_snapshot(),
                "cpu_samples": cpu_samples_snapshot(),
                "process": process_diagnostics(),
            })
        else:
            self.write_json(404, {"ok": False, "error": "unsupported_remote_path"})

    def handle_info(self) -> None:
        input_enabled = os.environ.get("V5_UI_REMOTE_INPUT", "off") == "layout_only"
        self.write_json(200, {
            "protocol_version": PROTOCOL_VERSION,
            "width": self.state.width,
            "height": self.state.height,
            "pixel_format": PIXEL_FORMAT,
            "stride": self.state.stride,
            "view_only": not input_enabled,
            "input_enabled": input_enabled,
            "system_metrics": system_metrics(),
        })

    def handle_full_frame(self) -> None:
        frame = self.state.full_frame()
        if frame is None:
            self.write_json(503, {"ok": False, "error": "remote_framebuffer_unavailable"})
            return
        self.state.mark_metric("full_frame_requests")
        frame_id, payload = frame
        meta = frame_metadata("full_frame", frame_id, 0, self.state.width, self.state.height, self.state.stride, [])
        meta_bytes = json.dumps(meta, separators=(",", ":")).encode("utf-8")
        self.write_frame_envelope(meta_bytes, payload)

    def handle_stream(self) -> None:
        if not self.accept_websocket():
            return
        self.state.mark_metric("stream_sessions")
        self.state.mark_metric("stream_active_sessions")
        sock = self.connection
        last_large_dirty_sent_at = 0.0
        try:
            frame = self.state.full_frame()
            if frame is None:
                return
            frame_id, payload = frame
            self.state.mark_metric("stream_initial_full_frames")
            self.send_frame(sock, frame_metadata("full_frame", frame_id, 0, self.state.width, self.state.height, self.state.stride, []), payload)
            last_sent = frame_id
            while True:
                batch = self.state.wait_dirty_batch_after(last_sent, STREAM_IDLE_PING_SECONDS, STREAM_COALESCE_SECONDS)
                if batch is None:
                    send_ws_frame(sock, 0x9, b"")
                    self.state.mark_metric("stream_idle_pings")
                    continue
                if batch.get("needs_full"):
                    frame = self.state.full_frame()
                    if frame is None:
                        continue
                    frame_id, payload = frame
                    self.state.mark_metric("stream_repair_missing_dirty_events")
                    self.state.mark_metric("stream_repair_full_frames")
                    self.send_frame(sock, frame_metadata("full_frame", frame_id, 0, self.state.width, self.state.height, self.state.stride, []), payload)
                    last_sent = frame_id
                    continue
                last_large_dirty_sent_at = self.state.throttle_large_dirty(batch, last_large_dirty_sent_at)
                prepared = self.state.dirty_payload(batch)
                if prepared is None:
                    continue
                payload, rects = prepared
                merged = int(batch.get("merged_events", 1))
                if merged > 1:
                    self.state.mark_metric("dirty_coalesced_events", merged - 1)
                meta = frame_metadata("dirty_rects", int(batch["frame_id"]), int(batch["base_frame_id"]), self.state.width, self.state.height, self.state.stride, rects)
                self.state.mark_metric("dirty_rect_frames")
                self.send_frame(sock, meta, payload)
                last_sent = int(batch["frame_id"])
        except (BrokenPipeError, ConnectionError, ConnectionResetError, OSError):
            self.state.mark_metric("stream_send_failures")
        finally:
            self.state.mark_metric("stream_disconnects")
            self.state.decrement_metric_floor("stream_active_sessions")

    def handle_input(self) -> None:
        if not self.accept_websocket():
            return
        self.state.mark_metric("input_sessions")
        self.state.mark_metric("input_active_sessions")
        input_enabled = os.environ.get("V5_UI_REMOTE_INPUT", "off") == "layout_only"
        session_id = ""
        sock = self.connection
        try:
            while True:
                opcode, payload = recv_ws_frame(sock)
                if opcode == 0x8:
                    return
                if opcode != 0x1:
                    return
                self.state.mark_metric("input_messages")
                try:
                    message = json.loads(payload.decode("utf-8"))
                except json.JSONDecodeError:
                    self.send_ack(sock, "pointer_reject", session_id, 0, "unknown", False, "invalid_json")
                    continue
                msg_type = str(message.get("type") or "")
                if msg_type == "control_request":
                    session_id = str(message.get("session_id") or "")
                    self.send_ack(sock, "control_grant" if input_enabled and session_id else "control_revoke", session_id, 0, "control", input_enabled and bool(session_id), None if input_enabled and session_id else "remote_input_disabled")
                elif msg_type == "pointer_event":
                    seq = int(message.get("seq") or 0)
                    phase = str(message.get("phase") or "unknown")
                    message_session = str(message.get("session_id") or "")
                    if not input_enabled:
                        self.send_ack(sock, "pointer_reject", message_session, seq, phase, False, "remote_input_disabled")
                    elif session_id and message_session != session_id:
                        self.send_ack(sock, "pointer_reject", message_session, seq, phase, False, "session_mismatch")
                    elif self.write_input_event(message):
                        self.send_ack(sock, "pointer_ack", message_session, seq, phase, True, None)
                    else:
                        self.send_ack(sock, "pointer_reject", message_session, seq, phase, False, "remote_input_ipc_unavailable")
                else:
                    self.send_ack(sock, "pointer_reject", session_id, 0, "unknown", False, "unsupported_type")
        except (BrokenPipeError, ConnectionError, ConnectionResetError, OSError):
            pass
        finally:
            self.state.mark_metric("input_disconnects")
            self.state.decrement_metric_floor("input_active_sessions")

    def write_input_event(self, message: dict) -> bool:
        try:
            self.state.run_dir.mkdir(parents=True, exist_ok=True)
            try:
                os.mkfifo(self.state.input_fifo_path, 0o600)
            except FileExistsError:
                pass
            fd = os.open(self.state.input_fifo_path, os.O_WRONLY | os.O_NONBLOCK)
            try:
                payload = json.dumps(message, separators=(",", ":")).encode("utf-8") + b"\n"
                os.write(fd, payload)
                return True
            finally:
                os.close(fd)
        except OSError:
            return False

    def send_ack(self, sock: socket.socket, msg_type: str, session_id: str, seq: int, phase: str, accepted: bool, reason: str | None) -> None:
        if msg_type == "pointer_ack":
            self.state.mark_metric("input_accepted")
        elif msg_type == "pointer_reject":
            self.state.mark_metric("input_rejected")
        payload = {
            "type": msg_type,
            "session_id": session_id,
            "seq": seq,
            "phase": phase,
            "accepted": accepted,
            "server_time_ms": now_ms(),
            "reason": reason,
        }
        send_ws_frame(sock, 0x1, json.dumps(payload, separators=(",", ":")).encode("utf-8"))

    def send_frame(self, sock: socket.socket, meta: dict, payload: FramePayload) -> None:
        send_ws_frame(sock, 0x1, json.dumps(meta, separators=(",", ":")).encode("utf-8"))
        send_ws_frame(sock, 0x2, payload)

    def is_ws_request(self) -> bool:
        return self.headers.get("Upgrade", "").lower() == "websocket"

    def accept_websocket(self) -> bool:
        key = self.headers.get("Sec-WebSocket-Key", "")
        if not key:
            self.write_json(400, {"ok": False, "error": "missing_websocket_key"})
            return False
        response = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {ws_accept_value(key)}\r\n\r\n"
        ).encode("ascii")
        self.connection.sendall(response)
        self.close_connection = True
        return True

    def write_json(self, status: int, body: dict) -> None:
        payload = json.dumps(body, separators=(",", ":")).encode("utf-8") + b"\n"
        self.write_bytes(status, "application/json; charset=utf-8", payload)

    def write_bytes(self, status: int, content_type: str, payload: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def write_frame_envelope(self, meta_bytes: bytes, payload: FramePayload) -> None:
        meta_len = struct.pack("<I", len(meta_bytes))
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(meta_len) + len(meta_bytes) + len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(meta_len)
        self.wfile.write(meta_bytes)
        self.wfile.write(payload)


class RemoteRelayServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, handler, state: FrameState, allow_networks):
        super().__init__(address, handler)
        self.state = state
        self.allow_networks = allow_networks


def main() -> int:
    parser = argparse.ArgumentParser(description="v5 remote UI relay using UI mmap/FIFO IPC.")
    parser.add_argument("--host", default=os.environ.get("V5_UI_REMOTE_BIND", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("V5_UI_REMOTE_PORT", "18080")))
    parser.add_argument("--allow-cidrs", default=os.environ.get("V5_UI_REMOTE_ALLOW_CIDRS", "127.0.0.1/32,192.168.1.0/24"))
    parser.add_argument("--run-dir", default=str(RUN_DIR))
    parser.add_argument("--width", type=int, default=int(os.environ.get("V5_UI_REMOTE_WIDTH", "1024")))
    parser.add_argument("--height", type=int, default=int(os.environ.get("V5_UI_REMOTE_HEIGHT", "600")))
    args = parser.parse_args()

    state = FrameState(Path(args.run_dir), args.width, args.height)
    dirty_reader = DirtyReader(state)
    dirty_reader.start()
    allow_networks = parse_allow_cidrs(args.allow_cidrs)
    with RemoteRelayServer((args.host, args.port), RemoteRelayHandler, state, allow_networks) as server:
        print(f"v5_remote_ui_relay listening host={args.host} port={args.port} run_dir={state.run_dir} allow_cidrs={args.allow_cidrs}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            return 0
        finally:
            dirty_reader.stop_requested.set()
            state.close_framebuffer_map()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
