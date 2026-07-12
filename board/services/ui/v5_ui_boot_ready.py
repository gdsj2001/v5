#!/usr/bin/env python3
"""Validate the off-screen page queue and publish the one-shot UI ready gate."""

from __future__ import annotations

import argparse
import json
import os
import re
import tempfile
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

from v5_ui_cache_queue_contract import validate_queue_trace


EXPECTED_QUEUE = (
    "main",
    "settings",
    "tool",
    "probe",
    "offset",
    "io",
    "network",
    "program",
    "mdi",
)
EXPECTED_CACHE_BUDGET_BYTES = 1024 * 600 * 4 * 13
CACHE_LINE_RE = re.compile(
    r"^V5_UI_CACHE_PREP page=(?P<page>[a-z]+) "
    r"completed=(?P<completed>[0-9]+) total=(?P<total>[0-9]+) "
    r"elapsed_us=(?P<elapsed>[0-9]+) "
    r"create_us=(?P<create>[0-9]+) prepare_us=(?P<prepare>[0-9]+) "
    r"yield_us=(?P<yield_us>[0-9]+) "
    r"cpu_pct_x100=(?P<cpu_pct_x100>[0-9]+) "
    r"peak_cpu_pct_x100=(?P<peak_cpu_pct_x100>[0-9]+) "
    r"worker_id=(?P<worker_id>[0-9]+) cache_valid=(?P<cache_valid>[01]) "
    r"invalidation_clean=(?P<invalidation_clean>[01]) "
    r"budget_bytes=(?P<budget>[0-9]+)$"
)


class ReadyError(RuntimeError):
    pass


def parse_cache_rows(text: str) -> list[dict]:
    rows: list[dict] = []
    for line in text.splitlines():
        match = CACHE_LINE_RE.match(line.strip())
        if not match:
            continue
        rows.append({
            "page": match.group("page"),
            "completed": int(match.group("completed")),
            "total": int(match.group("total")),
            "elapsed_us": int(match.group("elapsed")),
            "create_us": int(match.group("create")),
            "prepare_us": int(match.group("prepare")),
            "yield_us": int(match.group("yield_us")),
            "cpu_pct_x100": int(match.group("cpu_pct_x100")),
            "peak_cpu_pct_x100": int(match.group("peak_cpu_pct_x100")),
            "worker_id": int(match.group("worker_id")),
            "cache_valid": int(match.group("cache_valid")),
            "invalidation_clean": int(match.group("invalidation_clean")),
            "budget_bytes": int(match.group("budget")),
        })
    return rows


def validate_cache_rows(rows: list[dict]) -> None:
    pages = tuple(row["page"] for row in rows)
    observed_peak = 0
    if pages != EXPECTED_QUEUE:
        completed = len(rows)
        failure_page = EXPECTED_QUEUE[completed] if completed < len(EXPECTED_QUEUE) else "queue_duplicate_or_extra"
        raise ReadyError(
            f"cache queue mismatch pages={pages!r} expected={EXPECTED_QUEUE!r} failure_page={failure_page}"
        )
    for index, row in enumerate(rows, 1):
        if row["completed"] != index or row["total"] != len(EXPECTED_QUEUE):
            raise ReadyError(f"cache queue counters invalid page={row['page']} row={row!r}")
        if row["elapsed_us"] <= 0:
            raise ReadyError(f"cache page elapsed time missing page={row['page']}")
        if row["create_us"] + row["prepare_us"] + row["yield_us"] > row["elapsed_us"]:
            raise ReadyError(f"cache page timing counters exceed elapsed page={row['page']} row={row!r}")
        if row["yield_us"] <= 0 or row["worker_id"] != 0:
            raise ReadyError(f"cache page was not serialized on worker 0 page={row['page']} row={row!r}")
        if row["cache_valid"] != 1 or row["invalidation_clean"] != 1:
            raise ReadyError(f"cache page retained dirty/incomplete state page={row['page']} row={row!r}")
        observed_peak = max(observed_peak, row["cpu_pct_x100"])
        if row["peak_cpu_pct_x100"] != observed_peak:
            raise ReadyError(
                f"cache CPU peak sequence invalid page={row['page']} "
                f"actual={row['peak_cpu_pct_x100']} expected={observed_peak}"
            )
        if row["budget_bytes"] != EXPECTED_CACHE_BUDGET_BYTES:
            raise ReadyError(
                f"cache budget mismatch page={row['page']} actual={row['budget_bytes']} "
                f"expected={EXPECTED_CACHE_BUDGET_BYTES}"
            )


def read_status_field(pid: int, field: str) -> str:
    try:
        lines = Path(f"/proc/{pid}/status").read_text(encoding="ascii", errors="replace").splitlines()
    except OSError:
        return ""
    prefix = field + ":"
    for line in lines:
        if line.startswith(prefix):
            return line.split(":", 1)[1].strip()
    return ""


def read_diagnostics(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=1.0) as response:
        payload = json.load(response)
    if not isinstance(payload, dict):
        raise ReadyError("relay diagnostics is not an object")
    return payload


