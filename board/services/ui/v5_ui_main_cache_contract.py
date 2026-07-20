#!/usr/bin/env python3
"""Validate the boot-only Main page cache trace.

Non-Main pages are deliberately absent: they are created on the actual
navigation edge and therefore cannot delay ui_ready.
"""

from __future__ import annotations


PREFIX = "V5_UI_MAIN_CACHE "


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


def validate_main_cache_trace(lines: list[str]) -> None:
    events = []
    for line in lines:
        event = parse_event(line)
        if event is not None:
            events.append(event)
    if not events:
        raise ValueError("missing V5_UI_MAIN_CACHE events")
    failures = [event for event in events if event.get("event") == "fail"]
    if failures:
        raise ValueError(f"Main cache reported failure: {failures[0]}")
    if len(events) != 2:
        raise ValueError(f"Main cache requires exactly 2 events, got {len(events)}")

    start, end = events
    require(start, "event", "start", "start")
    require(start, "page", "main", "start")
    require(start, "slot", "0", "start")
    require(start, "output_suppressed", "1", "start")

    require(end, "event", "end", "end")
    require(end, "page", "main", "end")
    require(end, "slot", "0", "end")
    for key in (
        "create_ok",
        "apply_ok",
        "render_ok",
        "capture_ok",
        "cache_valid",
        "invalidation_clean",
    ):
        require(end, key, "1", "end")
    for key in ("elapsed_us", "create_us", "prepare_us", "budget_bytes"):
        try:
            value = int(end.get(key, ""))
        except ValueError as exc:
            raise ValueError(f"end: invalid {key}={end.get(key)!r}") from exc
        if value <= 0:
            raise ValueError(f"end: expected positive {key}, got {value}")
    try:
        cpu_pct_x100 = int(end.get("cpu_pct_x100", ""))
    except ValueError as exc:
        raise ValueError(
            f"end: invalid cpu_pct_x100={end.get('cpu_pct_x100')!r}"
        ) from exc
    if cpu_pct_x100 < 0:
        raise ValueError(f"end: negative cpu_pct_x100={cpu_pct_x100}")


def self_test_lines() -> list[str]:
    return [
        "V5_UI_MAIN_CACHE event=start page=main slot=0 output_suppressed=1",
        "V5_UI_MAIN_CACHE event=end page=main slot=0 create_ok=1 apply_ok=1 "
        "render_ok=1 capture_ok=1 cache_valid=1 invalidation_clean=1 "
        "elapsed_us=100 create_us=40 prepare_us=60 cpu_pct_x100=2500 "
        "budget_bytes=31948800",
    ]


def expect_invalid(label: str, lines: list[str]) -> None:
    try:
        validate_main_cache_trace(lines)
    except ValueError:
        return
    raise AssertionError(f"negative self-test unexpectedly passed: {label}")


def run_self_test() -> None:
    valid = self_test_lines()
    validate_main_cache_trace(valid)
    expect_invalid("missing_end", valid[:1])
    expect_invalid("wrong_page", [valid[0].replace("page=main", "page=settings"), valid[1]])
    expect_invalid(
        "invalid_cache",
        [valid[0], valid[1].replace("cache_valid=1", "cache_valid=0")],
    )
    expect_invalid("legacy_extra_page", valid + ["V5_UI_MAIN_CACHE event=start page=tool slot=2"])


if __name__ == "__main__":
    run_self_test()
    print("V5_UI_MAIN_CACHE_CONTRACT_OK")
