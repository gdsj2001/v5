#!/usr/bin/env python3
"""Canonical parser and validator for the serialized V5 UI cache queue trace."""

from __future__ import annotations


PAGES = (
    ("main", 0),
    ("settings", 1),
    ("tool", 2),
    ("probe", 3),
    ("offset", 4),
    ("io", 5),
    ("network", 6),
    ("program", 7),
    ("mdi", 8),
)
PREFIX = "V5_UI_CACHE_QUEUE "


def parse_event(line: str) -> dict[str, str] | None:
    line = line.strip()
    if not line.startswith(PREFIX):
        return None
    fields: dict[str, str] = {}
    for token in line[len(PREFIX) :].split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def require(fields: dict[str, str], key: str, expected: str, context: str) -> None:
    actual = fields.get(key)
    if actual != expected:
        raise ValueError(f"{context}: expected {key}={expected}, got {actual!r}")


def validate_queue_trace(lines: list[str]) -> None:
    events = []
    for line in lines:
        event = parse_event(line)
        if event is not None:
            events.append(event)
    if not events:
        raise ValueError("missing V5_UI_CACHE_QUEUE events")
    failures = [event for event in events if event.get("event") == "fail"]
    if failures:
        raise ValueError(f"queue reported failure: {failures[0]}")

    expected_event_count = 2 + (2 * len(PAGES))
    if len(events) != expected_event_count:
        raise ValueError(
            f"queue requires exactly {expected_event_count} events, got {len(events)}"
        )

    start = events[0]
    require(start, "event", "start", "event[0]")
    require(start, "worker_id", "0", "start")
    require(start, "worker_count", "1", "start")
    require(start, "mode", "caller_thread", "start")
    require(start, "total", str(len(PAGES)), "start")
    require(start, "output_suppressed", "1", "start")

    cursor = 1
    for sequence, (name, slot) in enumerate(PAGES):
        page_start = events[cursor]
        page_end = events[cursor + 1]
        context = f"page[{sequence}]"
        require(page_start, "event", "page_start", f"{context}.start")
        require(page_end, "event", "page_end", f"{context}.end")
        for event in (page_start, page_end):
            require(event, "worker_id", "0", context)
            require(event, "sequence", str(sequence), context)
            require(event, "page", name, context)
            require(event, "slot", str(slot), context)
        for key in (
            "create_ok",
            "apply_ok",
            "render_ok",
            "capture_ok",
            "cache_valid",
            "invalidation_clean",
            "yielded",
        ):
            require(page_end, key, "1", context)
        cursor += 2

    end = events[cursor]
    require(end, "event", "end", f"event[{cursor}]")
    require(end, "worker_id", "0", "end")
    require(end, "worker_count", "1", "end")
    require(end, "completed", str(len(PAGES)), "end")
    require(end, "total", str(len(PAGES)), "end")
    require(end, "evidence_valid", "1", "end")


def self_test_lines() -> list[str]:
    lines = [
        "V5_UI_CACHE_QUEUE event=start worker_id=0 worker_count=1 "
        "mode=caller_thread total=9 output_suppressed=1"
    ]
    for sequence, (name, slot) in enumerate(PAGES):
        lines.append(
            f"V5_UI_CACHE_QUEUE event=page_start worker_id=0 sequence={sequence} "
            f"page={name} slot={slot}"
        )
        lines.append(
            f"V5_UI_CACHE_QUEUE event=page_end worker_id=0 sequence={sequence} "
            f"page={name} slot={slot} create_ok=1 apply_ok=1 render_ok=1 "
            "capture_ok=1 cache_valid=1 invalidation_clean=1 yielded=1"
        )
    lines.append(
        "V5_UI_CACHE_QUEUE event=end worker_id=0 worker_count=1 completed=9 "
        "total=9 evidence_valid=1 peak_cpu_pct_x100=2500"
    )
    return lines


def expect_invalid(label: str, lines: list[str]) -> None:
    try:
        validate_queue_trace(lines)
    except ValueError:
        return
    raise AssertionError(f"negative self-test unexpectedly passed: {label}")


def run_self_test() -> None:
    valid = self_test_lines()
    validate_queue_trace(valid)

    page_starts = [line for line in valid if "event=page_start" in line]
    page_ends = [line for line in valid if "event=page_end" in line]
    overlapping = [valid[0], *page_starts, *page_ends, valid[-1]]
    expect_invalid("overlapping_page_workers", overlapping)

    missing_yield = list(valid)
    missing_yield[2] = missing_yield[2].replace("yielded=1", "yielded=0")
    expect_invalid("missing_yield", missing_yield)

    dirty_remaining = list(valid)
    dirty_remaining[2] = dirty_remaining[2].replace(
        "invalidation_clean=1", "invalidation_clean=0"
    )
    expect_invalid("dirty_remaining", dirty_remaining)
