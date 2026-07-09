#!/usr/bin/env python3
from __future__ import annotations

import tempfile
from pathlib import Path

import v5_remote_ui_relay as relay


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


def main() -> int:
    rc = check_cpu_usage_sampler()
    if rc != 0:
        return rc
    with tempfile.TemporaryDirectory(prefix="v5_remote_relay_coalesce_") as tmp:
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
        _, union_rects = prepared_union
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
        wide_state.close_framebuffer_map()
        state.close_framebuffer_map()
    print("v5 remote ui relay coalesce smoke: dirty coalescing ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
