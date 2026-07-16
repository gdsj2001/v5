#!/usr/bin/env python3
from __future__ import annotations

from typing import Any, Dict, List

import v5_drive_bus_action as action
import v5_drive_bus_context as context
import v5_drive_bus_contract as contract
import v5_drive_parameter_table as parameter_table
import v5_drive_query as query
import v5_drive_runtime_store as runtime_store
import v5_drive_sdo as sdo
from v5_drive_bus_contract import AXIS_ORDER


SCAN = {
    "ok": True,
    "code": "DRIVE_SCAN_OK",
    "slaves": [
        {"position": "0", "state": "OP", "name": "drive-0"},
        {"position": "1", "state": "OP", "name": "drive-1"},
        {"position": "2", "state": "OP", "name": "drive-2"},
        {"position": "3", "state": "OP", "name": "drive-3"},
        {"position": "4", "state": "OP", "name": "drive-4"},
        {"position": "5", "state": "OP", "name": "io-5"},
        {"position": "6", "state": "OP", "name": "toolmag-6"},
    ],
}
RUNTIME = {
    "axes": [
        {"axis": "X", "slave_index": "0", "drive_profile_id": "sv630n"},
        {"axis": "Y", "slave_index": "1", "drive_profile_id": "sv630n"},
        {"axis": "Z", "slave_index": "2", "drive_profile_id": "sv630n"},
        {"axis": "A", "slave_index": "3", "drive_profile_id": "sv630n"},
        {"axis": "C", "slave_index": "4", "drive_profile_id": "sv630n"},
    ]
}
BC_RUNTIME = {
    "axes": [
        {"axis": "X", "slave_index": "0", "drive_profile_id": "sv630n"},
        {"axis": "Y", "slave_index": "1", "drive_profile_id": "sv630n"},
        {"axis": "Z", "slave_index": "2", "drive_profile_id": "sv630n"},
        {"axis": "B", "slave_index": "3", "drive_profile_id": "sv630n"},
        {"axis": "C", "slave_index": "4", "drive_profile_id": "sv630n"},
    ]
}
PROFILE = {
    "profile_id": "sv630n",
    "commands": {
        "drive.set_egear": {"supported": True, "requires_save_parameters": False},
        "drive.write_mode": {"supported": True, "requires_save_parameters": False},
    },
}
AC_SECTIONS = {
    "RTCP": {"MODEL": "XYZAC_TRT", "KINS_COORDINATES": "XYZAC"},
    "TRAJ": {"COORDINATES": "X Y Z A C"},
}
BC_SECTIONS = {
    "RTCP": {"MODEL": "XYZBC_TRT", "KINS_COORDINATES": "XYZBC"},
    "TRAJ": {"COORDINATES": "X Y Z B C"},
}


def complete_bindings(**overrides: str) -> Dict[str, str]:
    bindings = {axis: "NAT" for axis in AXIS_ORDER}
    bindings.update(overrides)
    return bindings


