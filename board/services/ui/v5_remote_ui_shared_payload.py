from __future__ import annotations

import json
import os
import threading
import time
from collections import deque
from dataclasses import dataclass
from typing import TYPE_CHECKING

from v5_remote_ui_contract import DIRTY_EVENT_HISTORY_LIMIT, STREAM_COALESCE_SECONDS
from v5_remote_ui_protocol import frame_metadata

if TYPE_CHECKING:
    from v5_remote_ui_state import FrameState


FRAME_PRODUCER_NICE = 10


@dataclass(frozen=True)
class PreparedDirtyFrame:
    frame_id: int
    base_frame_id: int
    meta_bytes: bytes
    payload: bytes


@dataclass(frozen=True)
class PreparedFrameDelivery:
    frame: PreparedDirtyFrame | None = None
    needs_full: bool = False


class SharedDirtyPayloadProducer:
    """Coalesce and snapshot each dirty generation once for every stream client."""

    def __init__(
        self,
        state: FrameState,
        *,
        history_limit: int = DIRTY_EVENT_HISTORY_LIMIT,
        coalesce_seconds: float = STREAM_COALESCE_SECONDS,
        poll_seconds: float = 0.25,
    ):
        self.state = state
        self.condition = threading.Condition()
        self.history: deque[PreparedDirtyFrame] = deque(maxlen=max(1, int(history_limit)))
        self.coalesce_seconds = max(0.0, float(coalesce_seconds))
        self.poll_seconds = max(0.01, float(poll_seconds))
        self.stop_requested = threading.Event()
        self.thread = threading.Thread(target=self._run, name="v5-ui-frame-producer", daemon=True)
        self.subscribers = 0
        self.generation = 0
        self.cursor_frame_id = int(state.frame_id)
        self.latest_frame_id = self.cursor_frame_id
        self.last_large_dirty_sent_at = 0.0

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
                self.history.clear()
                self.cursor_frame_id = initial
                self.latest_frame_id = initial
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
                self.history.clear()
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
                    return PreparedFrameDelivery(needs_full=True)
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
            payload=bytes(payload),
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

    def _reset_after_gap(self, generation: int) -> None:
        with self.state.condition:
            current_frame_id = int(self.state.frame_id)
        with self.condition:
            if self.subscribers == 0 or self.generation != generation:
                return
            self.history.clear()
            self.cursor_frame_id = max(self.cursor_frame_id, current_frame_id)
            self.latest_frame_id = self.cursor_frame_id
            self.condition.notify_all()
        self.state.mark_metric("dirty_payload_shared_producer_repairs")

    def _run(self) -> None:
        if os.name == "posix":
            try:
                if os.nice(FRAME_PRODUCER_NICE) != FRAME_PRODUCER_NICE:
                    raise OSError("frame producer nice readback below policy")
            except OSError:
                self.state.mark_metric("dirty_payload_shared_producer_priority_errors")
                return
        while not self.stop_requested.is_set():
            active = self._wait_for_active_generation()
            if active is None:
                return
            generation, cursor_frame_id = active
            try:
                batch = self.state.wait_dirty_batch_after(
                    cursor_frame_id,
                    self.poll_seconds,
                    self.coalesce_seconds,
                )
                if batch is None:
                    continue
                with self.condition:
                    if not self._generation_is_current(generation, cursor_frame_id):
                        continue
                if batch.get("needs_full"):
                    self._reset_after_gap(generation)
                    continue
                self.last_large_dirty_sent_at = self.state.throttle_large_dirty(
                    batch,
                    self.last_large_dirty_sent_at,
                )
                prepared = self._build(batch)
                if prepared is None:
                    self._reset_after_gap(generation)
                    continue
                with self.condition:
                    if not self._generation_is_current(generation, cursor_frame_id):
                        continue
                    self.history.append(prepared)
                    self.cursor_frame_id = prepared.frame_id
                    self.latest_frame_id = prepared.frame_id
                    self.condition.notify_all()
                merged = int(batch.get("merged_events", 1))
                if merged > 1:
                    self.state.mark_metric("dirty_coalesced_events", merged - 1)
                self.state.mark_metric("dirty_rect_frames")
                self.state.mark_metric("dirty_payload_shared_builds")
            except Exception:
                self.state.mark_metric("dirty_payload_shared_producer_errors")
                self._reset_after_gap(generation)
                time.sleep(0.02)
