from __future__ import annotations

import json
import os
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from typing import TYPE_CHECKING

from v5_remote_ui_contract import (
    SHARED_DIRTY_FRAME_HISTORY_LIMIT,
    SHARED_DIRTY_PAYLOAD_HISTORY_BYTES,
    STREAM_COALESCE_SECONDS,
    FramePayload,
)
from v5_remote_ui_protocol import frame_metadata

if TYPE_CHECKING:
    from v5_remote_ui_state import FrameState


FRAME_PRODUCER_NICE = 10
PRIO_PROCESS = getattr(os, "PRIO_PROCESS", 0)


def ensure_frame_producer_priority(getpriority_fn=None, setpriority_fn=None) -> int:
    """Set the current producer thread to the absolute CPU1 niceness policy."""

    if getpriority_fn is None:
        getpriority_fn = os.getpriority
    if setpriority_fn is None:
        setpriority_fn = os.setpriority
    current = int(getpriority_fn(PRIO_PROCESS, 0))
    if current != FRAME_PRODUCER_NICE:
        setpriority_fn(PRIO_PROCESS, 0, FRAME_PRODUCER_NICE)
        current = int(getpriority_fn(PRIO_PROCESS, 0))
    if current != FRAME_PRODUCER_NICE:
        raise OSError(
            f"frame producer nice readback mismatch: {current} != {FRAME_PRODUCER_NICE}"
        )
    return current


def apply_frame_producer_priority(state, priority_fn=None) -> bool:
    if priority_fn is None:
        if os.name != "posix":
            return True
        priority_fn = ensure_frame_producer_priority
    try:
        priority_fn()
    except OSError:
        state.mark_metric("dirty_payload_shared_producer_priority_errors")
        return False
    return True


@dataclass(frozen=True)
class PreparedDirtyFrame:
    frame_id: int
    base_frame_id: int
    meta_bytes: bytes
    payload: FramePayload


@dataclass(frozen=True)
class PreparedFrameDelivery:
    frame: PreparedDirtyFrame | None = None
    restart_stream: bool = False
    reason: str = ""


