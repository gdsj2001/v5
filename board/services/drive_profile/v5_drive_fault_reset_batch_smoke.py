from __future__ import annotations

from typing import Any, Dict, List, Tuple

import v5_drive_health as health


TARGETS = [
    {"axis": "X", "position": "0", "commands": {"drive.reset_fault": {"supported": True}}},
    {"axis": "C", "position": "4", "commands": {"drive.reset_fault": {"supported": True}}},
]
EXPECTATIONS = {"0": ((16384, 3125), 8), "4": ((16384, 1125), 8)}
REQUIRED = (
    "drive.read_statusword", "drive.read_error_code", "drive.read_mode",
    "drive.read_actual_position", "drive.read_egear",
)


def readback(position: str, kind: str) -> Dict[str, Any]:
    egear, mode = EXPECTATIONS[position]
    reads = {name: {"ok": True, "required": True, "code": "OK"} for name in REQUIRED}
    failures: List[Dict[str, Any]] = []
    statusword, error_code = 0x1698, 0x0E08
    if kind == "healthy":
        statusword, error_code = 0x1230, 0
    elif kind == "missing":
        reads["drive.read_statusword"] = {
            "ok": False, "required": True, "code": "DRIVE_SDO_UPLOAD_FAILED"}
        statusword, error_code = 0, 0
        failures.append({"field": "statusword", "code": "DRIVE_STATUSWORD_READ_FAILED"})
    else:
        failures.extend((
            {"field": "statusword", "code": "DRIVE_STATUSWORD_FAULT_BIT_SET", "value": statusword},
            {"field": "error_code", "code": "DRIVE_ERROR_CODE_NONZERO", "value": error_code},
        ))
    drive_health = {
        "ok": not failures,
        "failures": failures,
        "statusword": statusword,
        "error_code": error_code,
        "aux_error_code": 0,
        "mode_of_operation": mode,
        "actual_position_counts": 100,
        "egear_numerator": egear[0],
        "egear_denominator": egear[1],
    }
    return {
        "ok": not failures,
        "attempts": [{"attempt": 1, "read_ok": kind != "missing", "health": drive_health, "reads": reads}],
        "health": drive_health,
    }


def run_case(scans: List[Dict[str, str]], sequences: Dict[str, List[str]]) -> Tuple[Dict[str, Any], List[Tuple[Any, ...]]]:
    events: List[Tuple[Any, ...]] = []
    scan_index = 0
    read_indexes = {position: 0 for position in sequences}
    originals = (
        health.run_ethercat_slaves, health.run_command, health.readback_once,
        health.write_command, health.time.sleep,
    )

    def fake_scan(_timeout: float) -> Dict[str, Any]:
        nonlocal scan_index
        states = scans[min(scan_index, len(scans) - 1)]
        scan_index += 1
        events.append(("scan",))
        return {"ok": True, "code": "DRIVE_SCAN_OK", "slaves": [
            {"position": position, "state": state} for position, state in states.items()]}

    def fake_read(target: Dict[str, Any], _timeout: float,
                  expected_egear=None, expected_mode=None, **_kwargs: Any) -> Dict[str, Any]:
        position = str(target["position"])
        index = min(read_indexes[position], len(sequences[position]) - 1)
        kind = sequences[position][index]
        read_indexes[position] += 1
        events.append(("read", position, kind))
        if index > 0 and kind == "healthy":
            assert (expected_egear, expected_mode) == EXPECTATIONS[position]
        return readback(position, kind)

    def fake_write(position: str, command_name: str, _command: Dict[str, Any]) -> Dict[str, Any]:
        events.append(("write", position, command_name))
        return {"ok": True, "code": "DRIVE_SDO_WRITE_OK"}

    def fake_command(argv: List[str], _timeout: float) -> Dict[str, Any]:
        assert argv[:2] == ["ethercat", "states"]
        events.append(("state_batch", argv[2]))
        return {"ok": True, "code": "OK", "returncode": 0}

    try:
        health.run_ethercat_slaves = fake_scan
        health.readback_once = fake_read
        health.write_command = fake_write
        health.run_command = fake_command
        health.time.sleep = lambda *_args, **_kwargs: None
        result = health.fault_reset_batch(TARGETS, 1.0)
    finally:
        (health.run_ethercat_slaves, health.run_command, health.readback_once,
         health.write_command, health.time.sleep) = originals
    return result, events


def test_nominal_writes_whole_batch_before_global_readback() -> None:
    result, events = run_case(
        [{"0": "OP", "4": "OP"}, {"0": "OP", "4": "OP"}],
        {"0": ["fault", "healthy"], "4": ["fault", "healthy"]})
    assert result["ok"] is True
    assert events == [
        ("scan",), ("read", "0", "fault"), ("read", "4", "fault"),
        ("write", "0", "drive.reset_fault"), ("write", "4", "drive.reset_fault"),
        ("scan",), ("state_batch", "OP"),
        ("scan",),
        ("scan",), ("read", "0", "healthy"), ("read", "4", "healthy"),
    ]


def test_persistent_actual_fault_never_triggers_state_recovery() -> None:
    result, events = run_case(
        [{"0": "OP", "4": "OP"}, {"0": "OP", "4": "OP"}],
        {"0": ["fault", "fault"], "4": ["fault", "fault"]})
    assert result["ok"] is False
    assert result["failed_positions"] == ["0", "4"]
    assert [event for event in events if event[0] == "state_batch"] == [
        ("state_batch", "OP")]


