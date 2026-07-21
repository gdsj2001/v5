from __future__ import annotations

import ctypes
import errno
import json
import math
import mmap
import os
import select
import threading
import time
import uuid
from collections import deque
from pathlib import Path

from v5_remote_ui_contract import (
    DIRTY_EVENT_HISTORY_LIMIT,
    DIRTY_FIFO_EMPTY_SLEEP_SECONDS,
    DIRTY_FIFO_NAME,
    FRAMEBUFFER_NAME,
    INPUT_FIFO_NAME,
    LARGE_DIRTY_MIN_INTERVAL_SECONDS,
    LARGE_DIRTY_PIXEL_RATIO,
    MAX_DIRTY_RECTS_PER_FRAME,
    FramePayload,
)
from v5_remote_ui_dirty_geometry import DirtyGeometryNormalizer

LOCK_SH = 1
LOCK_EX = 2
LOCK_UN = 8
_LIBC = ctypes.CDLL(None, use_errno=True)
_LIBC.flock.argtypes = (ctypes.c_int, ctypes.c_int)
_LIBC.flock.restype = ctypes.c_int


def flock(fd: int, operation: int) -> None:
    while _LIBC.flock(fd, operation) != 0:
        error_number = ctypes.get_errno()
        if error_number != errno.EINTR:
            raise OSError(error_number, os.strerror(error_number))

def canonical_uuid(value: object) -> str | None:
    text = str(value or "").strip().lower()
    try:
        parsed = uuid.UUID(text)
    except (ValueError, AttributeError):
        return None
    return text if str(parsed) == text else None

def process_start_ticks(proc_root: Path, pid: int) -> int:
    raw = (proc_root / str(pid) / "stat").read_text(encoding="ascii")
    tail = raw.rsplit(")", 1)
    if len(tail) != 2:
        raise ValueError(f"invalid process stat pid={pid}")
    fields = tail[1].split()
    if len(fields) <= 19:
        raise ValueError(f"short process stat pid={pid}")
    return int(fields[19])