def full_scan_and_candidate_smoke() -> None:
    calls: List[List[str]] = []
    original_run_command = sdo.run_command
    original_read_table = parameter_table.read_parameter_tsv
    original_write_table = parameter_table.write_parameter_tsv
    written_rows: List[tuple[str, str, str]] = []
    existing_rows = [
        ("X", "slave", "2"),
        ("Y", "slave", "1"),
        ("Z", "slave", "0"),
        ("A", "slave", "4"),
        ("B", "slave", "NAT"),
        ("C", "slave", "3"),
        ("SETTINGS", "slave_options", "99:stale"),
    ]
    try:
        def fake_run_command(argv: List[str], _timeout: float) -> Dict[str, Any]:
            calls.append(list(argv))
            return {
                "ok": True,
                "code": "OK",
                "returncode": 0,
                "stdout": "\n".join(
                    "%d  0:%d  OP  +  drive-%d" % (position, position, position)
                    for position in (4, 1, 6, 0, 5, 3, 2)
                ),
                "stderr": "",
            }

        sdo.run_command = fake_run_command
        scan = sdo.run_ethercat_slaves(1.0)
        if calls != [["ethercat", "slaves"]] or not scan.get("ok"):
            raise AssertionError((calls, scan))
        if [str(item.get("position")) for item in scan.get("slaves", [])] != ["4", "1", "6", "0", "5", "3", "2"]:
            raise AssertionError(scan)
        parameter_table.read_parameter_tsv = lambda _path: list(existing_rows)
        parameter_table.write_parameter_tsv = lambda _path, rows: written_rows.extend(rows)
        parameter_table.write_scan_self_parameter_table(scan)
    finally:
        sdo.run_command = original_run_command
        parameter_table.read_parameter_tsv = original_read_table
        parameter_table.write_parameter_tsv = original_write_table
    persisted = [row for row in written_rows if row[:2] != ("SETTINGS", "slave_options")]
    if persisted != existing_rows[:-1]:
        raise AssertionError((persisted, existing_rows))
    option_rows = [row for row in written_rows if row[:2] == ("SETTINGS", "slave_options")]
    if len(option_rows) != 1 or {token.split(":", 1)[0] for token in option_rows[0][2].split(",")} != {str(i) for i in range(7)}:
        raise AssertionError(option_rows)


def set_query_inputs(bindings: Dict[str, str], runtime: Dict[str, Any] = RUNTIME,
                     sections: Dict[str, Dict[str, str]] = AC_SECTIONS) -> None:
    context.resident_preload_active = True
    context.self_slave_binding_cache = dict(bindings)
    context.runtime_ini_sections_cache[str(contract.RUNTIME_SETTINGS_INI)] = sections
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


