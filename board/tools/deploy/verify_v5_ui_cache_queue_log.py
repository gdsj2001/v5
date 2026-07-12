#!/usr/bin/env python3
"""Validate the machine-readable V5 UI boot cache queue trace."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


UI_SERVICE_SOURCE = Path(__file__).resolve().parents[2] / "services" / "ui"
sys.path.insert(0, str(UI_SERVICE_SOURCE))

from v5_ui_cache_queue_contract import run_self_test, validate_queue_trace


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        try:
            run_self_test()
        except (AssertionError, ValueError) as exc:
            print(f"V5_UI_CACHE_QUEUE_VERIFY SELF_TEST_FAIL: {exc}", file=sys.stderr)
            return 1
        print("V5_UI_CACHE_QUEUE_VERIFY SELF_TEST_PASS")
        return 0
    if args.log:
        lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    else:
        lines = sys.stdin.read().splitlines()
    try:
        validate_queue_trace(lines)
    except (OSError, ValueError) as exc:
        print(f"V5_UI_CACHE_QUEUE_VERIFY FAIL: {exc}", file=sys.stderr)
        return 1
    print("V5_UI_CACHE_QUEUE_VERIFY PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
