#!/usr/bin/env python3
from __future__ import annotations

from typing import Any, Dict, List

import v5_drive_bus_action as action
import v5_drive_bus_context as context
import v5_drive_parameter_table as parameter_table
import v5_drive_query as query
from v5_drive_bus_contract import AXIS_ORDER


SCAN = {
    "ok": True,
    "code": "DRIVE_SCAN_OK",
    "slaves": [
        {"position": "0", "state": "OP", "name": "drive-0"},
        {"position": "1", "state": "OP", "name": "drive-1"},
        {"position": "2", "state": "OP", "name": "drive-2"},
    ],
}
RUNTIME = {
    "axes": [
        {"axis": "X", "slave_index": "0", "drive_profile_id": "sv630n"},
        {"axis": "Y", "slave_index": "1", "drive_profile_id": "sv630n"},
        {"axis": "Z", "slave_index": "2", "drive_profile_id": "sv630n"},
    ]
}
PROFILE = {
    "profile_id": "sv630n",
    "commands": {
        "drive.set_egear": {"supported": True, "requires_save_parameters": False},
        "drive.write_mode": {"supported": True, "requires_save_parameters": False},
    },
}


def complete_bindings(**overrides: str) -> Dict[str, str]:
    bindings = {axis: "NAT" for axis in AXIS_ORDER}
    bindings.update(overrides)
    return bindings


def set_query_inputs(bindings: Dict[str, str], runtime: Dict[str, Any] = RUNTIME) -> None:
    query.run_ethercat_slaves = lambda _timeout: SCAN
    query.load_settings_runtime = lambda: runtime
    query.load_self_slave_bindings = lambda: dict(bindings)
    query.read_resident_snapshot = lambda: {"profiles": [PROFILE]}
    query.parse_slave_identity = lambda position, _timeout: {
        "identity_ok": True,
        "position": str(position),
        "vendor_id": "0x1",
        "product_code": "0x2",
    }
    query.select_profile = lambda _identity, _snapshot: PROFILE


def run_set_drive_with(targets: List[Dict[str, Any]]) -> tuple[Dict[str, Any], List[tuple[str, str]]]:
    writes: List[tuple[str, str]] = []
    action.v5_drive_enable_window.begin = lambda run_id, _timeout: {
        "ok": True, "run_id": run_id,
        "initial_machine_enabled": False, "final_machine_enabled": False,
    }
    action.v5_drive_enable_window.finish_safely = lambda run_id, _timeout, restore=False: {
        "ok": True, "run_id": run_id, "code": "DRIVE_WRITE_WINDOW_FINISH_KEEP_OFF",
        "initial_machine_enabled": False, "final_machine_enabled": False,
    }
    action.configured_drive_targets = lambda _timeout: (targets, RUNTIME, SCAN)
    action.precheck_targets_for_write = lambda *_args, **_kwargs: {"ok": True}
    action.target_egear = lambda _target: (100, 1, {"source": "smoke"})
    action.read_required_state = lambda *_args: {"ok": True, "reads": {"drive.read_mode": {"upload": {"value": 8}}}}

    def fake_write(position: str, command_name: str, _command: Dict[str, Any], _values: Any = None) -> Dict[str, Any]:
        writes.append((str(position), command_name))
        return {"ok": True}

    action.write_command = fake_write
    action.set_drive_batch_readback = lambda current_targets, _timeout, _expectations: {
        "failed_positions": [],
        "cycles": [{"recovery_positions": []}],
        "readbacks": {
            str(target["position"]): {
                "ok": True,
                "health": {"ok": True, "egear_numerator": 100, "egear_denominator": 1},
            }
            for target in current_targets
        },
    }
    action.update_axis_drive_set_evidence = lambda *_args: None
    action.persist_settings_runtime = lambda _runtime: {"ok": True}
    action.write_drive_parameter_display_rows = lambda _updates: {"ok": True}
    return action.run_set_drive(1.0), writes


def expect_precheck_failure(
    bindings: Dict[str, str],
    expected_code: str,
    expected_direct_code: str = "",
    runtime: Dict[str, Any] = RUNTIME,
) -> None:
    set_query_inputs(bindings, runtime)
    writes: List[tuple[str, str]] = []
    action.configured_drive_targets = query.configured_drive_targets
    action.write_command = lambda position, name, *_args: writes.append((str(position), name)) or {"ok": True}
    result = action.run_set_drive(1.0)
    if result.get("code") != expected_code or result.get("write_executed") or writes:
        raise AssertionError((expected_code, result, writes))
    if expected_direct_code:
        failures = result.get("detail", {}).get("failures", []) if isinstance(result.get("detail"), dict) else []
        if expected_direct_code not in {str(item.get("code")) for item in failures if isinstance(item, dict)}:
            raise AssertionError((expected_direct_code, result))


def main() -> int:
    swapped = complete_bindings(X="2", Z="0")
    set_query_inputs(swapped)
    targets, _runtime, _scan = query.configured_drive_targets(1.0)
    target_positions = {str(target["axis"]): str(target["position"]) for target in targets}
    if target_positions != {"X": "2", "Z": "0"}:
        raise AssertionError(target_positions)
    if any(target.get("axis_slave_binding_source") != "resident_self_parameter_table" for target in targets):
        raise AssertionError(targets)

    result, writes = run_set_drive_with(targets)
    if not result.get("ok") or writes != [("2", "drive.set_egear"), ("0", "drive.set_egear")]:
        raise AssertionError((result, writes))

    context.resident_preload_active = True
    context.self_slave_binding_cache = swapped
    if parameter_table.resident_axis_by_slave_position() != {"2": "X", "0": "Z"}:
        raise AssertionError(parameter_table.resident_axis_by_slave_position())

    missing_x = complete_bindings(Z="0")
    del missing_x["X"]
    expect_precheck_failure(missing_x, "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_SELF_SLAVE_MISSING")
    expect_precheck_failure(complete_bindings(X="2", Z="2"), "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_DUPLICATE_SLAVE")
    expect_precheck_failure(complete_bindings(), "DRIVE_TARGETS_EMPTY")
    runtime_missing_z = {"axes": list(RUNTIME["axes"][:2])}
    expect_precheck_failure(
        swapped,
        "DRIVE_TARGET_PRECHECK_FAILED",
        "DRIVE_TARGET_RUNTIME_AXIS_MISSING",
        runtime_missing_z,
    )

    print("v5 drive resident mapping smoke: swapped mapping and fail-closed zero-SDO cases ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
