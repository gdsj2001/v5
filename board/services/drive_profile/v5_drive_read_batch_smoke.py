from __future__ import annotations

from typing import Any, Dict, List, Tuple

import v5_drive_query as query
import v5_drive_result as drive_result
from v5_drive_bus_contract import OPTIONAL_READ_COMMANDS, REQUIRED_READ_COMMANDS


POSITIONS = ("0", "4")


def targets() -> List[Dict[str, Any]]:
    commands = {
        name: {"supported": True}
        for name in (*REQUIRED_READ_COMMANDS, *OPTIONAL_READ_COMMANDS)
    }
    return [
        {
            "axis": "X" if position == "0" else "C",
            "position": position,
            "commands": commands,
            "identity": {"vendor_id": 1, "product_code": int(position) + 1},
            "profile": {"profile_id": "profile-" + position},
        }
        for position in POSITIONS
    ]


def run_case(initial_states: Dict[str, str], missing_position: str = "",
             actual_fault_position: str = "") -> Tuple[Dict[str, Any], List[Tuple[Any, ...]]]:
    events: List[Tuple[Any, ...]] = []
    read_stage = 0
    target_set = targets()
    scan = {"ok": True, "profile_snapshot": {
        "generated_at": "frozen-snapshot", "profile_count": 2}, "slaves": [
        {"position": position, "state": state}
        for position, state in initial_states.items()
    ]}
    originals = {
        name: getattr(query, name)
        for name in (
            "configured_drive_targets", "read_command",
            "recover_full_target_set_mailbox", "run_ethercat_slaves",
            "evaluate_drive_health", "drive_display_update_from_health",
            "write_drive_parameter_display_rows",
        )
    }

    query.configured_drive_targets = lambda _timeout: (
        target_set, {"axes": []}, scan)

    def read(position: str, name: str, _command: Dict[str, Any], required: bool) -> Dict[str, Any]:
        events.append(("read", read_stage, name, position))
        missing = read_stage == 0 and position == missing_position and name == REQUIRED_READ_COMMANDS[0]
        return {
            "ok": not missing,
            "required": required,
            "code": "MISSING" if missing else "OK",
            "fault": position == actual_fault_position,
        }

    def recover(failed_targets: List[Dict[str, Any]], _timeout: float) -> Dict[str, Any]:
        nonlocal read_stage
        positions = tuple(str(target["position"]) for target in failed_targets)
        events.append(("recover", positions))
        read_stage = 1
        return {"ok": True, "operations": []}

    def fresh_scan(_timeout: float) -> Dict[str, Any]:
        events.append(("scan",))
        return {"ok": True, "slaves": [
            {"position": position, "state": "OP"} for position in POSITIONS]}

    def evaluate(reads: Dict[str, Any], *_args: Any, **_kwargs: Any) -> Dict[str, Any]:
        fault = any(bool(item.get("fault")) for item in reads.values())
        return {
            "ok": not fault,
            "failures": [{"code": "DRIVE_ERROR_CODE_NONZERO"}] if fault else [],
            "statusword": 0x1698 if fault else 0x1230,
            "error_code": 0x0E08 if fault else 0,
        }

    query.read_command = read
    query.recover_full_target_set_mailbox = recover
    query.run_ethercat_slaves = fresh_scan
    query.evaluate_drive_health = evaluate
    query.drive_display_update_from_health = (
        lambda axis, _health, status, position: {
            "axis": axis, "position": position, "write_status": status})
    query.write_drive_parameter_display_rows = lambda _updates: {"ok": True}
    try:
        result = query.read_drive(1.0)
    finally:
        for name, value in originals.items():
            setattr(query, name, value)
    return result, events


def expected_stage(stage: int) -> List[Tuple[Any, ...]]:
    return [
        ("read", stage, command_name, position)
        for command_name in (*REQUIRED_READ_COMMANDS, *OPTIONAL_READ_COMMANDS)
        for position in POSITIONS
    ]


nominal, nominal_events = run_case({"0": "OP", "4": "OP"})
assert nominal["ok"] is True
assert nominal["code"] == "DRIVE_READ_OK"
assert nominal["snapshot_generated_at"] == "frozen-snapshot"
assert nominal["snapshot_profile_count"] == 2
assert nominal_events == expected_stage(0)

subset, subset_events = run_case(
    {"0": "OP", "4": "PREOP"}, missing_position="4")
assert subset["ok"] is True
assert subset["recovery_positions"] == ["4"]
assert subset_events == (
    expected_stage(0) + [("recover", ("0", "4")), ("scan",)] + expected_stage(1))

actual_fault, fault_events = run_case(
    {"0": "OP", "4": "OP"}, actual_fault_position="4")
assert actual_fault["ok"] is True
assert actual_fault["code"] == "DRIVE_READ_OK"
assert not any(event[0] == "recover" for event in fault_events)

compacted = drive_result.compact_action_result_payload({
    **subset,
    "schema": "v5.drive_bus_action.v1",
    "action": "drive_parameter_read",
    "oversize_padding": "x" * 70000,
})
assert compacted["recovery_positions"] == ["4"]
assert compacted["targets"][0]["reads"]
assert compacted["targets"][0]["health"]["statusword"] == 0x1230

print("drive read coordinated batch smoke ok")
