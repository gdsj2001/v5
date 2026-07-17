#!/usr/bin/env python3
from __future__ import annotations

import json
import tempfile
from pathlib import Path

from v5_bus_zero_resident_gate import verify_bus_zero_resident


def fixture(root: Path) -> tuple[Path, Path]:
    ini = root / "v5_bus.ini"
    runtime = root / "settings_runtime.json"
    ini.write_text(
        "[TRAJ]\nCOORDINATES = X Y Z A C\n"
        "[AXIS_A]\nWCHECKPOINT_COUNTS_PER_REV = 360000\n",
        encoding="utf-8")
    runtime.write_text(json.dumps({"axes": [{
        "axis": "A", "zero_model": {
            "source": "settings_axis_zero", "zero_anchor_counts": 1234,
            "zero_run_id": "run-7", "counts_per_unit": 1000,
        }, "rotary_load_counts_per_rev": 360000}]}), encoding="utf-8")
    return runtime, ini


def reader(anchor: float, valid: float = 1.0):
    values = {
        "v5-native-hal-owner.home-table-mapping-valid": valid,
        "v5-native-hal-owner.home-table-map-gen": 99,
        "v5-native-hal-owner.home-status-slot-03": 3,
        "v5-native-hal-owner.home-axis-code-03": ord("A"),
        "v5-native-hal-owner.home-mapping-generation-03": 99,
        "v5-native-hal-owner.home-zero-counts-03": anchor,
        "v5-native-hal-owner.home-counts-per-unit-03": 1000,
    }
    return lambda name: values[name]


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        runtime, ini = fixture(Path(temporary))
        try:
            verify_bus_zero_resident(runtime, ini, reader(5))
        except RuntimeError as exc:
            assert str(exc).startswith("BUS_ZERO_GATE_RESIDENT_ANCHOR_MISMATCH:A:")
        else:
            raise AssertionError("old resident anchor was accepted")
        result = verify_bus_zero_resident(runtime, ini, reader(1234))
        assert result["ok"] and result["checked"][0]["zero_run_id"] == "run-7"
        assert result["checked"][0]["counts_per_rev"] == 360000
        try:
            verify_bus_zero_resident(runtime, ini, reader(1234.00001))
        except RuntimeError as exc:
            assert "RESIDENT_ANCHOR_MISMATCH" in str(exc)
        else:
            raise AssertionError("inexact resident count was accepted")
        for failed_reader, marker in (
            (reader(1234, valid=0), "BUS_ZERO_GATE_MAPPING_INVALID"),
            (lambda _name: (_ for _ in ()).throw(OSError("rollback")), "rollback"),
        ):
            try:
                verify_bus_zero_resident(runtime, ini, failed_reader)
            except Exception as exc:
                assert marker in str(exc)
            else:
                raise AssertionError("failed restart/readback was accepted")
        ini.write_text(
            "[TRAJ]\nCOORDINATES = X Y Z A C\n"
            "[AXIS_A]\nWCHECKPOINT_COUNTS_PER_REV = 36000\n",
            encoding="utf-8")
        try:
            verify_bus_zero_resident(runtime, ini, reader(1234))
        except RuntimeError as exc:
            assert "ROTARY_CREV_MISMATCH:A" in str(exc)
        else:
            raise AssertionError("stale rotary Crev was accepted")
    init_text = (Path(__file__).parents[1] / "ui/init.d/v5-ui-relay").read_text(
        encoding="utf-8")
    assert "active_ini=pulse" in init_text
    assert "v5_ui_relay rejects disabled Pulse runtime mode" in init_text
    assert "BUS zero resident gate skipped active_mode=pulse" not in init_text
    assert 'expected_ini="$PROJECT_ROOT/linuxcnc/ini/v5_pulse.ini"' not in init_text
    assert init_text.index('case "$active_ini" in') < init_text.index('"$BUS_ZERO_GATE"')
    print("V5_BUS_ZERO_RESIDENT_GATE_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
