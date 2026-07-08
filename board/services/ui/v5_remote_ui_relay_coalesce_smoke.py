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


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="v5_remote_relay_coalesce_") as tmp:
        state = relay.FrameState(Path(tmp), 4, 4)
        write_framebuffer(state.framebuffer_path, state.width, state.height)
        state.publish_dirty({"frame_id": 2, "base_frame_id": 1, "x": 0, "y": 0, "w": 1, "h": 1})
        state.publish_dirty({"frame_id": 3, "base_frame_id": 2, "x": 2, "y": 2, "w": 1, "h": 1})
        batch = state.wait_dirty_batch_after(1, timeout=0.01, coalesce_seconds=0.0)
        if not batch or batch.get("needs_full"):
            print("missing coalesced dirty batch", batch)
            return 1
        expected = {"frame_id": 3, "base_frame_id": 1, "x": 0, "y": 0, "w": 3, "h": 3, "merged_events": 2}
        for key, value in expected.items():
            if batch.get(key) != value:
                print("bad coalesced batch", key, batch)
                return 2
        payload = state.dirty_payload(batch)
        if payload is None or len(payload) != 3 * 3 * 4:
            print("bad coalesced payload", None if payload is None else len(payload))
            return 3
        gap_state = relay.FrameState(Path(tmp) / "gap", 4, 4)
        gap_state.publish_dirty({"frame_id": 4, "base_frame_id": 3, "x": 0, "y": 0, "w": 1, "h": 1})
        gap = gap_state.wait_dirty_batch_after(1, timeout=0.01, coalesce_seconds=0.0)
        if not gap or not gap.get("needs_full"):
            print("missing full repair signal", gap)
            return 4
    print("v5 remote ui relay coalesce smoke: dirty coalescing ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