class SharedDirtyPayloadProducer:
    """Coalesce and snapshot each dirty generation once for every stream client."""

    def __init__(
        self,
        state: FrameState,
        *,
        history_limit: int = SHARED_DIRTY_FRAME_HISTORY_LIMIT,
        history_byte_budget: int = SHARED_DIRTY_PAYLOAD_HISTORY_BYTES,
        coalesce_seconds: float = STREAM_COALESCE_SECONDS,
        poll_seconds: float = 0.25,
    ):
        self.state = state
        self.condition = threading.Condition()
        self.history: deque[PreparedDirtyFrame] = deque()
        self.history_limit = max(1, int(history_limit))
        self.history_byte_budget = max(1, int(history_byte_budget))
        self.history_payload_bytes = 0
        self.coalesce_seconds = max(0.0, float(coalesce_seconds))
        self.poll_seconds = max(0.01, float(poll_seconds))
        self.stop_requested = threading.Event()
        self.thread = threading.Thread(target=self._run, name="v5-ui-frame-producer", daemon=True)
        self.subscribers = 0
        self.generation = 0
        self.cursor_frame_id = int(state.frame_id)
        self.latest_frame_id = self.cursor_frame_id
        self.latest_reset_reason = ""
        self.last_large_dirty_sent_at = 0.0
        self.next_error_log_at = 0.0

    def _clear_history_locked(self) -> None:
        self.history.clear()
        self.history_payload_bytes = 0

    def _append_history_locked(self, prepared: PreparedDirtyFrame) -> None:
        self.history.append(prepared)
        self.history_payload_bytes += len(prepared.payload)
        while len(self.history) > self.history_limit or (
            self.history_payload_bytes > self.history_byte_budget and len(self.history) > 1
        ):
            evicted = self.history.popleft()
            self.history_payload_bytes -= len(evicted.payload)

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.stop_requested.set()
        with self.condition:
            self.condition.notify_all()
        with self.state.condition:
            self.state.condition.notify_all()
        if self.thread.is_alive():
            self.thread.join(1.0)

    def subscribe(self, initial_frame_id: int) -> None:
        initial = int(initial_frame_id)
        with self.condition:
            if self.subscribers == 0:
                self.generation += 1
                self._clear_history_locked()
                self.cursor_frame_id = initial
                self.latest_frame_id = initial
                self.latest_reset_reason = ""
                self.last_large_dirty_sent_at = 0.0
            self.subscribers += 1
            self.condition.notify_all()
        with self.state.condition:
            self.state.condition.notify_all()
        self.state.mark_metric("dirty_payload_shared_subscribers")

    def unsubscribe(self) -> None:
        with self.condition:
            if self.subscribers > 0:
                self.subscribers -= 1
            if self.subscribers == 0:
                self.generation += 1
                self._clear_history_locked()
            self.condition.notify_all()
        with self.state.condition:
            self.state.condition.notify_all()

    def wait_after(self, frame_id: int, timeout: float) -> PreparedFrameDelivery | None:
        requested = int(frame_id)
        deadline = time.monotonic() + max(0.0, float(timeout))
        with self.condition:
            while not self.stop_requested.is_set():
                for prepared in self.history:
                    if prepared.base_frame_id == requested:
                        self.state.mark_metric("dirty_payload_shared_history_hits")
                        return PreparedFrameDelivery(frame=prepared)
                if requested < self.latest_frame_id:
                    self.state.mark_metric("dirty_payload_shared_history_misses")
                    return PreparedFrameDelivery(
                        restart_stream=True,
                        reason=self.latest_reset_reason or "history_gap",
                    )
                remaining = deadline - time.monotonic()
                if remaining <= 0.0:
                    return None
                self.condition.wait(remaining)
        return None

    def _build(self, batch: dict) -> PreparedDirtyFrame | None:
        prepared = self.state.dirty_payload(batch)
        if prepared is None:
            return None
        payload, rects = prepared
        frame_id = int(batch["frame_id"])
        base_frame_id = int(batch["base_frame_id"])
        meta = frame_metadata(
            "dirty_rects",
            frame_id,
            base_frame_id,
            self.state.width,
            self.state.height,
            self.state.stride,
            tuple(dict(rect) for rect in rects),
        )
        return PreparedDirtyFrame(
            frame_id=frame_id,
            base_frame_id=base_frame_id,
            meta_bytes=json.dumps(meta, separators=(",", ":")).encode("utf-8"),
            payload=payload,
        )

    def _wait_for_active_generation(self) -> tuple[int, int] | None:
        with self.condition:
            while self.subscribers == 0 and not self.stop_requested.is_set():
                self.condition.wait()
            if self.stop_requested.is_set():
                return None
            return self.generation, self.cursor_frame_id

    def _generation_is_current(self, generation: int, cursor_frame_id: int) -> bool:
        return (
            not self.stop_requested.is_set()
            and self.subscribers > 0
            and self.generation == generation
            and self.cursor_frame_id == cursor_frame_id
        )

    def _reset_after_discontinuity(self, generation: int, reason: str) -> None:
        with self.state.condition:
            current_frame_id = int(self.state.frame_id)
        with self.condition:
            if self.subscribers == 0 or self.generation != generation:
                return
            self._clear_history_locked()
            self.cursor_frame_id = max(self.cursor_frame_id, current_frame_id)
            self.latest_frame_id = self.cursor_frame_id
            self.latest_reset_reason = str(reason or "dirty_discontinuity")
            self.condition.notify_all()
        self.state.mark_metric("dirty_payload_shared_producer_resets")

    def _run(self) -> None:
        apply_frame_producer_priority(self.state)
        cadence_generation = -1
        next_emit_at = 0.0
        while not self.stop_requested.is_set():
            active = self._wait_for_active_generation()
            if active is None:
                return
            generation, cursor_frame_id = active
            if cadence_generation != generation:
                cadence_generation = generation
                next_emit_at = time.monotonic() + self.coalesce_seconds
            try:
                batch = self.state.wait_dirty_batch_after(
                    cursor_frame_id,
                    self.poll_seconds,
                    self.coalesce_seconds,
                    coalesce_deadline=next_emit_at,
                )
                if batch is None:
                    continue
                with self.condition:
                    if not self._generation_is_current(generation, cursor_frame_id):
                        continue
                if batch.get("stream_reset"):
                    self._reset_after_discontinuity(
                        generation,
                        str(batch.get("reason") or "dirty_discontinuity"),
                    )
                    continue
                self.last_large_dirty_sent_at = self.state.throttle_large_dirty(
                    batch,
                    self.last_large_dirty_sent_at,
                )
                prepared = self._build(batch)
                if prepared is None:
                    self._reset_after_discontinuity(generation, "payload_unavailable")
                    continue
                with self.condition:
                    if not self._generation_is_current(generation, cursor_frame_id):
                        continue
                    self._append_history_locked(prepared)
                    self.cursor_frame_id = prepared.frame_id
                    self.latest_frame_id = prepared.frame_id
                    self.latest_reset_reason = ""
                    self.condition.notify_all()
                merged = int(batch.get("merged_events", 1))
                if merged > 1:
                    self.state.mark_metric("dirty_coalesced_events", merged - 1)
                self.state.mark_metric("dirty_rect_frames")
                self.state.mark_metric("dirty_payload_shared_builds")
                next_emit_at += self.coalesce_seconds
                now = time.monotonic()
                if next_emit_at <= now:
                    # The build itself may cross the scheduled cadence boundary.
                    # In that case the next already-pending dirty generation is
                    # immediately eligible; adding a fresh 33 ms here turns the
                    # target period into build_time + 33 ms and halves the stream.
                    next_emit_at = now
            except Exception as exc:
                self.state.mark_metric("dirty_payload_shared_producer_errors")
                error_now = time.monotonic()
                if error_now >= self.next_error_log_at:
                    print(
                        "v5_remote_ui_shared_payload producer error "
                        f"type={type(exc).__name__} message={exc}",
                        file=sys.stderr,
                        flush=True,
                    )
                    self.next_error_log_at = error_now + 5.0
                self._reset_after_discontinuity(generation, "producer_exception")
                time.sleep(0.02)