class FrameState:
    def __init__(self, run_dir: Path, width: int, height: int, dirty_packer,
                 ready_path: Path | None = None, *, proc_root: Path = Path("/proc"),
                 boot_id_path: Path = Path("/proc/sys/kernel/random/boot_id"),
                 process_pid: int | None = None):
        self.run_dir = run_dir
        self.width = width
        self.height = height
        self.stride = width * 4
        self.frame_id = 1
        self.first_event: dict | None = None
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
            "stream_runtime_resets": 0,
            "stream_runtime_history_gap_disconnects": 0,
            "stream_runtime_invalid_dirty_disconnects": 0,
            "stream_idle_pings": 0,
            "stream_disconnects": 0,
            "stream_send_failures": 0,
            "dirty_events": 0,
            "dirty_coalesced_events": 0,
            "dirty_rect_frames": 0,
            "dirty_payload_bytes": 0,
            "dirty_payload_rows": 0,
            "dirty_payload_rects": 0,
            "dirty_payload_native_frames": 0,
            "dirty_payload_native_failures": 0,
            "dirty_payload_contiguous_frames": 0,
            "dirty_payload_union_frames": 0,
            "dirty_payload_union_source_rects": 0,
            "dirty_payload_bounded_merge_frames": 0,
            "dirty_payload_bounded_merge_source_rects": 0,
            "dirty_payload_bounded_merge_output_rects": 0,
            "dirty_payload_bounded_merge_added_pixels": 0,
            "dirty_payload_shared_build_ms": 0,
            "dirty_payload_shared_backpressure_sleeps": 0,
            "dirty_payload_shared_backpressure_ms": 0,
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
        self._framebuffer_fd: int | None = None
        self._framebuffer_key: tuple[int, int, int] | None = None
        self._ready_path = ready_path or (run_dir / "ui_ready.json")
        self._proc_root = proc_root
        self._boot_id_path = boot_id_path
        self._process_pid = os.getpid() if process_pid is None else int(process_pid)
        self._ready_lock = threading.Lock()
        self._ready_metadata: dict | None = None
        self._ready_file_key: tuple[int, int, int, int] | None = None
        self._system_boot_id: str | None = None
        self._dirty_geometry = DirtyGeometryNormalizer(width, height, self.mark_metric)
        self._dirty_packer = dirty_packer
        self._display_cpu_metrics: dict | None = None

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
    def ready_path(self) -> Path:
        return self._ready_path

    def _clear_ready_metadata(self) -> None:
        with self._ready_lock:
            self._ready_metadata = None
            self._ready_file_key = None

    def _current_boot_id(self) -> str | None:
        if self._system_boot_id is None:
            try:
                self._system_boot_id = canonical_uuid(
                    self._boot_id_path.read_text(encoding="ascii"))
            except OSError:
                return None
        return self._system_boot_id

    def _ready_identity_valid(self, payload: dict) -> bool:
        try:
            boot_id = canonical_uuid(payload.get("boot_id"))
            ui_instance_id = canonical_uuid(payload.get("ui_instance_id"))
            ui_pid = int(payload.get("ui_pid") or 0)
            ui_start_ticks = int(payload.get("ui_start_ticks") or 0)
        except (TypeError, ValueError):
            return False
        if (boot_id is None or boot_id != self._current_boot_id() or
                ui_instance_id is None or ui_pid <= 0 or ui_start_ticks <= 0 or
                not self._publisher_barrier_valid(payload.get("publisher_barrier"))):
            return False
        try:
            return process_start_ticks(self._proc_root, ui_pid) == ui_start_ticks
        except (OSError, ValueError):
            return False

    @staticmethod
    def _publisher_barrier_valid(barrier: object) -> bool:
        if not isinstance(barrier, dict):
            return False
        identities = barrier.get("owner_identities")
        baseline = barrier.get("baseline_markers")
        final = barrier.get("final_markers")
        if (not isinstance(identities, dict) or
                set(identities) != {"position", "wcs", "state"} or
                not isinstance(baseline, dict) or
                not isinstance(final, dict) or
                set(baseline) != {"position", "state", "wcs", "modal"} or
                set(final) != set(baseline)):
            return False
        try:
            for name, token in identities.items():
                if (not isinstance(token, dict) or int(token.get("pid") or 0) <= 0 or
                        int(token.get("start_ticks") or 0) <= 0 or
                        not isinstance(token.get("argv"), list) or not token["argv"]):
                    return False
                if name == "position" and int(token.get("writer_identity") or 0) <= 0:
                    return False
            baseline_values = {name: int(value or 0) for name, value in baseline.items()}
            final_values = {name: int(value or 0) for name, value in final.items()}
        except (TypeError, ValueError):
            return False
        return (all(baseline_values.values()) and all(final_values.values()) and
                all(final_values[name] >= baseline_values[name] for name in final_values))

    def _failure_identity_valid(self, payload: dict) -> bool:
        try:
            boot_id = canonical_uuid(payload.get("boot_id"))
            startup_instance_id = canonical_uuid(payload.get("startup_instance_id"))
            owner_pid = int(payload.get("owner_pid") or 0)
            owner_start_ticks = int(payload.get("owner_start_ticks") or 0)
        except (TypeError, ValueError):
            return False
        owner_role = payload.get("owner_role")
        if (boot_id is None or boot_id != self._current_boot_id() or
                startup_instance_id is None or
                owner_role not in ("relay", "supervisor") or
                (owner_role == "relay" and owner_pid != self._process_pid) or
                owner_pid <= 0 or owner_start_ticks <= 0 or
                not str(payload.get("stage") or "").strip() or
                not str(payload.get("error") or "").strip()):
            return False
        try:
            return process_start_ticks(self._proc_root, owner_pid) == owner_start_ticks
        except (OSError, ValueError):
            return False

    @staticmethod
    def _file_key(stat_result: os.stat_result) -> tuple[int, int, int, int]:
        return (int(stat_result.st_dev), int(stat_result.st_ino),
                int(stat_result.st_size), int(stat_result.st_mtime_ns))

    def _lifecycle_metadata(self) -> dict | None:
        payload: dict | None = None
        key: tuple[int, int, int, int] | None = None
        for _attempt in range(2):
            try:
                with self.ready_path.open("r", encoding="utf-8") as stream:
                    key = self._file_key(os.fstat(stream.fileno()))
                    with self._ready_lock:
                        if self._ready_file_key == key and self._ready_metadata is not None:
                            payload = dict(self._ready_metadata)
                        else:
                            loaded = json.load(stream)
                            payload = dict(loaded) if isinstance(loaded, dict) else None
                if self._file_key(self.ready_path.stat()) == key:
                    break
            except (OSError, ValueError, TypeError):
                self._clear_ready_metadata()
                return None
        else:
            self._clear_ready_metadata()
            return None
        if payload is None:
            self._clear_ready_metadata()
            return None
        if payload.get("schema") == "v5.ui_ready.v1":
            if payload.get("ready") is not True or not self._ready_identity_valid(payload):
                self._clear_ready_metadata()
                return None
            try:
                metadata_frame_id = int(payload.get("current_frame_id") or 0)
            except (TypeError, ValueError):
                self._clear_ready_metadata()
                return None
            with self.condition:
                current_frame_id = int(self.frame_id)
            if metadata_frame_id <= 0 or current_frame_id < metadata_frame_id:
                self._clear_ready_metadata()
                return None
        elif payload.get("schema") == "v5.ui_failure.v1":
            if (payload.get("ready") is not False or payload.get("failed") is not True or
                    not self._failure_identity_valid(payload)):
                self._clear_ready_metadata()
                return None
        else:
            self._clear_ready_metadata()
            return None
        with self._ready_lock:
            self._ready_metadata = dict(payload)
            self._ready_file_key = key
            return dict(self._ready_metadata)

    def lifecycle_snapshot(self) -> tuple[str, dict | None]:
        payload = self._lifecycle_metadata()
        if payload is None:
            return "booting", None
        if payload.get("schema") == "v5.ui_ready.v1":
            return "ready", payload
        return "failed", payload

    def ready_metadata(self) -> dict | None:
        status, payload = self.lifecycle_snapshot()
        return payload if status == "ready" else None

    def failure_metadata(self) -> dict | None:
        status, payload = self.lifecycle_snapshot()
        return payload if status == "failed" else None

    def ui_ready(self) -> bool:
        return self.ready_metadata() is not None

    def first_dirty_event(self) -> dict | None:
        with self.condition:
            return dict(self.first_event) if self.first_event is not None else None

    def recent_dirty_events(self, limit: int = 32) -> list[dict]:
        with self.condition:
            events = list(self.dirty_events)[-max(0, int(limit)):]
        return [dict(event) for event in events]

    @property
    def frame_size(self) -> int:
        return self.stride * self.height

    def close_framebuffer_map(self) -> None:
        self._dirty_packer.close()
        if self._framebuffer_mmap is not None:
            try:
                self._framebuffer_mmap.close()
            except BufferError:
                return
            except OSError:
                pass
        self._framebuffer_mmap = None
        if self._framebuffer_fd is not None:
            try:
                os.close(self._framebuffer_fd)
            except OSError:
                pass
        self._framebuffer_fd = None
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
        if self._framebuffer_mmap is not None and self._framebuffer_fd is not None and self._framebuffer_key == key:
            return self._framebuffer_mmap
        self.close_framebuffer_map()
        try:
            fd = os.open(
                self.framebuffer_path,
                os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_BINARY", 0))
        except OSError:
            return None
        try:
            mapped = mmap.mmap(fd, self.frame_size, access=mmap.ACCESS_READ)
        except OSError:
            os.close(fd)
            return None
        self._framebuffer_mmap = mapped
        self._framebuffer_fd = fd
        self._framebuffer_key = key
        self.mark_metric("framebuffer_mmap_refreshes")
        return mapped

    def full_frame(self) -> tuple[int, bytes] | None:
        mapped = self.framebuffer_map()
        fd = self._framebuffer_fd
        if mapped is None or fd is None:
            return None
        locked = False
        try:
            flock(fd, LOCK_SH)
            locked = True
            with self.condition:
                frame_id = max(1, self.frame_id)
            data = bytes(mapped[:self.frame_size])
        except OSError:
            return None
        finally:
            if locked:
                try:
                    flock(fd, LOCK_UN)
                except OSError:
                    pass
        if len(data) != self.frame_size:
            return None
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

    def normalized_dirty_rects(self, event: dict) -> list[dict] | None:
        prepared = self._dirty_geometry.prepare(event)
        return prepared[0] if prepared is not None else None

    def dirty_area_pixels(self, event: dict) -> int:
        prepared = self._dirty_geometry.prepare(event)
        return prepared[1] if prepared is not None else 0

    def is_large_dirty(self, event: dict) -> bool:
        frame_pixels = max(1, self.width * self.height)
        threshold = max(1, int(frame_pixels * LARGE_DIRTY_PIXEL_RATIO))
        return self.dirty_area_pixels(event) >= threshold

    def throttle_large_dirty(self, event: dict, last_sent_at: float) -> float:
        pixels = self.dirty_area_pixels(event)
        frame_pixels = max(1, self.width * self.height)
        threshold = max(1, int(frame_pixels * LARGE_DIRTY_PIXEL_RATIO))
        if pixels < threshold:
            return last_sent_at
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
        fd = self._framebuffer_fd
        if mapped is None or fd is None:
            return None
        total_bytes = sum(int(rect["w"]) * int(rect["h"]) * 4 for rect in rects)
        total_rows = sum(int(rect["h"]) for rect in rects)
        locked = False
        try:
            flock(fd, LOCK_SH)
            locked = True
            payload = self._dirty_packer.pack(
                fd, self.frame_size, self.width, self.height, self.stride, rects)
        except (OSError, RuntimeError, ValueError):
            self.mark_metric("dirty_payload_native_failures")
            return None
        finally:
            if locked:
                try:
                    flock(fd, LOCK_UN)
                except OSError:
                    pass
        if len(payload) != total_bytes:
            self.mark_metric("dirty_payload_native_failures")
            return None
        self.mark_metric("dirty_payload_native_frames")
        self.mark_metric("dirty_payload_bytes", total_bytes)
        self.mark_metric("dirty_payload_rows", total_rows)
        self.mark_metric("dirty_payload_rects", len(rects))
        return payload, rects

    def publish_dirty(self, event: dict) -> None:
        with self.condition:
            if int(event["frame_id"]) >= self.frame_id:
                self.frame_id = int(event["frame_id"])
            stored = dict(event)
            if self.first_event is None:
                self.first_event = dict(stored)
            self.latest_event = stored
            self.dirty_events.append(stored)
            self.condition.notify_all()

    def publish_display_cpu_metrics(self, metrics: dict) -> None:
        with self.condition:
            self._display_cpu_metrics = dict(metrics)

    def display_cpu_metrics(self) -> dict | None:
        with self.condition:
            return dict(self._display_cpu_metrics) if self._display_cpu_metrics is not None else None

    def wait_dirty_batch_after(
        self,
        frame_id: int,
        timeout: float,
        coalesce_seconds: float,
        coalesce_deadline: float | None = None,
    ) -> dict | None:
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                if any(int(event["frame_id"]) > frame_id for event in self.dirty_events):
                    break
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.condition.wait(min(remaining, 1.0))
            first_event_at = time.monotonic()
            settle_deadline = first_event_at + max(0.0, coalesce_seconds)
            if coalesce_deadline is not None:
                settle_deadline = min(settle_deadline, max(first_event_at, float(coalesce_deadline)))
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
        index = 0
        while index < len(events):
            event_frame = int(events[index]["frame_id"])
            group_end = index + 1
            while group_end < len(events) and int(events[group_end]["frame_id"]) == event_frame:
                group_end += 1
            if event_frame <= expected_base:
                index = group_end
                continue
            for event in events[index:group_end]:
                event_base = int(event["base_frame_id"])
                if event_base != expected_base:
                    return {
                        "stream_reset": True,
                        "frame_id": max(latest_frame, event_frame),
                        "base_frame_id": base_frame_id,
                        "reason": "missing_dirty_event",
                    }
                event_rects = self._dirty_geometry.source_rects(event)
                if not event_rects:
                    return {
                        "stream_reset": True,
                        "frame_id": max(latest_frame, event_frame),
                        "base_frame_id": base_frame_id,
                        "reason": "invalid_dirty_event",
                    }
                rects.extend(event_rects)
                merged += len(event_rects)
            latest_frame = event_frame
            expected_base = event_frame
            index = group_end
        if merged == 0:
            return None
        batch = {
            "frame_id": latest_frame,
            "base_frame_id": int(base_frame_id),
            "rects": rects,
            "merged_events": merged,
        }
        if self._dirty_geometry.prepare(batch) is None:
            return {
                "stream_reset": True,
                "frame_id": latest_frame,
                "base_frame_id": base_frame_id,
                "reason": "invalid_dirty_event",
            }
        return batch


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
        if len(parts) == 5 and parts[0] == "M":
            try:
                generation = int(parts[1])
                monotonic_ns = int(parts[2])
                cpu0_percent = float(parts[3])
                cpu1_percent = float(parts[4])
            except ValueError:
                return
            if (
                generation <= 0
                or monotonic_ns <= 0
                or not math.isfinite(cpu0_percent)
                or not math.isfinite(cpu1_percent)
                or not 0.0 <= cpu0_percent <= 100.0
                or not 0.0 <= cpu1_percent <= 100.0
            ):
                return
            self.state.publish_display_cpu_metrics({
                "cpu0_percent": cpu0_percent,
                "cpu1_percent": cpu1_percent,
                "cpu_sample_generation": generation,
                "cpu_sample_monotonic_ns": monotonic_ns,
            })
            return
        if len(parts) < 7:
            return
        try:
            frame_id, base_frame_id, rect_count = [int(part) for part in parts[:3]]
        except ValueError:
            return
        if (
            frame_id <= 0
            or base_frame_id < 0
            or rect_count <= 0
            or rect_count > MAX_DIRTY_RECTS_PER_FRAME
            or len(parts) != 3 + (rect_count * 4)
        ):
            return
        rects = []
        for index in range(rect_count):
            start = 3 + (index * 4)
            try:
                x, y, w, h = [int(part) for part in parts[start : start + 4]]
            except ValueError:
                return
            if w <= 0 or h <= 0 or x < 0 or y < 0 or x + w > self.state.width or y + h > self.state.height:
                return
            rects.append({"x": x, "y": y, "w": w, "h": h})
        event = {
            "frame_id": frame_id,
            "base_frame_id": base_frame_id,
            "rects": rects,
        }
        if rect_count == 1:
            event.update(rects[0])
        self.state.mark_metric("dirty_events", rect_count)
        self.state.publish_dirty(event)
