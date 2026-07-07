#!/usr/bin/env python3
"""Compatibility wrapper for the project-root remote UI relay."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[2]
CANONICAL_SCRIPT_DIR = PROJECT_ROOT / "lvgl_app" / "scripts"
CANONICAL_RELAY = CANONICAL_SCRIPT_DIR / "v3_remote_ui_relay.py"


def _load_canonical_relay() -> Any:
    sys.path.insert(0, str(CANONICAL_SCRIPT_DIR))
    spec = importlib.util.spec_from_file_location("_v3_remote_ui_relay_canonical", CANONICAL_RELAY)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load canonical remote UI relay: {CANONICAL_RELAY}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_CANONICAL_RELAY = _load_canonical_relay()

for _name in dir(_CANONICAL_RELAY):
    if not _name.startswith("_") and _name != "main":
        globals()[_name] = getattr(_CANONICAL_RELAY, _name)


def main() -> int:
    return _CANONICAL_RELAY.main()


if __name__ == "__main__":
    raise SystemExit(main())
