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
        "static void invalidate_toolpath_line_segment",
        "invalidate_toolpath_line_segment(line, points);",
        "lv_obj_invalidate_area(line, &dirty);",
    ]
    if any(token not in toolpath_source for token in precise_line_tokens):
        print("dynamic toolpath line precise invalidation is missing")
        return 28
    if axis_line_body.count("invalidate_toolpath_line_segment(line, points);") != 2:
        print("dynamic toolpath line does not invalidate both old and new segment bounds")
        return 29
    if axis_line_body.count("lv_line_set_points(line, points, 2);") != 1:
        print("visible dynamic toolpath line still uses whole-object invalidation")
        return 30
    hide_ac_start = toolpath_source.index("void v5_main_page_internal_hide_toolpath_ac_geometry")
    hide_ac_end = toolpath_source.index("void v5_main_page_internal_hide_toolpath_program_wcs_objects", hide_ac_start)
    if "toolpath_holder_line" in toolpath_source[hide_ac_start:hide_ac_end]:
        print("AC geometry refresh still hides and re-shows the holder line")
        return 37
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
    required_dirty_snapshot = ["payload = bytearray(total_bytes)", "flock(fd, LOCK_SH)", "return bytes(payload), rects"]
    if any(token not in dirty_body for token in required_dirty_snapshot) or "memoryview(mapped)" in dirty_body:
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
    def __init__(self):
        self.width = 4
        self.height = 4
        self.stride = self.width * 4
        self.frame_id = 1
        self.condition = threading.Condition()
        self.events: list[dict] = []
        self.metrics: dict[str, int] = {}
        self.build_count = 0

    def mark_metric(self, name: str, delta: int = 1) -> None:
        self.metrics[name] = self.metrics.get(name, 0) + int(delta)

    def metrics_snapshot(self) -> dict:
        return dict(self.metrics)

    def publish_dirty(self, event: dict) -> None:
        with self.condition:
            self.events.append(dict(event))
            self.frame_id = max(self.frame_id, int(event["frame_id"]))
            self.condition.notify_all()

    def wait_dirty_batch_after(self, frame_id: int, timeout: float, coalesce_seconds: float) -> dict | None:
        deadline = time.monotonic() + timeout
        with self.condition:
            while not any(int(event["frame_id"]) > frame_id for event in self.events):
                remaining = deadline - time.monotonic()
                if remaining <= 0.0:
                    return None
                self.condition.wait(remaining)
            if coalesce_seconds > 0.0:
                self.condition.wait(coalesce_seconds)
            events = [dict(event) for event in self.events if int(event["frame_id"]) > frame_id]
        events.sort(key=lambda event: int(event["frame_id"]))
        expected_base = int(frame_id)
        rects = []
        for event in events:
            if int(event["base_frame_id"]) != expected_base:
                return {"needs_full": True, "frame_id": int(event["frame_id"])}
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
        self.build_count += 1
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
        return bytes([int(batch["frame_id"]) & 0xFF]) * payload_size, rects


def check_global_producer_source_contract() -> int:
    relay_source = Path(__file__).with_name("v5_remote_ui_relay.py").read_text(encoding="utf-8")
    producer_source = Path(__file__).with_name("v5_remote_ui_shared_payload.py").read_text(encoding="utf-8")
    required = [
        "SharedDirtyPayloadProducer",
        "FRAME_PRODUCER_NICE = 10",
        "os.nice(FRAME_PRODUCER_NICE)",
        "dirty_payload_shared_producer_priority_errors",
        "self.server.payload_producer.subscribe(frame_id)",
        "self.server.payload_producer.wait_after(last_sent",
        "self.server.payload_producer.unsubscribe()",
        "deque(maxlen=max(1, int(history_limit)))",
    ]
    if any(token not in relay_source + producer_source for token in required):
        print("global dirty payload producer contract is incomplete")
        return 42
    unsupported_thread_id_tokens = ("threading.get_native_id", "/proc/self/task/")
    if any(token in producer_source for token in unsupported_thread_id_tokens):
        print("global producer still depends on a Python 3.8 thread-id API")
        return 52
    retired = ["SharedDirtyPayloadCache", "payload_cache", "self.state.wait_dirty_batch_after(last_sent"]
    if any(token in relay_source + producer_source for token in retired):
        print("retired per-client dirty payload path is still present")
        return 43
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
        if state.build_count != 1 or not isinstance(first.frame.meta_bytes, bytes) or not isinstance(first.frame.payload, bytes):
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


def check_missing_history_requests_full_repair() -> int:
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
        if missing is None or not missing.needs_full or missing.frame is not None:
            print("missing producer history did not request full repair", missing)
            return 50
    finally:
        producer.unsubscribe()
        producer.stop()
    return 0


def check_shared_payload_producer() -> int:
    for check in (
        check_global_producer_source_contract,
        check_skewed_stream_clients_share_prepared_history,
        check_slow_client_does_not_block_global_producer,
        check_missing_history_requests_full_repair,
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
        reader = relay.DirtyReader(state)
        reader.handle_line("2 1 2 0 0 1 1 2 2 1 1")
        batch = state.wait_dirty_batch_after(1, timeout=0.01, coalesce_seconds=0.0)
        if not batch or batch.get("needs_full"):
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
            "rects": [{"x": index, "y": 0, "w": 1, "h": 1} for index in range(relay.MAX_DIRTY_RECTS_PER_FRAME + 1)],
        }
        wide_state = relay.FrameState(Path(tmp) / "wide", relay.MAX_DIRTY_RECTS_PER_FRAME + 1, 1)
        write_framebuffer(wide_state.framebuffer_path, wide_state.width, wide_state.height)
        prepared_union = wide_state.dirty_payload(many_rects)
        if prepared_union is None:
            print("union dirty payload missing")
            return 11
        union_payload, union_rects = prepared_union
        union_metrics = wide_state.metrics_snapshot()
        expected_union = [{"x": 0, "y": 0, "w": relay.MAX_DIRTY_RECTS_PER_FRAME + 1, "h": 1, "codec": "raw"}]
        if union_rects != expected_union or union_metrics.get("dirty_payload_union_frames") != 1:
            print("bad union dirty payload", union_rects, union_metrics)
            return 12
        cpu_samples = relay.cpu_samples_snapshot()
        process = relay.process_diagnostics()
        if not isinstance(cpu_samples, dict) or "pid" not in process or "threads" not in process:
            print("bad diagnostics samples", cpu_samples, process)
            return 13
        gap_state = relay.FrameState(Path(tmp) / "gap", 4, 4)
        gap_state.publish_dirty({"frame_id": 4, "base_frame_id": 3, "x": 0, "y": 0, "w": 1, "h": 1})
        gap = gap_state.wait_dirty_batch_after(1, timeout=0.01, coalesce_seconds=0.0)
        if not gap or not gap.get("needs_full"):
            print("missing full repair signal", gap)
            return 4
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
        del union_payload
        del prepared_union
        del payload
        del prepared
        wide_state.close_framebuffer_map()
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
