#!/usr/bin/env python3
from __future__ import annotations

import json
import tempfile
from pathlib import Path

import v5_drive_bus_action as action
import v5_drive_bus_contract as contract


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="v5_drive_cache_smoke_") as tmp:
        root = Path(tmp)
        contract.PROJECT_ROOT = root
        contract.SELF_PARAMETER_TABLE = root / "config/settings/self_parameter_table.tsv"
        contract.DRIVE_PARAMETER_TABLE = root / "config/settings/drive_parameter_table.tsv"
        contract.SETTINGS_RUNTIME_JSON = root / "settings_runtime.json"
        contract.RUNTIME_SETTINGS_INI = root / "linuxcnc/ini/v5_bus.ini"
        contract.RESIDENT_SNAPSHOT = root / "run/drive_profile_resident_snapshot.json"
        write_text(
            contract.SETTINGS_RUNTIME_JSON,
            json.dumps({"schema": contract.SETTINGS_RUNTIME_SCHEMA, "axes": [{"axis": "X"}]}),
        )
        write_text(contract.RESIDENT_SNAPSHOT, json.dumps({"profiles": [{"profile_id": "smoke"}]}))
        write_text(contract.RUNTIME_SETTINGS_INI, "[AXIS_X]\nMAX_VELOCITY = 1\n")
        write_text(contract.SELF_PARAMETER_TABLE, "X\tslave\t0\n")
        first = action.preload_resident_state()
        first_bindings = action.load_self_slave_bindings()
        write_text(contract.SELF_PARAMETER_TABLE, "X\tslave\tNAT\n")
        second = action.preload_resident_state()
        second_bindings = action.load_self_slave_bindings()
        if not first.get("ok") or not second.get("ok"):
            print("preload failed", first, second)
            return 1
        if first_bindings.get("X") != "0" or second_bindings.get("X") != "NAT":
            print("stale binding cache", first_bindings, second_bindings)
            return 2
    print("v5 drive bus action cache smoke: preload cache invalidation ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
