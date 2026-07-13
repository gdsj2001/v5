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
    if commit_body.count("++g_frame_id;") != 1 or commit_body.count("notify_remote_dirty(") != 1:
        print("composed refresh does not publish exactly one frame and dirty event")
        return 22
    if "accumulate_dirty_area(x1, y1, x2, y2);" not in compose_body:
        print("dirty union accumulation missing")
        return 23
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


def main() -> int:
    global relay
    import v5_remote_ui_relay as relay

    rc = check_cpu_usage_sampler()
    if rc != 0:
        return rc
    rc = check_refresh_commit_boundary()
    if rc != 0:
        return rc
    with tempfile.TemporaryDirectory(prefix="v5_remote_relay_coalesce_") as tmp:
        rc = check_full_frame_waits_for_commit(Path(tmp))
        if rc != 0:
            return rc
        state = relay.FrameState(Path(tmp), 4, 4)
        write_framebuffer(state.framebuffer_path, state.width, state.height)
        state.publish_dirty({"frame_id": 2, "base_frame_id": 1, "x": 0, "y": 0, "w": 1, "h": 1})
        state.publish_dirty({"frame_id": 3, "base_frame_id": 2, "x": 2, "y": 2, "w": 1, "h": 1})
        batch = state.wait_dirty_batch_after(1, timeout=0.01, coalesce_seconds=0.0)
        if not batch or batch.get("needs_full"):
            print("missing coalesced dirty batch", batch)
            return 1
        expected = {"frame_id": 3, "base_frame_id": 1, "merged_events": 2}
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
    if sys.argv[1:] == ["--source-contract-only"]:
        source_rc = check_refresh_commit_boundary()
        if source_rc == 0:
            print("v5 remote display source contract: last-flush atomic commit ok")
        raise SystemExit(source_rc)
    raise SystemExit(main())
