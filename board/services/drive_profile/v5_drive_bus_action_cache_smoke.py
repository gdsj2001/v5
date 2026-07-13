#!/usr/bin/env python3
from __future__ import annotations

import json
import tempfile
from pathlib import Path

import v5_drive_bus_action as action
import v5_drive_axis_model as axis_model
import v5_drive_bus_contract as contract
import v5_drive_bus_context as context
import v5_drive_runtime_store as runtime_store


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
            json.dumps({"schema": contract.SETTINGS_RUNTIME_SCHEMA, "axes": [{"axis": "X"}, {"axis": "Y"}]}),
        )
        write_text(contract.RESIDENT_SNAPSHOT, json.dumps({"profiles": [{"profile_id": "smoke"}]}))
        write_text(
            contract.RUNTIME_SETTINGS_INI,
            "[TRAJ]\nCOORDINATES = X Y Z A C\n"
            "[AXIS_X]\nPITCH = 5\nMOTOR_REV = 1\nLOAD_REV = 1\nMIN_LIMIT = -500\nMAX_LIMIT = 500\n"
            "[JOINT_0]\nSCALE = 10000\nMIN_LIMIT = -500\nMAX_LIMIT = 500\n"
            "[AXIS_Y]\nPITCH = 5\nMOTOR_REV = 1\nLOAD_REV = 1\nMIN_LIMIT = -500\nMAX_LIMIT = 500\n"
            "[JOINT_1]\nSCALE = 10000\nMIN_LIMIT = -500\nMAX_LIMIT = 500\n"
            "[AXIS_A]\nPITCH = 360\nMOTOR_REV = 50\nLOAD_REV = 1\n"
            "[JOINT_3]\nSCALE = 1000\n"
            "[AXIS_C]\nPITCH = 360\nMOTOR_REV = 10\nLOAD_REV = 1\nMIN_LIMIT = -100\nMAX_LIMIT = 100\n"
            "[JOINT_4]\nSCALE = 1000\nMIN_LIMIT = -100\nMAX_LIMIT = 100\n",
        )
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
        if not context.resident_preload_active or action.load_settings_runtime().get("axes") != [{"axis": "X"}, {"axis": "Y"}]:
            print("successful preload did not leave guarded resident owner active")
            return 3
        sections = runtime_store.read_runtime_ini_sections(contract.RUNTIME_SETTINGS_INI)
        mappings = {
            axis: runtime_store.runtime_ini_joint_index(sections, axis, configured)[0]
            for axis, configured in (("X", 0), ("A", 3), ("C", 5))
        }
        if mappings != {"X": 0, "A": 3, "C": 4}:
            print("runtime INI axis to joint mapping is wrong", mappings)
            return 4
        if runtime_store.runtime_ini_joint_index(
                {"TRAJ": {"COORDINATES": "X Y Z A B C"}}, "C", 4) != (
                    5, "active_runtime_ini.TRAJ.COORDINATES"):
            print("runtime INI XYZABC mapping did not resolve C to JOINT_5")
            return 5
        try:
            runtime_store.runtime_ini_joint_index({}, "C", 5)
        except contract.DriveActionError as exc:
            if exc.code != "RUNTIME_INI_JOINT_MAPPING_INVALID":
                print("missing coordinates returned unexpected code", exc.code)
                return 6
        else:
            print("missing coordinates reused configured settings row index")
            return 7
        expected_egear = {"X": (16384, 3125), "A": (8192, 225), "C": (8192, 1125)}
        for axis, configured in (("X", 0), ("A", 3), ("C", 5)):
            numerator, denominator, evidence = axis_model.target_egear_from_runtime_ini(
                axis, configured, {"encoder_bits": 18}
            )
            if (numerator, denominator) != expected_egear[axis]:
                print("runtime INI egear formula is wrong", axis, numerator, denominator, evidence)
                return 8
            if evidence.get("axis_index") != mappings[axis]:
                print("runtime INI egear evidence used settings row index", axis, evidence)
                return 9
        counts_per_unit, scale_evidence = axis_model.derive_counts_per_unit("C", {}, 5)
        if counts_per_unit != 1000 or scale_evidence.get("joint_section") != "JOINT_4":
            print("C count-domain scale used settings row index", counts_per_unit, scale_evidence)
            return 10
        raw_limits = runtime_store.update_runtime_ini_raw_limits("C", 5, 0.0, 10.0)
        if raw_limits.get("joint_section") != "JOINT_4" or "JOINT_5" in raw_limits.get("updated_sections", []):
            print("C raw limit update used settings row index", raw_limits)
            return 11
        runtime = action.load_settings_runtime()
        stale_fork_runtime = json.loads(json.dumps(runtime))
        first_zero = runtime_store.persist_axis_zero_model(
            runtime,
            "X",
            0,
            25000.0,
            10000.0,
            {"source": "active_runtime_ini.SCALE", "counts_per_unit": 10000.0},
            {"read": {"value": 25000.0}, "profile_id": "smoke"},
        )
        first_limits = first_zero.get("raw_limit_save", {})
        if (first_zero.get("old_zero_source") != "runtime_ini_initial_origin" or
                first_zero.get("old_zero_counts") != 0.0 or
                abs(float(first_limits.get("raw_min_limit", 0.0)) - (-497.5)) > 1.0e-9 or
                abs(float(first_limits.get("raw_max_limit", 0.0)) - 502.5) > 1.0e-9):
            print("first axis-zero did not establish an initial count-domain origin", first_zero)
            return 12
        y_zero = runtime_store.persist_axis_zero_model(
            stale_fork_runtime,
            "Y",
            1,
            1000.0,
            10000.0,
            {"source": "active_runtime_ini.SCALE", "counts_per_unit": 10000.0},
            {"read": {"value": 1000.0}, "profile_id": "smoke"},
        )
        merged_runtime = json.loads(contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
        merged_x, _ = runtime_store.find_runtime_axis(merged_runtime, "X")
        merged_y, _ = runtime_store.find_runtime_axis(merged_runtime, "Y")
        if (runtime_store.saved_zero_counts(merged_x) != 25000.0 or
                runtime_store.saved_zero_counts(merged_y) != 1000.0 or
                y_zero.get("old_zero_source") != "runtime_ini_initial_origin"):
            print("stale fork axis-zero overwrote a prior axis", merged_runtime, y_zero)
            return 13
        second_zero = runtime_store.persist_axis_zero_model(
            runtime,
            "X",
            0,
            40000.0,
            10000.0,
            {"source": "active_runtime_ini.SCALE", "counts_per_unit": 10000.0},
            {"read": {"value": 40000.0}, "profile_id": "smoke"},
        )
        second_limits = second_zero.get("raw_limit_save", {})
        if (second_zero.get("old_zero_source") != "existing_zero_model" or
                second_zero.get("old_zero_counts") != 25000.0 or
                abs(float(second_limits.get("raw_min_limit", 0.0)) - (-496.0)) > 1.0e-9 or
                abs(float(second_limits.get("raw_max_limit", 0.0)) - 504.0) > 1.0e-9):
            print("subsequent axis-zero did not roll the existing zero model", second_zero)
            return 14
        final_runtime = json.loads(contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
        final_y, _ = runtime_store.find_runtime_axis(final_runtime, "Y")
        if runtime_store.saved_zero_counts(final_y) != 1000.0:
            print("same-axis update overwrote the other axis", final_runtime)
            return 15
        contract.SETTINGS_RUNTIME_JSON.unlink()
        failed = action.preload_resident_state()
        if failed.get("ok") or context.resident_preload_active:
            print("failed preload left resident latch active", failed)
            return 16
        try:
            action.load_settings_runtime()
        except contract.DriveActionError as exc:
            if exc.code != "SETTINGS_RUNTIME_RESIDENT_NOT_PRELOADED":
                print("failed preload returned unexpected guarded-owner code", exc.code)
                return 17
        else:
            print("failed preload did not keep guarded owner fail-closed")
            return 18
    print("v5 drive bus action cache smoke: preload lifecycle and cache invalidation ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