def test_fault_held_preop_safeop_still_resets_before_single_op_restore() -> None:
    result, events = run_case(
        [{"0": "PREOP", "4": "SAFEOP"}, {"0": "OP", "4": "OP"}],
        {"0": ["fault", "healthy"], "4": ["fault", "healthy"]})
    assert result["ok"] is True
    assert events == [
        ("scan",), ("read", "0", "fault"), ("read", "4", "fault"),
        ("write", "0", "drive.reset_fault"), ("write", "4", "drive.reset_fault"),
        ("scan",), ("state_batch", "OP"),
        ("scan",),
        ("scan",), ("read", "0", "healthy"), ("read", "4", "healthy"),
    ]


def test_preflight_recovers_only_unavailable_target_once() -> None:
    result, events = run_case(
        [
            {"0": "OP", "4": "INIT"},
            {"0": "OP", "4": "PREOP"},
            {"0": "PREOP", "4": "PREOP"},
            {"0": "PREOP", "4": "PREOP"},
            {"0": "PREOP", "4": "PREOP"},
            {"0": "OP", "4": "OP"},
            {"0": "OP", "4": "OP"},
        ],
        {
            "0": ["fault", "fault", "healthy"],
            "4": ["missing", "fault", "healthy"],
        })
    assert result["ok"] is True
    assert result["recovery_positions"] == ["4"]
    assert [event for event in events if event[0] == "state_batch"] == [
        ("state_batch", "PREOP"), ("state_batch", "OP")]
    first_write = next(index for index, event in enumerate(events) if event[0] == "write")
    assert all(event[0] != "write" for event in events[:first_write])


def test_postwrite_mailbox_failure_does_not_loop_or_rewrite() -> None:
    result, events = run_case(
        [
            {"0": "OP", "4": "OP"},
            {"0": "OP", "4": "OP"},
        ],
        {
            "0": ["fault", "healthy"],
            "4": ["fault", "missing"],
        })
    assert result["ok"] is False
    assert [event for event in events if event[0] == "state_batch"] == [
        ("state_batch", "OP")]
    assert [event for event in events if event[0] == "write"] == [
        ("write", "0", "drive.reset_fault"),
        ("write", "4", "drive.reset_fault"),
    ]


def test_target_set_mismatch_fails_without_per_axis_fallback() -> None:
    commands: List[List[str]] = []
    original_scan = health.run_ethercat_slaves
    original_command = health.run_command
    try:
        health.run_ethercat_slaves = lambda _timeout: {
            "ok": True,
            "slaves": [
                {"position": "0", "state": "OP"},
                {"position": "2", "state": "OP"},
                {"position": "4", "state": "OP"},
            ],
        }
        health.run_command = lambda argv, _timeout: (
            commands.append(list(argv)) or {"ok": True, "code": "OK"})
        result = health.request_full_target_set_state(TARGETS, "OP", 1.0)
    finally:
        health.run_ethercat_slaves = original_scan
        health.run_command = original_command
    assert result["ok"] is False
    assert result["code"] == "DRIVE_STATE_TARGET_SET_MISMATCH"
    assert commands == []


def test_target_set_waits_for_all_actual_states() -> None:
    commands: List[List[str]] = []
    scans = iter([
        {
            "ok": True,
            "slaves": [
                {"position": "0", "state": "OP"},
                {"position": "4", "state": "OP"},
            ],
        },
        {
            "ok": True,
            "slaves": [
                {"position": "0", "state": "SAFEOP"},
                {"position": "4", "state": "PREOP"},
            ],
        },
        {
            "ok": True,
            "slaves": [
                {"position": "0", "state": "OP"},
                {"position": "4", "state": "OP"},
            ],
        },
    ])
    original_scan = health.run_ethercat_slaves
    original_command = health.run_command
    original_sleep = health.time.sleep
    try:
        health.run_ethercat_slaves = lambda _timeout: next(scans)
        health.run_command = lambda argv, _timeout: (
            commands.append(list(argv)) or {"ok": True, "code": "OK"})
        health.time.sleep = lambda _delay: None
        result = health.request_full_target_set_state(TARGETS, "op", 1.0)
    finally:
        health.run_ethercat_slaves = original_scan
        health.run_command = original_command
        health.time.sleep = original_sleep
    assert result["ok"] is True
    assert result["code"] == "DRIVE_STATE_TARGET_SET_OK"
    assert result["requested_state"] == "OP"
    assert result["state_readback_ok"] is True
    assert result["poll_count"] == 2
    assert result["actual_states"] == {"0": "OP", "4": "OP"}
    assert result["failed_positions"] == []
    assert commands == [["ethercat", "states", "OP"]]


test_nominal_writes_whole_batch_before_global_readback()
test_persistent_actual_fault_never_triggers_state_recovery()
test_fault_held_preop_safeop_still_resets_before_single_op_restore()
test_preflight_recovers_only_unavailable_target_once()
test_postwrite_mailbox_failure_does_not_loop_or_rewrite()
test_target_set_mismatch_fails_without_per_axis_fallback()
test_target_set_waits_for_all_actual_states()
print("drive fault reset coordinated batch smoke ok")
