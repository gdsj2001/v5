#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import sys
import tempfile
import threading
import time
from pathlib import Path


def write_framebuffer(path: Path, width: int, height: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = bytearray()
    for y in range(height):
        for x in range(width):
            payload.extend(bytes((x, y, 128, 255)))
    path.write_bytes(bytes(payload))


def payload_bytes(payload) -> bytes:
    parts = getattr(payload, "parts", None)
    if parts is not None:
        return b"".join(bytes(part) for part in parts)
    return bytes(payload)


def check_cpu_usage_sampler() -> int:
    samples = {
        "cpu0": iter([(100, 50), (200, 70), (260, 100)]),
        "cpu1": iter([(100, 90), (120, 100)]),
    }

    def sample(name: str):
        return next(samples[name])

    sampler = relay.CpuUsageSampler(sample)
    checks = [
        ("first cpu0 sample has no delta", sampler.percent("cpu0"), None),
        ("cpu0 first delta", sampler.percent("cpu0"), 80.0),
        ("cpu0 second delta", sampler.percent("cpu0"), 50.0),
        ("first cpu1 sample has no delta", sampler.percent("cpu1"), None),
        ("cpu1 first delta", sampler.percent("cpu1"), 50.0),
    ]
    for label, actual, expected in checks:
        if actual != expected:
            print("bad cpu sampler", label, actual, expected)
            return 10
    return 0


def check_refresh_commit_boundary() -> int:
    source_path = Path(__file__).resolve().parents[2] / "app" / "src" / "v5_lvgl_remote_display.c"
    source = source_path.read_text(encoding="utf-8")
    toolpath_source = source_path.with_name("v5_main_page_toolpath_primitives.c").read_text(encoding="utf-8")
    toolpath_geometry_source = source_path.with_name("v5_main_page_toolpath_geometry.c").read_text(encoding="utf-8")
    relay_source = Path(__file__).with_name("v5_remote_ui_relay.py").read_text(encoding="utf-8")
    state_source = Path(__file__).with_name("v5_remote_ui_state.py").read_text(encoding="utf-8")
    mirror_source = source_path.parents[2].joinpath("tools", "deploy", "v5_ai_browser_mirror.py").read_text(encoding="utf-8")
    compose_start = source.index("static void compose_area")
    commit_start = source.index("static void commit_composed_frame")
    flush_start = source.index("static void remote_flush")
    setup_start = source.index("int v5_lvgl_remote_display_setup")
    compose_body = source[compose_start:commit_start]
    commit_body = source[commit_start:flush_start]
    flush_body = source[flush_start:setup_start]
    if "notify_remote_dirty" in compose_body or "g_remote_fb" in compose_body:
        print("pre-final flush still publishes output")
        return 19
    required_flush = ["compose_area(area, color_p);", "lv_disp_flush_is_last(driver)", "commit_composed_frame();"]
    if any(token not in flush_body for token in required_flush):
        print("last-flush commit boundary missing")
        return 20
    if not (flush_body.index(required_flush[0]) < flush_body.index(required_flush[1]) < flush_body.index(required_flush[2])):
        print("last-flush commit boundary is out of order")
        return 21
    if commit_body.count("++g_frame_id;") != 1 or commit_body.count("notify_remote_dirty_rects(") != 1:
        print("composed refresh does not publish exactly one frame with its dirty rect list")
        return 22
    required_dirty_list = [
        "V5_REMOTE_DIRTY_RECT_CAPACITY",
        "g_pending_dirty_rects",
        "add_pending_dirty_area(x1, y1, x2, y2);",
    ]
    if any(token not in source for token in required_dirty_list):
        print("bounded dirty rect list is missing")
        return 23
    retired_union_tokens = ["accumulate_dirty_area", "g_pending_dirty_x1", "g_pending_dirty_x2"]
    if any(token in source for token in retired_union_tokens):
        print("retired single bounding-rect accumulator is still present")
        return 24
    heartbeat_tokens = [
        'if opcode == 0x9:',
        'return "pong"',
        'if opcode == 0xA:',
        'send_ws_frame(sock, 0xA, payload)',
    ]
    if any(token not in relay_source for token in heartbeat_tokens):
        print("remote input websocket heartbeat handling is incomplete")
        return 26
    axis_line_start = toolpath_source.index("void v5_main_page_internal_set_toolpath_axis_line")
    axis_line_end = toolpath_source.index("int v5_main_page_internal_main_page_tool_length_mm")
    axis_line_body = toolpath_source[axis_line_start:axis_line_end]
    precise_line_tokens = [
        "static void invalidate_toolpath_line_points",
        "invalidate_toolpath_line_points(line, points, 2U);",
        "lv_obj_invalidate_area(line, &dirty);",
        "coalesce_toolpath_display_invalidations",
    ]
    if any(token not in toolpath_source for token in precise_line_tokens):
        print("dynamic toolpath line precise invalidation is missing")
        return 28
    if axis_line_body.count("invalidate_toolpath_line_points(line, points, 2U);") != 2:
        print("dynamic toolpath line does not invalidate both old and new segment bounds")
        return 29
    if axis_line_body.count("lv_line_set_points(line, points, 2);") != 1:
        print("visible dynamic toolpath line still uses whole-object invalidation")
        return 30
    hide_unproven_start = toolpath_geometry_source.index("void v5_main_page_internal_hide_toolpath_unproven_geometry")
    hide_unproven_end = toolpath_geometry_source.index("void v5_main_page_internal_update_toolpath_state_lines", hide_unproven_start)
    if "v5_main_page_internal_hide_toolpath_line(page->toolpath_holder_line);" not in toolpath_geometry_source[hide_unproven_start:hide_unproven_end]:
        print("unproven toolpath geometry no longer hides the holder line")
        return 38
    full_start = state_source.index("    def full_frame(")
    full_end = state_source.index("    def mark_metric(", full_start)
    full_body = state_source[full_start:full_end]
    required_full_order = ["flock(fd, LOCK_SH)", "frame_id = max(1, self.frame_id)", "data = bytes(mapped[:self.frame_size])", "flock(fd, LOCK_UN)"]
    if any(token not in full_body for token in required_full_order) or not all(
        full_body.index(required_full_order[index]) < full_body.index(required_full_order[index + 1])
        for index in range(len(required_full_order) - 1)
    ):
        print("full frame pixels and frame id are not captured under one shared lock")
        return 31
    dirty_start = state_source.index("    def dirty_payload(")
    dirty_end = state_source.index("    def publish_dirty(", dirty_start)
    dirty_body = state_source[dirty_start:dirty_end]
    required_dirty_snapshot = [
        "payload = bytearray(total_bytes)",
        "payload_view = memoryview(payload)",
        "mapped_view = memoryview(mapped)",
        "flock(fd, LOCK_SH)",
        "payload_view[captured_bytes:captured_bytes + length] = mapped_view[start:end]",
        "payload_view[captured_bytes:captured_bytes + row_bytes] = mapped_view[src_start:src_end]",
        "return bytes(payload), rects",
    ]
    retired_dirty_copy = [
        "payload_parts: list[memoryview] = []",
        "return PayloadViews(payload_parts, total_bytes), rects",
        ".toreadonly()",
    ]
    if any(token not in dirty_body for token in required_dirty_snapshot) or any(
        token in dirty_body for token in retired_dirty_copy
    ):
        print("dirty payload is not an owned framebuffer snapshot")
        return 32
    mirror_tokens = [
        "let streamGeneration = 0;",
        "let baseReady = false;",
        "let pointerForwarded = false;",
        "const nextRgba = new Uint8ClampedArray",
        "if (generation !== streamGeneration) return;",
        "scheduleStreamReconnect('base-repair');",
        "globalThis.v5AiClick = v5AiClick;",
        "if (!inputReady || !streamLive || !baseReady) return {ok: false, reason: 'not_ready'};",
        "if (pointerForwarded) pointerEvent('up', event, false);",
        "const screenTarget = document.getElementById('screen-wrap');",
        "const canvas = document.getElementById('screen');",
        "canvas.getContext('bitmaprenderer');",
        "new OffscreenCanvas(1024, 600)",
        "stagingCanvas.transferToImageBitmap();",
        "bitmapCtx.transferFromImageBitmap(bitmap);",
        "async function checkStreamProgress()",
        "scheduleStreamReconnect('stale-stream');",
        "setInterval(checkStreamProgress, 1000);",
    ]
    retired_mirror_tokens = [
        "repairFull(",
        "rectRgba",
        "screen-back",
        "frontCanvas",
        "backCanvas",
        "screenTarget.appendChild",
        "ctx.drawImage",
        "ctx.putImageData",
    ]
    if any(token not in mirror_source for token in mirror_tokens) or any(
        token in mirror_source for token in retired_mirror_tokens
    ):
        print("AI browser mirror still has competing full-frame bases")
        return 33
    if '"frame_id": self.state.frame_id' not in relay_source:
        print("remote info does not expose the upstream frame id for stale-stream recovery")
        return 54
    click_start = mirror_source.index("  async function v5AiClick(")
    click_end = mirror_source.index("  globalThis.v5AiClick = v5AiClick;", click_start)
    click_body = mirror_source[click_start:click_end]
    required_click_order = [
        "sendPointer('down', point.x, point.y, true)",
        "await new Promise(resolve => setTimeout",
        "sendPointer('up', point.x, point.y, false)",
    ]
    if any(token not in click_body for token in required_click_order) or not all(
        click_body.index(required_click_order[index]) < click_body.index(required_click_order[index + 1])
        for index in range(len(required_click_order) - 1)
    ):
        print("AI browser click does not preserve down, hold, up ordering")
        return 53
    present_start = mirror_source.index("  function presentRgba(")
    present_end = mirror_source.index("  function applyFull(", present_start)
    present_body = mirror_source[present_start:present_end]
    required_atomic_order = [
        "stagingCtx.putImageData(new ImageData(rgba, width, height), 0, 0);",
        "const bitmap = stagingCanvas.transferToImageBitmap();",
        "bitmapCtx.transferFromImageBitmap(bitmap);",
    ]
    if any(token not in present_body for token in required_atomic_order) or not all(
        present_body.index(required_atomic_order[index]) < present_body.index(required_atomic_order[index + 1])
        for index in range(len(required_atomic_order) - 1)
    ):
        print("AI browser mirror does not finish the offscreen bitmap before the atomic visible commit")
        return 51
    full_apply_start = mirror_source.index("  function applyFull(")
    full_apply_end = mirror_source.index("  function applyDirty(", full_apply_start)
    full_apply_body = mirror_source[full_apply_start:full_apply_end]
    if full_apply_body.index("const nextRgba") > full_apply_body.index("presentRgba(width, height)"):
        print("AI browser mirror clears the canvas before validating the replacement frame")
        return 34
    return 0


def check_full_frame_waits_for_commit(root: Path) -> int:
    import fcntl

    state = relay.FrameState(root / "flock", 4, 4)
    write_framebuffer(state.framebuffer_path, state.width, state.height)
    writer_fd = os.open(state.framebuffer_path, os.O_RDWR)
    started = threading.Event()
    finished = threading.Event()
    result: list[tuple[int, bytes] | None] = []

    def read_frame() -> None:
        started.set()
        result.append(state.full_frame())
        finished.set()

    committed = bytes((7, 11, 13, 255)) * (state.width * state.height)
    thread = threading.Thread(target=read_frame, daemon=True)
    try:
        fcntl.flock(writer_fd, fcntl.LOCK_EX)
        thread.start()
        if not started.wait(1.0):
            print("full frame reader did not start")
            return 16
        midpoint = len(committed) // 2
        os.pwrite(writer_fd, committed[:midpoint], 0)
        time.sleep(0.05)
        if finished.is_set():
            print("full frame reader observed a partial writer commit")
            return 17
        os.pwrite(writer_fd, committed[midpoint:], midpoint)
        fcntl.flock(writer_fd, fcntl.LOCK_UN)
        thread.join(1.0)
    finally:
        try:
            fcntl.flock(writer_fd, fcntl.LOCK_UN)
        except OSError:
            pass
        os.close(writer_fd)
    if thread.is_alive() or not result or result[0] is None or result[0][1] != committed:
        print("full frame reader did not return the committed frame")
        return 18
    state.close_framebuffer_map()
    return 0


def check_dirty_payload_is_owned_snapshot(root: Path) -> int:
    state = relay.FrameState(root / "dirty_snapshot", 4, 4)
    write_framebuffer(state.framebuffer_path, state.width, state.height)
    event = {"frame_id": 2, "base_frame_id": 1, "x": 0, "y": 0, "w": 4, "h": 4}
    prepared = state.dirty_payload(event)
    if prepared is None:
        print("dirty payload snapshot missing")
        return 35
    payload, _ = prepared
    if (
        not isinstance(payload, bytes)
        or len(payload) != state.frame_size
    ):
        print("dirty payload is not one immutable owned snapshot")
        return 53
    captured = payload_bytes(payload)
    replacement = bytes((31, 37, 41, 255)) * (state.width * state.height)
    writer_fd = os.open(state.framebuffer_path, os.O_RDWR)
    try:
        os.pwrite(writer_fd, replacement, 0)
    finally:
        os.close(writer_fd)
    if payload_bytes(payload) != captured or captured == replacement:
        print("dirty payload changed after its framebuffer lock was released")
        return 36
    state.close_framebuffer_map()
    return 0


class ProducerTestState:
    def __init__(self, build_delay_seconds: float = 0.0):
        self.width = 4
        self.height = 4
        self.stride = self.width * 4
        self.frame_id = 1
        self.condition = threading.Condition()
        self.events: list[dict] = []
        self.metrics: dict[str, int] = {}
        self.build_count = 0
        self.build_times: list[float] = []
        self.build_delay_seconds = max(0.0, float(build_delay_seconds))
        self.last_payload = None

    def mark_metric(self, name: str, delta: int = 1) -> None:
        self.metrics[name] = self.metrics.get(name, 0) + int(delta)

    def metrics_snapshot(self) -> dict:
        return dict(self.metrics)

    def publish_dirty(self, event: dict) -> None:
        with self.condition:
            self.events.append(dict(event))
            self.frame_id = max(self.frame_id, int(event["frame_id"]))
            self.condition.notify_all()

    def wait_dirty_batch_after(
        self,
        frame_id: int,
        timeout: float,
        coalesce_seconds: float,
        coalesce_deadline: float | None = None,
    ) -> dict | None:
        deadline = time.monotonic() + timeout
        with self.condition:
            while not any(int(event["frame_id"]) > frame_id for event in self.events):
                remaining = deadline - time.monotonic()
                if remaining <= 0.0:
                    return None
                self.condition.wait(remaining)
            first_event_at = time.monotonic()
            settle_deadline = first_event_at + max(0.0, coalesce_seconds)
            if coalesce_deadline is not None:
                settle_deadline = min(settle_deadline, max(first_event_at, float(coalesce_deadline)))
            remaining = settle_deadline - time.monotonic()
            if remaining > 0.0:
                self.condition.wait(remaining)
            events = [dict(event) for event in self.events if int(event["frame_id"]) > frame_id]
        events.sort(key=lambda event: int(event["frame_id"]))
        expected_base = int(frame_id)
        rects = []
        for event in events:
            if int(event["base_frame_id"]) != expected_base:
                return {
                    "stream_reset": True,
                    "frame_id": int(event["frame_id"]),
                    "reason": "missing_dirty_event",
                }
            rects.extend(event.get("rects") or [event])
            expected_base = int(event["frame_id"])
        return {
            "frame_id": expected_base,
            "base_frame_id": int(frame_id),
            "rects": rects,
            "merged_events": len(events),
        }

    def throttle_large_dirty(self, batch: dict, last_sent_at: float) -> float:
        return last_sent_at

    def dirty_payload(self, batch: dict):
        from v5_remote_ui_contract import PayloadViews

        if self.build_delay_seconds > 0.0:
            time.sleep(self.build_delay_seconds)
        self.build_count += 1
        self.build_times.append(time.monotonic())
        rects = [
            {
                "x": int(rect["x"]),
                "y": int(rect["y"]),
                "w": int(rect["w"]),
                "h": int(rect["h"]),
                "codec": "raw",
            }
            for rect in batch["rects"]
        ]
        payload_size = sum(rect["w"] * rect["h"] * 4 for rect in rects)
        payload = bytes([int(batch["frame_id"]) & 0xFF]) * payload_size
        self.last_payload = PayloadViews([memoryview(payload)], payload_size)
        return self.last_payload, rects


def check_global_producer_source_contract() -> int:
    relay_source = Path(__file__).with_name("v5_remote_ui_relay.py").read_text(encoding="utf-8")
    producer_source = Path(__file__).with_name("v5_remote_ui_shared_payload.py").read_text(encoding="utf-8")
    required = [
        "SharedDirtyPayloadProducer",
        "FRAME_PRODUCER_NICE = 10",
        "apply_frame_producer_priority(self.state)",
        "os.getpriority",
        "os.setpriority",
        "dirty_payload_shared_producer_priority_errors",
        "self.server.payload_producer.subscribe(frame_id)",
        "self.server.payload_producer.wait_after(last_sent",
        "self.server.payload_producer.unsubscribe()",
        "SHARED_DIRTY_FRAME_HISTORY_LIMIT",
        "SHARED_DIRTY_PAYLOAD_HISTORY_BYTES",
        "self.history_limit = max(1, int(history_limit))",
        "self.history_payload_bytes > self.history_byte_budget",
        "coalesce_deadline=next_emit_at",
        "next_emit_at += self.coalesce_seconds",
        "next_emit_at = now",
        "restart_stream=True",
        "if delivery.restart_stream:",
        "stream_runtime_resets",
    ]
    if any(token not in relay_source + producer_source for token in required):
        print("global dirty payload producer contract is incomplete")
        return 42
    unsupported_thread_id_tokens = ("threading.get_native_id", "/proc/self/task/")
    if any(token in producer_source for token in unsupported_thread_id_tokens):
        print("global producer still depends on a Python 3.8 thread-id API")
        return 52
    retired = [
        "SharedDirtyPayloadCache",
        "payload_cache",
        "self.state.wait_dirty_batch_after(last_sent",
        "payload=bytes(payload)",
        "DIRTY_EVENT_HISTORY_LIMIT, STREAM_COALESCE_SECONDS",
        "next_emit_at = now + self.coalesce_seconds",
        "PreparedFrameDelivery(" + "needs_" + "full=True)",
        "if delivery." + "needs_" + "full:",
    ]
    if any(token in relay_source + producer_source for token in retired):
        print("retired per-client dirty payload path is still present")
        return 43
    handle_stream = relay_source[
        relay_source.index("    def handle_stream(self) -> None:"):
        relay_source.index("    def handle_input(self) -> None:")
    ]
    if handle_stream.count("self.state.full_frame()") != 1 or "if delivery.restart_stream:\n" not in handle_stream:
        print("runtime stream discontinuity can still fall back to a full frame")
        return 62
    return 0


def check_shared_payload_producer_priority_is_absolute() -> int:
    import v5_remote_ui_shared_payload as shared

    for initial in (0, shared.FRAME_PRODUCER_NICE, 15):
        current = [initial]
        set_calls: list[tuple[int, int, int]] = []

        def fake_getpriority(which: int, who: int) -> int:
            if which != shared.PRIO_PROCESS or who != 0:
                raise AssertionError((which, who))
            return current[0]

        def fake_setpriority(which: int, who: int, value: int) -> None:
            set_calls.append((which, who, value))
            current[0] = value

        result = shared.ensure_frame_producer_priority(fake_getpriority, fake_setpriority)
        expected_calls = [] if initial == shared.FRAME_PRODUCER_NICE else [
            (shared.PRIO_PROCESS, 0, shared.FRAME_PRODUCER_NICE)
        ]
        if result != shared.FRAME_PRODUCER_NICE or set_calls != expected_calls:
            print("producer priority was not applied absolutely", initial, result, set_calls)
            return 53

    def ignored_setpriority(which: int, who: int, value: int) -> None:
        return None

    try:
        shared.ensure_frame_producer_priority(
            lambda which, who: 0,
            ignored_setpriority,
        )
    except OSError:
        pass
    else:
        print("producer priority mismatch did not fail closed")
        return 54

    state = ProducerTestState()

    def failed_priority() -> None:
        raise OSError("simulated priority failure")

    if shared.apply_frame_producer_priority(state, failed_priority):
        print("producer priority failure was not reported")
        return 55
    if state.metrics.get("dirty_payload_shared_producer_priority_errors") != 1:
        print("producer priority failure metric missing", state.metrics)
        return 56
    producer = shared.SharedDirtyPayloadProducer(
        state,
        history_limit=2,
        coalesce_seconds=0.001,
        poll_seconds=0.01,
    )
    producer.start()
    producer.subscribe(1)
    try:
        state.publish_dirty({"frame_id": 2, "base_frame_id": 1, "x": 0, "y": 0, "w": 1, "h": 1})
        delivery = producer.wait_after(1, 1.0)
        if delivery is None or delivery.frame is None:
            print("producer stopped after a non-safety priority failure")
            return 57
    finally:
        producer.unsubscribe()
        producer.stop()
    return 0


def check_skewed_stream_clients_share_prepared_history() -> int:
    import v5_remote_ui_shared_payload as shared

    state = ProducerTestState()
    producer = shared.SharedDirtyPayloadProducer(state, history_limit=4, coalesce_seconds=0.001, poll_seconds=0.01)
    producer.start()
    producer.subscribe(1)
    producer.subscribe(1)
    try:
        state.publish_dirty({"frame_id": 2, "base_frame_id": 1, "x": 0, "y": 0, "w": 2, "h": 2})
        first = producer.wait_after(1, 1.0)
        time.sleep(0.03)
        second = producer.wait_after(1, 1.0)
        if first is None or second is None or first.frame is None or first.frame is not second.frame:
            print("skewed clients did not share one immutable prepared frame", first, second)
            return 44
        parts = getattr(first.frame.payload, "parts", ())
        if (
            state.build_count != 1
            or not isinstance(first.frame.meta_bytes, bytes)
            or first.frame.payload is not state.last_payload
            or not isinstance(parts, tuple)
            or any(not part.readonly for part in parts)
        ):
            print("global producer rebuilt or exposed mutable payload", state.build_count, first)
            return 45
    finally:
        producer.unsubscribe()
        producer.unsubscribe()
        producer.stop()
    return 0


def check_slow_client_does_not_block_global_producer() -> int:
    import v5_remote_ui_shared_payload as shared

    state = ProducerTestState()
    producer = shared.SharedDirtyPayloadProducer(state, history_limit=4, coalesce_seconds=0.001, poll_seconds=0.01)
    producer.start()
    producer.subscribe(1)
    producer.subscribe(1)
    slow_holds_frame = threading.Event()
    release_slow = threading.Event()

    def slow_send() -> None:
        delivery = producer.wait_after(1, 1.0)
        if delivery is not None and delivery.frame is not None:
            slow_holds_frame.set()
            release_slow.wait(1.0)

    slow = threading.Thread(target=slow_send, daemon=True)
    try:
        slow.start()
        state.publish_dirty({"frame_id": 2, "base_frame_id": 1, "x": 0, "y": 0, "w": 1, "h": 1})
        if not slow_holds_frame.wait(1.0):
            print("slow client did not receive the first prepared frame")
            return 46
        fast_first = producer.wait_after(1, 1.0)
        state.publish_dirty({"frame_id": 3, "base_frame_id": 2, "x": 2, "y": 2, "w": 1, "h": 1})
        fast_second = producer.wait_after(2, 1.0)
        if fast_first is None or fast_first.frame is None or fast_second is None or fast_second.frame is None:
            print("slow client blocked global producer progress", fast_first, fast_second)
            return 47
        if slow_holds_frame.is_set() and not release_slow.is_set() and state.build_count != 2:
            print("producer did not build the next generation while one client was slow", state.build_count)
            return 48
    finally:
        release_slow.set()
        slow.join(1.0)
        producer.unsubscribe()
        producer.unsubscribe()
        producer.stop()
    return 0


def check_continuous_30hz_input_keeps_30hz_cadence() -> int:
    import v5_remote_ui_shared_payload as shared
    from v5_remote_ui_contract import STREAM_COALESCE_SECONDS

    state = ProducerTestState()
    producer = shared.SharedDirtyPayloadProducer(
        state,
        history_limit=64,
        history_byte_budget=1024 * 1024,
        coalesce_seconds=STREAM_COALESCE_SECONDS,
        poll_seconds=0.05,
    )
    producer.start()
    producer.subscribe(1)
    started = time.monotonic()
    expected_frames = 40
    try:
        for frame_id in range(2, 2 + expected_frames):
            target = started + 0.005 + ((frame_id - 2) * STREAM_COALESCE_SECONDS)
            remaining = target - time.monotonic()
            if remaining > 0.0:
                time.sleep(remaining)
            state.publish_dirty(
                {"frame_id": frame_id, "base_frame_id": frame_id - 1, "x": 0, "y": 0, "w": 1, "h": 1}
            )
        deadline = time.monotonic() + 0.25
        while state.build_count < expected_frames and time.monotonic() < deadline:
            time.sleep(0.005)
        steady_build_times = state.build_times[1:]
        if len(steady_build_times) < 29:
            print("continuous 30 Hz dirty input produced too few steady samples", state.build_count)
            return 58
        elapsed = max(0.001, steady_build_times[-1] - steady_build_times[0])
        cadence_hz = (len(steady_build_times) - 1) / elapsed
        if cadence_hz < 29.0:
            print("continuous 30 Hz dirty input was merged down below cadence", state.build_count, cadence_hz)
            return 58
    finally:
        producer.unsubscribe()
        producer.stop()
    return 0


def check_late_build_does_not_add_another_cadence_wait() -> int:
    import v5_remote_ui_shared_payload as shared

    cadence = 0.02
    build_delay = 0.03
    state = ProducerTestState(build_delay_seconds=build_delay)
    producer = shared.SharedDirtyPayloadProducer(
        state,
        history_limit=64,
        history_byte_budget=1024 * 1024,
        coalesce_seconds=cadence,
        poll_seconds=0.05,
    )
    producer.start()
    producer.subscribe(1)
    started = time.monotonic()
    try:
        for frame_id in range(2, 26):
            target = started + 0.002 + ((frame_id - 2) * cadence)
            remaining = target - time.monotonic()
            if remaining > 0.0:
                time.sleep(remaining)
            state.publish_dirty(
                {"frame_id": frame_id, "base_frame_id": frame_id - 1, "x": 0, "y": 0, "w": 1, "h": 1}
            )
        deadline = time.monotonic() + 0.35
        while state.build_count < 12 and time.monotonic() < deadline:
            time.sleep(0.005)
        if state.build_count < 10:
            print("late producer build still paid a second full cadence wait", state.build_count)
            return 59
        if len(state.build_times) >= 4:
            elapsed = state.build_times[-1] - state.build_times[1]
            average_interval = elapsed / max(1, len(state.build_times) - 2)
            if average_interval >= build_delay + (cadence * 0.75):
                print("late producer cadence includes an artificial post-build wait", average_interval)
                return 60
    finally:
        producer.unsubscribe()
        producer.stop()
    return 0


def check_missing_history_restarts_stream() -> int:
    import v5_remote_ui_shared_payload as shared

    state = ProducerTestState()
    producer = shared.SharedDirtyPayloadProducer(state, history_limit=1, coalesce_seconds=0.001, poll_seconds=0.01)
    producer.start()
    producer.subscribe(1)
    try:
        state.publish_dirty({"frame_id": 2, "base_frame_id": 1, "x": 0, "y": 0, "w": 1, "h": 1})
        first = producer.wait_after(1, 1.0)
        state.publish_dirty({"frame_id": 3, "base_frame_id": 2, "x": 1, "y": 1, "w": 1, "h": 1})
        second = producer.wait_after(2, 1.0)
        missing = producer.wait_after(1, 0.05)
        if first is None or first.frame is None or second is None or second.frame is None:
            print("bounded producer history did not advance", first, second)
            return 49
        if (
            missing is None
            or not missing.restart_stream
            or missing.reason != "history_gap"
            or missing.frame is not None
        ):
            print("missing producer history did not restart the stream", missing)
            return 50
    finally:
        producer.unsubscribe()
        producer.stop()
    return 0


def check_frame_and_byte_budgets_restart_streams() -> int:
    import v5_remote_ui_shared_payload as shared
    from v5_remote_ui_contract import STREAM_COALESCE_SECONDS, STREAM_TARGET_FPS

    if abs(STREAM_COALESCE_SECONDS - (1.0 / STREAM_TARGET_FPS)) > 1e-12 or STREAM_TARGET_FPS != 30:
        print("shared payload history changed the 30 Hz stream cadence")
        return 54

    def advance(producer, state, start: int, count: int) -> None:
        for frame_id in range(start + 1, start + count + 1):
            state.publish_dirty(
                {"frame_id": frame_id, "base_frame_id": frame_id - 1, "x": 0, "y": 0, "w": 1, "h": 1}
            )
            delivery = producer.wait_after(frame_id - 1, 1.0)
            if delivery is None or delivery.frame is None:
                raise RuntimeError(f"producer did not advance to frame {frame_id}")

    frame_state = ProducerTestState()
    frame_producer = shared.SharedDirtyPayloadProducer(
        frame_state,
        history_limit=2,
        history_byte_budget=1024,
        coalesce_seconds=0.001,
        poll_seconds=0.01,
    )
    frame_producer.start()
    frame_producer.subscribe(1)
    try:
        advance(frame_producer, frame_state, 1, 3)
        frame_gap = frame_producer.wait_after(1, 0.05)
        if len(frame_producer.history) != 2 or frame_gap is None or not frame_gap.restart_stream:
            print("frame-bounded history did not restart the slow client stream")
            return 55
    finally:
        frame_producer.unsubscribe()
        frame_producer.stop()

    byte_state = ProducerTestState()
    byte_producer = shared.SharedDirtyPayloadProducer(
        byte_state,
        history_limit=8,
        history_byte_budget=8,
        coalesce_seconds=0.001,
        poll_seconds=0.01,
    )
    byte_producer.start()
    byte_producer.subscribe(1)
    try:
        advance(byte_producer, byte_state, 1, 3)
        repaired = byte_producer.wait_after(1, 0.05)
        if (
            len(byte_producer.history) != 2
            or byte_producer.history_payload_bytes != 8
            or repaired is None
            or not repaired.restart_stream
        ):
            print("byte-bounded history did not restart the slow client stream")
            return 56
    finally:
        byte_producer.unsubscribe()
        byte_producer.stop()
    return 0


def check_shared_payload_producer() -> int:
    for check in (
        check_global_producer_source_contract,
        check_shared_payload_producer_priority_is_absolute,
        check_skewed_stream_clients_share_prepared_history,
        check_slow_client_does_not_block_global_producer,
        check_continuous_30hz_input_keeps_30hz_cadence,
        check_late_build_does_not_add_another_cadence_wait,
        check_missing_history_restarts_stream,
        check_frame_and_byte_budgets_restart_streams,
    ):
        rc = check()
        if rc != 0:
            return rc
    return 0


def main() -> int:
    global relay
    import v5_remote_ui_relay as relay

    expected_input_actions = {
        0x1: "message",
        0x8: "close",
        0x9: "pong",
        0xA: "continue",
        0x2: "close",
    }
    if {opcode: relay.input_ws_frame_action(opcode) for opcode in expected_input_actions} != expected_input_actions:
        print("bad remote input websocket frame actions")
        return 27

    rc = check_cpu_usage_sampler()
    if rc != 0:
        return rc
    rc = check_refresh_commit_boundary()
    if rc != 0:
        return rc
    rc = check_shared_payload_producer()
    if rc != 0:
        return rc
    with tempfile.TemporaryDirectory(prefix="v5_remote_relay_coalesce_") as tmp:
        rc = check_full_frame_waits_for_commit(Path(tmp))
        if rc != 0:
            return rc
        rc = check_dirty_payload_is_owned_snapshot(Path(tmp))
        if rc != 0:
            return rc
        state = relay.FrameState(Path(tmp), 4, 4)
        write_framebuffer(state.framebuffer_path, state.width, state.height)
        normalization_calls = [0]
        original_normalize = state._dirty_geometry._non_overlapping_union_rects

        def counted_normalize(rects):
            normalization_calls[0] += 1
            return original_normalize(rects)

        state._dirty_geometry._non_overlapping_union_rects = counted_normalize
        reader = relay.DirtyReader(state)
        reader.handle_line("2 1 2 0 0 1 1 2 2 1 1")
        batch = state.wait_dirty_batch_after(1, timeout=0.01, coalesce_seconds=0.0)
        if not batch or batch.get("stream_reset"):
            print("missing coalesced dirty batch", batch)
            return 1
        expected = {"frame_id": 2, "base_frame_id": 1, "merged_events": 2}
        for key, value in expected.items():
            if batch.get(key) != value:
                print("bad coalesced batch", key, batch)
                return 2
        rects = batch.get("rects")
        if rects != [
            {"x": 0, "y": 0, "w": 1, "h": 1, "codec": "raw"},
            {"x": 2, "y": 2, "w": 1, "h": 1, "codec": "raw"},
        ]:
            print("bad coalesced rects", rects)
            return 7
        prepared = state.dirty_payload(batch)
        if prepared is None:
            print("bad coalesced payload", None)
            return 3
        payload, payload_rects = prepared
        if len(payload) != 2 * 1 * 1 * 4 or payload_rects != rects:
            print("bad coalesced payload", len(payload), payload_rects)
            return 3
        if state.dirty_area_pixels(batch) != 2:
            print("disjoint dirty rects regressed to their 3x3 bounding rectangle", batch)
            return 25
        if normalization_calls[0] != 1:
            print("dirty batch geometry was normalized more than once", normalization_calls[0])
            return 61
        overlap_state = relay.FrameState(Path(tmp) / "overlap", 8, 8)
        overlap = {
            "frame_id": 3,
            "base_frame_id": 2,
            "rects": [
                {"x": 1, "y": 1, "w": 6, "h": 6},
                {"x": 1, "y": 1, "w": 6, "h": 6},
                {"x": 1, "y": 1, "w": 6, "h": 6},
            ],
        }
        overlap_rects = overlap_state.normalized_dirty_rects(overlap)
        if (
            overlap_state.dirty_area_pixels(overlap) != 36
            or overlap_state.is_large_dirty(overlap)
            or overlap_rects != [{"x": 1, "y": 1, "w": 6, "h": 6, "codec": "raw"}]
        ):
            print("overlapping dirty rects were double-counted or recopied", overlap_rects)
            return 59
        multi_band_state = relay.FrameState(Path(tmp) / "multi_band", 4, 6)
        multi_band = {
            "frame_id": 4,
            "base_frame_id": 3,
            "rects": [
                {"x": 0, "y": 0, "w": 2, "h": 2},
                {"x": 2, "y": 0, "w": 2, "h": 2},
                {"x": 0, "y": 4, "w": 2, "h": 2},
                {"x": 2, "y": 4, "w": 2, "h": 2},
            ],
        }
        multi_band_rects = multi_band_state.normalized_dirty_rects(multi_band)
        if multi_band_rects != [
            {"x": 0, "y": 0, "w": 4, "h": 2, "codec": "raw"},
            {"x": 0, "y": 4, "w": 4, "h": 2, "codec": "raw"},
        ]:
            print("multiple y bands were not merged horizontally", multi_band_rects)
            return 63
        single_state = relay.FrameState(Path(tmp) / "single", 4, 4)
        single_reader = relay.DirtyReader(single_state)
        single_reader.handle_line("2 1 1 0 0 4 4")
        single_event = single_state.first_dirty_event()
        if not single_event or single_event.get("rects") != [{"x": 0, "y": 0, "w": 4, "h": 4}] or any(
            single_event.get(key) != value for key, value in {"x": 0, "y": 0, "w": 4, "h": 4}.items()
        ):
            print("single-rect first-frame diagnostics lost canonical bounds", single_event)
            return 41
        rejected_state = relay.FrameState(Path(tmp) / "rejected", 4, 4)
        rejected_reader = relay.DirtyReader(rejected_state)
        rejected_reader.handle_line("2 1 0 0 1 1")
        if rejected_state.wait_dirty_batch_after(1, timeout=0.001, coalesce_seconds=0.0) is not None:
            print("retired per-rect dirty protocol was still accepted")
            return 39
        rejected_reader.handle_line("2 1 2 0 0 1 1")
        if rejected_state.wait_dirty_batch_after(1, timeout=0.001, coalesce_seconds=0.0) is not None:
            print("truncated dirty frame group was published")
            return 40
        metrics = state.metrics_snapshot()
        if metrics.get("framebuffer_mmap_refreshes", 0) < 1:
            print("dirty payload did not use framebuffer mmap", metrics)
            return 5
        if metrics.get("dirty_payload_rows") != 2 or metrics.get("dirty_payload_bytes") != 2 * 1 * 1 * 4 or metrics.get("dirty_payload_rects") != 2:
            print("bad dirty payload metrics", metrics)
            return 6
        large = {"frame_id": 4, "base_frame_id": 3, "x": 0, "y": 0, "w": 4, "h": 4}
        if not state.is_large_dirty(large):
            print("large dirty frame was not detected")
            return 8
        first_large_at = state.throttle_large_dirty(large, 0.0)
        second_large_at = state.throttle_large_dirty(large, first_large_at)
        large_metrics = state.metrics_snapshot()
        if second_large_at < first_large_at or large_metrics.get("dirty_large_throttle_sleeps", 0) < 1:
            print("large dirty throttle metrics missing", large_metrics)
            return 9
        many_rects = {
            "frame_id": 5,
            "base_frame_id": 4,
            "rects": [
                {"x": x, "y": y, "w": 1, "h": 1}
                for y in (0, 4, 8)
                for x in range(0, 44, 2)
            ],
        }
        bounded_state = relay.FrameState(Path(tmp) / "bounded", 43, 9)
        write_framebuffer(bounded_state.framebuffer_path, bounded_state.width, bounded_state.height)
        prepared_bounded = bounded_state.dirty_payload(many_rects)
        if prepared_bounded is None:
            print("bounded dirty payload missing")
            return 11
        bounded_payload, bounded_rects = prepared_bounded
        bounded_metrics = bounded_state.metrics_snapshot()
        if (
            len(bounded_rects) != relay.MAX_DIRTY_RECTS_PER_FRAME
            or len(bounded_rects) <= 1
            or any(int(rect["h"]) != 1 for rect in bounded_rects)
            or len(bounded_payload) != 68 * 4
            or bounded_metrics.get("dirty_payload_bounded_merge_frames") != 1
            or bounded_metrics.get("dirty_payload_bounded_merge_source_rects") != 66
            or bounded_metrics.get("dirty_payload_bounded_merge_output_rects") != relay.MAX_DIRTY_RECTS_PER_FRAME
            or bounded_metrics.get("dirty_payload_bounded_merge_added_pixels") != 2
        ):
            print("over-limit multi-band dirty geometry collapsed or exceeded its bound", bounded_rects, bounded_metrics)
            return 12
        cpu_samples = relay.cpu_samples_snapshot()
        process = relay.process_diagnostics()
        if not isinstance(cpu_samples, dict) or "pid" not in process or "threads" not in process:
            print("bad diagnostics samples", cpu_samples, process)
            return 13
        gap_state = relay.FrameState(Path(tmp) / "gap", 4, 4)
        gap_state.publish_dirty({"frame_id": 4, "base_frame_id": 3, "x": 0, "y": 0, "w": 1, "h": 1})
        gap = gap_state.wait_dirty_batch_after(1, timeout=0.01, coalesce_seconds=0.0)
        if not gap or not gap.get("stream_reset") or gap.get("reason") != "missing_dirty_event":
            print("missing dirty history did not request a stream restart", gap)
            return 4
        invalid_state = relay.FrameState(Path(tmp) / "invalid", 4, 4)
        invalid_state.publish_dirty({"frame_id": 2, "base_frame_id": 1, "x": 3, "y": 3, "w": 2, "h": 2})
        invalid = invalid_state.wait_dirty_batch_after(1, timeout=0.01, coalesce_seconds=0.0)
        if not invalid or not invalid.get("stream_reset") or invalid.get("reason") != "invalid_dirty_event":
            print("invalid dirty geometry did not request a stream restart", invalid)
            return 64
        ready_state = relay.FrameState(Path(tmp) / "ready", 4, 4)
        if ready_state.ui_ready():
            print("ready gate opened without metadata")
            return 14
        ready_state.publish_dirty({"frame_id": 2, "base_frame_id": 1, "x": 0, "y": 0, "w": 4, "h": 4})
        ready_state.ready_path.parent.mkdir(parents=True, exist_ok=True)
        ready_state.ready_path.write_text(
            json.dumps({"schema": "v5.ui_ready.v1", "ready": True, "current_frame_id": 2}),
            encoding="utf-8",
        )
        if not ready_state.ui_ready() or ready_state.first_dirty_event() != {
            "frame_id": 2,
            "base_frame_id": 1,
            "x": 0,
            "y": 0,
            "w": 4,
            "h": 4,
        }:
            print("ready gate did not bind to the formal first frame")
            return 15
        del bounded_payload
        del prepared_bounded
        del payload
        del prepared
        bounded_state.close_framebuffer_map()
        state.close_framebuffer_map()
    print("v5 remote ui relay coalesce smoke: dirty coalescing ok")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--shared-producer-only"]:
        producer_rc = check_shared_payload_producer()
        if producer_rc == 0:
            print("v5 remote ui relay shared producer: global history ok")
        raise SystemExit(producer_rc)
    if sys.argv[1:] == ["--source-contract-only"]:
        source_rc = check_refresh_commit_boundary()
        if source_rc == 0:
            print("v5 remote display source contract: last-flush atomic commit ok")
        raise SystemExit(source_rc)
    raise SystemExit(main())
