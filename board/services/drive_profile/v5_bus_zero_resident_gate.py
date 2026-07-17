#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from pathlib import Path
from typing import Callable

_hal_reader_component = None


def active_axes_from_ini(path: Path) -> list[str]:
    section = ""
    for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().upper()
        elif section == "TRAJ" and "=" in line:
            key, value = line.split("=", 1)
            if key.strip().upper() == "COORDINATES":
                return re.findall(r"[XYZABCUVW]", value.upper())
    raise RuntimeError("BUS_ZERO_GATE_COORDINATES_MISSING")


def ini_axis_number(path: Path, axis: str, key: str) -> float:
    target_section = "AXIS_%s" % axis
    target_key = key.upper()
    section = ""
    matches = []
    for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().upper()
        elif section == target_section and "=" in line:
            name, value = line.split("=", 1)
            if name.strip().upper() == target_key:
                try:
                    matches.append(float(value.strip()))
                except ValueError as exc:
                    raise RuntimeError(
                        "BUS_ZERO_GATE_INI_VALUE_INVALID:%s:%s" %
                        (axis, target_key)) from exc
    if len(matches) != 1 or not math.isfinite(matches[0]):
        raise RuntimeError(
            "BUS_ZERO_GATE_INI_VALUE_NOT_UNIQUE:%s:%s:%d" %
            (axis, target_key, len(matches)))
    return matches[0]


def hal_value(name: str) -> float:
    global _hal_reader_component
    try:
        dist_packages = "/usr/lib/python3/dist-packages"
        if dist_packages not in sys.path:
            sys.path.insert(0, dist_packages)
        import hal
        if _hal_reader_component is None:
            _hal_reader_component = hal.component("v5-bus-zero-gate-%d" % os.getpid())
            _hal_reader_component.ready()
        return float(hal.get_value(name))
    except Exception as exc:
        raise RuntimeError("BUS_ZERO_GATE_HAL_READ_FAILED:%s" % name) from exc


def verify_bus_zero_resident(
        runtime_path: Path, ini_path: Path,
        reader: Callable[[str], float] = hal_value) -> dict:
    runtime = json.loads(runtime_path.read_text(encoding="utf-8"))
    axes = active_axes_from_ini(ini_path)
    by_axis = {
        str(item.get("axis") or "").upper(): item
        for item in runtime.get("axes", []) if isinstance(item, dict)
    }
    if not reader("v5-native-hal-owner.home-table-mapping-valid"):
        raise RuntimeError("BUS_ZERO_GATE_MAPPING_INVALID")
    mapping_generation = int(reader("v5-native-hal-owner.home-table-map-gen"))
    if mapping_generation <= 0:
        raise RuntimeError("BUS_ZERO_GATE_MAPPING_GENERATION_INVALID")
    all_configured_zero_axes = {
        axis for axis, cfg in by_axis.items()
        if isinstance(cfg.get("zero_model"), dict)
        and cfg["zero_model"].get("source") == "settings_axis_zero"
    }
    configured_zero_axes = all_configured_zero_axes.intersection(axes)
    checked = []
    for slot, axis in enumerate(axes):
        cfg = by_axis.get(axis, {})
        zero = cfg.get("zero_model") if isinstance(cfg.get("zero_model"), dict) else {}
        if zero.get("source") != "settings_axis_zero":
            continue
        run_id = str(zero.get("zero_run_id") or "")
        anchor = float(zero.get("zero_anchor_counts"))
        counts_per_unit = float(zero.get("counts_per_unit"))
        if (not math.isfinite(anchor) or not math.isfinite(counts_per_unit) or
                counts_per_unit <= 0.0):
            raise RuntimeError("BUS_ZERO_GATE_SOURCE_IDENTITY_MISSING:%s" % axis)
        actual_slot = int(reader(
            "v5-native-hal-owner.home-status-slot-%02d" % slot))
        axis_code = int(reader(
            "v5-native-hal-owner.home-axis-code-%02d" % slot))
        axis_generation = int(reader(
            "v5-native-hal-owner.home-mapping-generation-%02d" % slot))
        if actual_slot != slot or axis_code != ord(axis) or axis_generation != mapping_generation:
            raise RuntimeError("BUS_ZERO_GATE_SLOT_MAPPING_MISMATCH:%s" % axis)
        actual = reader(
            "v5-native-hal-owner.home-zero-counts-%02d" % slot)
        if not math.isfinite(actual) or not math.isclose(
                actual, anchor, rel_tol=0.0, abs_tol=1.0e-6):
            raise RuntimeError(
                "BUS_ZERO_GATE_RESIDENT_ANCHOR_MISMATCH:%s:%s:%s" %
                (axis, anchor, actual))
        resident_counts_per_unit = reader(
            "v5-native-hal-owner.home-counts-per-unit-%02d" % slot)
        if (not math.isfinite(resident_counts_per_unit) or
                not math.isclose(resident_counts_per_unit, counts_per_unit,
                                 rel_tol=0.0, abs_tol=1.0e-6)):
            raise RuntimeError(
                "BUS_ZERO_GATE_RESIDENT_SCALE_MISMATCH:%s:%s:%s" %
                (axis, counts_per_unit, resident_counts_per_unit))
        counts_per_rev = None
        if axis in {"A", "B", "C"}:
            counts_per_rev_float = counts_per_unit * 360.0
            counts_per_rev = int(round(counts_per_rev_float))
            runtime_counts_per_rev = float(
                cfg.get("rotary_load_counts_per_rev"))
            ini_counts_per_rev = ini_axis_number(
                ini_path, axis, "WCHECKPOINT_COUNTS_PER_REV")
            if (counts_per_rev <= 0 or
                    abs(counts_per_rev_float - counts_per_rev) > 1.0e-6 or
                    not math.isclose(runtime_counts_per_rev, counts_per_rev,
                                     rel_tol=0.0, abs_tol=1.0e-6) or
                    not math.isclose(ini_counts_per_rev, counts_per_rev,
                                     rel_tol=0.0, abs_tol=1.0e-6)):
                raise RuntimeError(
                    "BUS_ZERO_GATE_ROTARY_CREV_MISMATCH:%s:%s:%s:%s" %
                    (axis, counts_per_rev, runtime_counts_per_rev,
                     ini_counts_per_rev))
        checked.append({
            "axis": axis, "zero_anchor_counts": anchor,
            "zero_run_id": run_id,
            "counts_per_unit": counts_per_unit,
            "counts_per_rev": counts_per_rev,
        })
    if configured_zero_axes and not checked:
        raise RuntimeError("BUS_ZERO_GATE_EXPECTED_ZERO_NOT_CHECKED")
    return {
        "ok": True, "code": "BUS_ZERO_GATE_RESIDENT_MATCH",
        "mapping_generation": mapping_generation, "checked": checked,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--ini", required=True)
    args = parser.parse_args()
    try:
        result = verify_bus_zero_resident(Path(args.runtime), Path(args.ini))
    except Exception as exc:
        print(str(exc))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