def expect_active_model_error(sections: Dict[str, Dict[str, str]], expected_code: str) -> None:
    try:
        runtime_store.active_model_axes_from_sections(sections)
    except contract.DriveActionError as exc:
        if exc.code != expected_code:
            raise AssertionError((expected_code, exc.code, exc.detail))
    else:
        raise AssertionError((expected_code, sections))


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
    sections: Dict[str, Dict[str, str]] = AC_SECTIONS,
) -> None:
    set_query_inputs(bindings, runtime, sections)
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
    full_scan_and_candidate_smoke()
    if runtime_store.active_model_axes_from_sections(AC_SECTIONS) != tuple("XYZAC"):
        raise AssertionError(AC_SECTIONS)
    if runtime_store.active_model_axes_from_sections(BC_SECTIONS) != tuple("XYZBC"):
        raise AssertionError(BC_SECTIONS)
    unregistered_sections = {
        "RTCP": {"MODEL": "TABLE_TRT_V3", "KINS_COORDINATES": "XYZABC"},
        "TRAJ": {"COORDINATES": "X Y Z A B C"},
    }
    expect_active_model_error(unregistered_sections, "ACTIVE_MODEL_UNSUPPORTED")
    expect_active_model_error(
        {"RTCP": {"KINS_COORDINATES": "XYZAC"}, "TRAJ": {"COORDINATES": "X Y Z A C"}},
        "ACTIVE_MODEL_MISSING",
    )
    expect_active_model_error(
        {"RTCP": {"MODEL": "XYZAC_TRT"}, "TRAJ": {"COORDINATES": "X Y Z A C"}},
        "ACTIVE_MODEL_COORDINATES_MISSING",
    )
    expect_active_model_error(
        {"RTCP": {"MODEL": "XYZAC_TRT", "KINS_COORDINATES": "XYZAC"},
         "TRAJ": {"COORDINATES": "X Y Z A A C"}},
        "ACTIVE_MODEL_COORDINATES_DUPLICATE",
    )
    expect_active_model_error(
        {"RTCP": {"MODEL": "XYZAC_TRT", "KINS_COORDINATES": "XYZBC"},
         "TRAJ": {"COORDINATES": "X Y Z A C"}},
        "ACTIVE_MODEL_COORDINATES_MISMATCH",
    )
    expect_active_model_error(
        {"RTCP": {"MODEL": "XYZAC_TRT", "KINS_COORDINATES": "XYZBC"},
         "TRAJ": {"COORDINATES": "X Y Z B C"}},
        "ACTIVE_MODEL_DESCRIPTOR_MISMATCH",
    )

    swapped = complete_bindings(X="2", Y="1", Z="0", A="4", C="3")
    set_query_inputs(swapped)
    targets, _runtime, _scan = query.configured_drive_targets(1.0)
    target_positions = {str(target["axis"]): str(target["position"]) for target in targets}
    if target_positions != {"X": "2", "Y": "1", "Z": "0", "A": "4", "C": "3"}:
        raise AssertionError(target_positions)
    if any(target.get("axis_slave_binding_source") != "resident_self_parameter_table" for target in targets):
        raise AssertionError(targets)
    if _scan.get("active_model_axes") != list("XYZAC"):
        raise AssertionError(_scan)
    if len(_scan.get("slaves", [])) != 7:
        raise AssertionError(_scan)

    ignored_aux = complete_bindings(
        X="2", Y="1", Z="0", A="4", C="3", GANTRY="5", TOOLMAG="6")
    set_query_inputs(ignored_aux)
    aux_targets, _runtime, _scan = query.configured_drive_targets(1.0)
    if {str(target["axis"]): str(target["position"]) for target in aux_targets} != target_positions:
        raise AssertionError(aux_targets)

    result, writes = run_set_drive_with(targets)
    if not result.get("ok") or writes != [
            ("2", "drive.set_egear"),
            ("1", "drive.set_egear"),
            ("0", "drive.set_egear"),
            ("4", "drive.set_egear"),
            ("3", "drive.set_egear")]:
        raise AssertionError((result, writes))

    context.resident_preload_active = True
    context.self_slave_binding_cache = swapped
    if parameter_table.resident_axis_by_slave_position() != {
            "2": "X", "1": "Y", "0": "Z", "4": "A", "3": "C"}:
        raise AssertionError(parameter_table.resident_axis_by_slave_position())

    missing_x = dict(swapped)
    del missing_x["X"]
    expect_precheck_failure(missing_x, "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_SELF_SLAVE_MISSING")
    missing_b = dict(swapped)
    del missing_b["B"]
    expect_precheck_failure(missing_b, "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_INACTIVE_AXIS_BINDING_MISSING")
    duplicate_active = dict(swapped)
    duplicate_active["Y"] = "2"
    expect_precheck_failure(duplicate_active, "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_DUPLICATE_SLAVE")
    duplicate_aux = dict(swapped)
    duplicate_aux["GANTRY"] = "2"
    expect_precheck_failure(duplicate_aux, "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_DUPLICATE_SLAVE")
    inactive_bound = dict(swapped)
    inactive_bound["B"] = "6"
    expect_precheck_failure(inactive_bound, "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_INACTIVE_AXIS_BOUND")
    active_nat = dict(swapped)
    active_nat["C"] = "NAT"
    expect_precheck_failure(active_nat, "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_ACTIVE_AXIS_NAT")
    bc_bindings = complete_bindings(X="2", Y="1", Z="0", B="4", C="3")
    bc_inactive_bound = dict(bc_bindings)
    bc_inactive_bound["A"] = "6"
    expect_precheck_failure(
        bc_inactive_bound,
        "DRIVE_TARGET_PRECHECK_FAILED",
        "DRIVE_TARGET_INACTIVE_AXIS_BOUND",
        runtime=BC_RUNTIME,
        sections=BC_SECTIONS,
    )
    expect_precheck_failure(complete_bindings(), "DRIVE_TARGET_PRECHECK_FAILED", "DRIVE_TARGET_ACTIVE_AXIS_NAT")
    runtime_missing_z = {"axes": [item for item in RUNTIME["axes"] if item["axis"] != "Z"]}
    expect_precheck_failure(
        swapped,
        "DRIVE_TARGET_PRECHECK_FAILED",
        "DRIVE_TARGET_RUNTIME_AXIS_MISSING",
        runtime_missing_z,
    )

    print("v5 drive resident mapping smoke: full scan, explicit mapping, and fail-closed zero-SDO cases ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