def validate_first_event(diagnostics: dict, width: int, height: int) -> dict:
    event = diagnostics.get("first_dirty_event")
    if not isinstance(event, dict):
        raise ReadyError("relay has not captured the formal first frame")
    expected_rect = (0, 0, width, height)
    actual_rect = tuple(int(event.get(key, -1)) for key in ("x", "y", "w", "h"))
    if actual_rect != expected_rect:
        raise ReadyError(f"first frame is not full main blit actual={actual_rect} expected={expected_rect}")
    frame_id = int(event.get("frame_id") or 0)
    base_frame_id = int(event.get("base_frame_id") or -1)
    if frame_id <= 1 or base_frame_id != 1:
        raise ReadyError(f"first frame identity invalid event={event!r}")
    return dict(event)


def atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temp_name, 0o600)
        os.replace(temp_name, path)
    finally:
        try:
            os.unlink(temp_name)
        except FileNotFoundError:
            pass


def build_ready_payload(
    ui_pid: int,
    ui_log: Path,
    diagnostics: dict,
    width: int,
    height: int,
    required_cpu_list: str,
) -> dict:
    try:
        text = ui_log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise ReadyError(f"cannot read UI boot log: {exc}") from exc
    try:
        validate_queue_trace(text.splitlines())
    except ValueError as exc:
        raise ReadyError(f"cache queue evidence invalid: {exc}") from exc
    rows = parse_cache_rows(text)
    validate_cache_rows(rows)
    if "v5 UI remote framebuffer IPC ready:" not in text:
        raise ReadyError("UI process has not crossed the post-cache IPC-ready boundary")
    cpus_allowed = read_status_field(ui_pid, "Cpus_allowed_list")
    if cpus_allowed != required_cpu_list:
        raise ReadyError(
            f"UI pre-render affinity invalid pid={ui_pid} Cpus_allowed_list={cpus_allowed!r} "
            f"expected={required_cpu_list!r}"
        )
    first_event = validate_first_event(diagnostics, width, height)
    current_frame_id = int(diagnostics.get("frame_id") or 0)
    if current_frame_id < int(first_event["frame_id"]):
        raise ReadyError(
            f"relay frame identity regressed first={first_event['frame_id']} current={current_frame_id}"
        )
    if diagnostics.get("ui_ready"):
        raise ReadyError("stale ready metadata was visible before this boot completed")
    return {
        "schema": "v5.ui_ready.v1",
        "ready": True,
        "boot_id": str(uuid.uuid4()),
        "ui_pid": ui_pid,
        "cpus_allowed_list": cpus_allowed,
        "width": width,
        "height": height,
        "cache_page_count": len(EXPECTED_QUEUE),
        "cache_slot_count": 13,
        "cache_budget_bytes": EXPECTED_CACHE_BUDGET_BYTES,
        "cache_queue": rows,
        "cache_peak_cpu_pct_x100": max(row["peak_cpu_pct_x100"] for row in rows),
        "first_frame": first_event,
        "current_frame_id": current_frame_id,
        "created_realtime_ns": time.time_ns(),
        "created_monotonic_ns": time.monotonic_ns(),
    }


def wait_and_publish(args: argparse.Namespace) -> dict:
    deadline = time.monotonic() + args.timeout
    last_error = "not_checked"
    while time.monotonic() < deadline:
        if not Path(f"/proc/{args.ui_pid}/status").exists():
            raise ReadyError(f"UI process exited before ready pid={args.ui_pid}")
        try:
            diagnostics = read_diagnostics(args.diagnostics_url)
            payload = build_ready_payload(
                args.ui_pid,
                args.ui_log,
                diagnostics,
                args.width,
                args.height,
                args.require_cpu_list,
            )
            atomic_write_json(args.ready_path, payload)
            return payload
        except (OSError, ReadyError, urllib.error.URLError) as exc:
            last_error = str(exc)
            time.sleep(0.05)
    rows: list[dict] = []
    try:
        rows = parse_cache_rows(args.ui_log.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        pass
    failure_page = EXPECTED_QUEUE[len(rows)] if len(rows) < len(EXPECTED_QUEUE) else "post_cache_ready"
    raise ReadyError(
        f"UI ready timeout seconds={args.timeout:.1f} completed={len(rows)}/9 "
        f"failure_page={failure_page} last_error={last_error}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate v5 UI boot cache queue and publish ui_ready metadata.")
    parser.add_argument("--ui-pid", type=int, required=True)
    parser.add_argument("--ui-log", type=Path, required=True)
    parser.add_argument("--ready-path", type=Path, required=True)
    parser.add_argument("--diagnostics-url", required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--require-cpu-list", default="1")
    args = parser.parse_args()
    try:
        payload = wait_and_publish(args)
    except ReadyError as exc:
        print(f"v5_ui_boot_ready FAIL {exc}", file=os.sys.stderr)
        return 1
    print(
        "v5_ui_boot_ready PASS "
        f"boot_id={payload['boot_id']} ui_pid={payload['ui_pid']} "
        f"frame_id={payload['current_frame_id']} cache_budget_bytes={payload['cache_budget_bytes']}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
