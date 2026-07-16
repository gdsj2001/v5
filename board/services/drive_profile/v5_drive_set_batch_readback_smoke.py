from __future__ import annotations

from typing import Any, Dict, List, Tuple

import v5_drive_bus_action as bus
import v5_drive_health as health
import v5_drive_result as drive_result
from v5_drive_bus_contract import DriveActionError


TARGETS = [{"position": "0"}, {"position": "1"}]
EXPECTATIONS = {"0": ((16384, 3125), 8), "1": ((16384, 3125), 8)}


def healthy_readback(position: str) -> Dict[str, Any]:
    egear, mode = EXPECTATIONS[position]
    health_result = {
        "ok": True,
        "failures": [],
        "statusword": 5840,
        "error_code": 0,
        "mode_of_operation": mode,
        "egear_numerator": egear[0],
        "egear_denominator": egear[1],
    }
    reads = {command: {"ok": True, "required": True, "code": "OK"} for command in (
        "drive.read_statusword", "drive.read_error_code", "drive.read_mode", "drive.read_actual_position", "drive.read_egear")}
    return {"ok": True, "attempts": [{"attempt": 1, "read_ok": True, "health": health_result, "reads": reads}], "health": health_result}


def mismatch_readback(position: str) -> Dict[str, Any]:
    result = healthy_readback(position)
    expected = EXPECTATIONS[position][1]
    failure = {"field": "mode", "code": "DRIVE_MODE_READBACK_MISMATCH", "expected": expected, "actual": 7}
    result["ok"] = False
    result["health"] = dict(result["health"], ok=False, failures=[failure], mode_of_operation=7)
    result["attempts"][0]["health"] = result["health"]
    return result


def missing_required_readback(position: str) -> Dict[str, Any]:
    result = mismatch_readback(position)
    result["attempts"][0]["read_ok"] = False
    result["attempts"][0]["reads"] = {
        "drive.read_statusword": {"ok": False, "required": True, "code": "DRIVE_SDO_UPLOAD_FAILED"},
        "drive.read_error_code": {"ok": True, "required": True, "code": "OK"},
        "drive.read_mode": {"ok": True, "required": True, "code": "OK"},
        "drive.read_actual_position": {"ok": True, "required": True, "code": "OK"},
        "drive.read_egear": {"ok": True, "required": True, "code": "OK"},
    }
    return result


def run_batch(scans: List[Dict[str, str]], readback_sequences: Dict[str, List[Dict[str, Any]]]) -> Tuple[Dict[str, Any], List[Tuple[str, ...]]]:
    events: List[Tuple[str, ...]] = []
    scan_index = 0
    read_indexes = {position: 0 for position in readback_sequences}
    originals = (health.run_ethercat_slaves, health.run_command, health.readback_once, health.time.sleep)

    def fake_scan(_timeout: float) -> Dict[str, Any]:
        nonlocal scan_index
        states = scans[min(scan_index, len(scans) - 1)]
        scan_index += 1
        events.append(("scan",))
        return {"ok": True, "code": "DRIVE_SCAN_OK", "slaves": [{"position": position, "state": state} for position, state in states.items()]}

    def fake_run(argv: List[str], _timeout: float) -> Dict[str, Any]:
        raise AssertionError(f"set-drive readback must not switch EtherCAT AL state: {argv!r}")

    def fake_readback(target: Dict[str, Any], _timeout: float, expected_egear=None, expected_mode=None, **_kwargs: Any) -> Dict[str, Any]:
        position = str(target["position"])
        events.append(("read", position))
        assert (expected_egear, expected_mode) == EXPECTATIONS[position]
        sequence = readback_sequences[position]
        index = min(read_indexes[position], len(sequence) - 1)
        read_indexes[position] += 1
        return sequence[index]

    try:
        health.run_ethercat_slaves = fake_scan
        health.run_command = fake_run
        health.readback_once = fake_readback
        health.time.sleep = lambda *_args, **_kwargs: None
        result = health.set_drive_batch_readback(TARGETS, 1.0, EXPECTATIONS)
    finally:
        health.run_ethercat_slaves, health.run_command, health.readback_once, health.time.sleep = originals
    return result, events


def test_nominal_has_no_state_switch() -> None:
    result, events = run_batch([{"0": "OP", "1": "OP"}], {"0": [healthy_readback("0")], "1": [healthy_readback("1")]})
    assert result["ok"] is True
    assert not any(event[0] == "state" for event in events)


def test_op_transients_poll_without_state_switch() -> None:
    result, events = run_batch(
        [{"0": "OP", "1": "OP"}],
        {"0": [healthy_readback("0")], "1": [mismatch_readback("1"), healthy_readback("1")]},
    )
    assert result["ok"] is True
    assert not any(event[0] == "state" for event in events)

    result, events = run_batch(
        [{"0": "OP", "1": "OP"}],
        {"0": [healthy_readback("0")], "1": [missing_required_readback("1"), healthy_readback("1")]},
    )
    assert result["ok"] is True
    assert not any(event[0] == "state" for event in events)


def test_persistently_failed_target_fails_without_state_switch() -> None:
    result, events = run_batch(
        [{"0": "OP", "1": "OP"}, {"0": "OP", "1": "OP"}, {"0": "OP", "1": "OP"}],
        {
            "0": [healthy_readback("0")],
            "1": [missing_required_readback("1"), missing_required_readback("1"), missing_required_readback("1")],
        },
    )
    assert result["ok"] is False
    assert result["failed_positions"] == ["1"]
    assert not any(event[0] == "state" for event in events)


def test_non_op_target_can_only_recover_via_fresh_reread() -> None:
    result, events = run_batch(
        [{"0": "OP", "1": "PREOP"}, {"0": "OP", "1": "OP"}],
        {"0": [healthy_readback("0")], "1": [missing_required_readback("1"), healthy_readback("1")]},
    )
    assert result["ok"] is True
    assert not any(event[0] == "state" for event in events)


def test_multiple_failed_targets_never_trigger_al_state_commands() -> None:
    result, events = run_batch(
        [{"0": "OP", "1": "OP"}, {"0": "OP", "1": "OP"}, {"0": "OP", "1": "OP"}],
        {
            "0": [missing_required_readback("0"), missing_required_readback("0"), missing_required_readback("0")],
            "1": [missing_required_readback("1"), missing_required_readback("1"), missing_required_readback("1")],
        },
    )
    assert result["ok"] is False
    assert result["failed_positions"] == ["0", "1"]
    assert not any(event[0] == "state" for event in events)


def test_terminal_failure_preserves_actual_and_expected() -> None:
    target = {
        "axis": "X",
        "position": "0",
        "axis_cfg": {"axis": "X"},
        "profile": {"profile_id": "sv630n"},
        "commands": {
            "drive.set_egear": {"supported": True, "access": "write", "requires_save_parameters": False},
            "drive.write_mode": {"supported": True, "access": "write", "requires_save_parameters": False},
        },
    }
    failed_readback = mismatch_readback("0")
    failed_readback.update({"slave_state": "OP", "first_failure": failed_readback["health"]["failures"][0]})
    originals = {
        name: getattr(bus, name)
        for name in (
            "configured_drive_targets", "precheck_targets_for_write", "target_egear", "read_required_state",
            "write_command", "set_drive_batch_readback", "mark_drive_parameters_invalid",
            "persist_settings_runtime", "write_drive_parameter_display_rows",
        )
    }
    original_window_begin = bus.v5_drive_enable_window.begin
    original_window_finish = bus.v5_drive_enable_window.finish
    try:
        bus.configured_drive_targets = lambda _timeout: ([target], {"axes": []}, {"ok": True, "slaves": []})
        bus.precheck_targets_for_write = lambda *_args, **_kwargs: []
        bus.target_egear = lambda _target: (16384, 3125, {"source": "smoke"})
        bus.read_required_state = lambda *_args, **_kwargs: {"ok": True, "reads": {"drive.read_mode": {"upload": {"value": 8}}}}
        bus.write_command = lambda *_args, **_kwargs: {"ok": True, "code": "OK"}
        bus.set_drive_batch_readback = lambda *_args, **_kwargs: {
            "ok": False,
            "code": "DRIVE_SET_BATCH_READBACK_FAILED",
            "cycles": [{"attempt": 1}],
            "readbacks": {"0": failed_readback},
            "failed_positions": ["0"],
        }
        bus.mark_drive_parameters_invalid = lambda *_args, **_kwargs: None
        bus.persist_settings_runtime = lambda _runtime: {"ok": True}
        bus.write_drive_parameter_display_rows = lambda _updates: {"ok": True}
        bus.v5_drive_enable_window.begin = lambda *_args, **_kwargs: {"ok": True, "code": "DRIVE_WRITE_WINDOW_READY", "run_id": "smoke"}
        bus.v5_drive_enable_window.finish = lambda *_args, **_kwargs: {"ok": True, "code": "DRIVE_WRITE_WINDOW_CLOSED", "run_id": "smoke"}
        action_result = bus.run_set_drive(1.0)
    finally:
        for name, value in originals.items():
            setattr(bus, name, value)
        bus.v5_drive_enable_window.begin = original_window_begin
        bus.v5_drive_enable_window.finish = original_window_finish
    assert action_result["ok"] is False
    failure = action_result["targets"][0]["readback"]["health"]["failures"][0]
    assert (failure["field"], failure["actual"], failure["expected"]) == ("mode", 7, 8)
    compact = drive_result.compact_action_result_payload(action_result)
    compact_failure = compact["targets"][0]["readback"]["health"]["failures"][0]
    assert (compact_failure["field"], compact_failure["actual"], compact_failure["expected"]) == ("mode", 7, 8)


def run_enable_window_success_case(
        initial_machine_enabled: bool,
        final_machine_enabled: bool,
        finish_ok: bool = True,
        finish_code: str = "DRIVE_WRITE_WINDOW_FINISH_RESTORED",
        batch_readback_ok: bool = True) -> Tuple[Dict[str, Any], List[Tuple[str, ...]]]:
    events: List[Tuple[str, ...]] = []
    target = {
        "axis": "X",
        "position": "0",
        "axis_cfg": {"axis": "X"},
        "profile": {"profile_id": "sv630n"},
        "commands": {
            "drive.set_egear": {"supported": True, "access": "write", "requires_disabled": True, "requires_save_parameters": False},
            "drive.write_mode": {"supported": True, "access": "write", "requires_disabled": True, "requires_save_parameters": False},
        },
    }
    originals = {
        name: getattr(bus, name)
        for name in (
            "configured_drive_targets", "precheck_targets_for_write", "target_egear", "read_required_state",
            "write_command", "set_drive_batch_readback", "update_axis_drive_set_evidence",
            "persist_settings_runtime", "write_drive_parameter_display_rows", "drive_display_update_from_health",
            "mark_drive_parameters_invalid",
        )
    }
    original_window_begin = bus.v5_drive_enable_window.begin
    original_window_finish = bus.v5_drive_enable_window.finish
    try:
        bus.configured_drive_targets = lambda _timeout: (events.append(("targets",)) or ([target], {"axes": []}, {"ok": True, "slaves": []}))
        bus.v5_drive_enable_window.begin = lambda run_id, _timeout: (events.append(("window_begin", run_id)) or {"ok": True, "run_id": run_id, "initial_machine_enabled": initial_machine_enabled, "final_machine_enabled": False})
        bus.precheck_targets_for_write = lambda *_args, **_kwargs: (events.append(("fresh_disabled_precheck",)) or [])
        bus.target_egear = lambda _target: (16384, 3125, {"source": "smoke"})
        bus.read_required_state = lambda *_args, **_kwargs: (events.append(("pre_read",)) or {"ok": True, "reads": {"drive.read_mode": {"upload": {"value": 8}}}})
        bus.write_command = lambda *_args, **_kwargs: (events.append(("write",)) or {"ok": True, "code": "OK"})
        bus.set_drive_batch_readback = lambda *_args, **_kwargs: (events.append(("batch_readback",)) or {
            "ok": batch_readback_ok,
            "code": "DRIVE_SET_BATCH_READBACK_OK" if batch_readback_ok else "DRIVE_SET_BATCH_READBACK_FAILED",
            "cycles": [{"attempt": 1}],
            "readbacks": {"0": healthy_readback("0") if batch_readback_ok else mismatch_readback("0")},
            "failed_positions": [] if batch_readback_ok else ["0"],
        })
        bus.update_axis_drive_set_evidence = lambda *_args, **_kwargs: None
        bus.mark_drive_parameters_invalid = lambda *_args, **_kwargs: None
        bus.persist_settings_runtime = lambda _runtime: {"ok": True}
        bus.write_drive_parameter_display_rows = lambda _updates: {"ok": True}
        bus.drive_display_update_from_health = lambda *_args, **_kwargs: {"axis": "X", "write_status": "已写入"}
        def fake_finish(run_id: str, _timeout: float, restore: bool = False) -> Dict[str, Any]:
            events.append(("window_finish", run_id, str(int(restore))))
            return {
                "ok": finish_ok,
                "code": finish_code,
                "run_id": run_id,
                "initial_machine_enabled": initial_machine_enabled,
                "final_machine_enabled": final_machine_enabled,
            }
        bus.v5_drive_enable_window.finish = fake_finish
        result = bus.run_set_drive(1.0, {"_run_id": "enable-window-smoke"})
    finally:
        for name, value in originals.items():
            setattr(bus, name, value)
        bus.v5_drive_enable_window.begin = original_window_begin
        bus.v5_drive_enable_window.finish = original_window_finish
    return result, events


def test_enable_window_precedes_all_sdo_and_restores_initial_enabled() -> None:
    result, events = run_enable_window_success_case(True, True)
    assert result["ok"] is True
    assert events == [
        ("targets",),
        ("window_begin", "enable-window-smoke"),
        ("fresh_disabled_precheck",),
        ("pre_read",),
        ("write",),
        ("batch_readback",),
        ("window_finish", "enable-window-smoke", "1"),
    ]
    assert result["restart_required"] is True
    assert result["restart_deferred"] is True
    assert result["drive_write_window"]["restore_requested"] is True
    assert result["drive_write_window"]["restore_expected"] is True
    assert result["drive_write_window"]["restore_confirmed"] is True
    assert result["drive_write_window"]["finish"]["code"] == "DRIVE_WRITE_WINDOW_FINISH_RESTORED"


def test_enable_window_success_preserves_initial_machine_off() -> None:
    result, events = run_enable_window_success_case(
        False, False, finish_code="DRIVE_WRITE_WINDOW_FINISH_KEPT_OFF")
    assert result["ok"] is True
    assert events[-1] == ("window_finish", "enable-window-smoke", "1")
    assert result["drive_write_window"]["restore_requested"] is True
    assert result["drive_write_window"]["restore_expected"] is False
    assert result["drive_write_window"]["restore_confirmed"] is True
    assert result["drive_write_window"]["finish"]["final_machine_enabled"] is False
    assert result["restart_required"] is True
    assert result["restart_deferred"] is True


def test_enable_window_restore_not_confirmed_fails_closed() -> None:
    result, events = run_enable_window_success_case(
        True, False, finish_code="DRIVE_WRITE_WINDOW_FINISH_KEPT_OFF")
    assert events[-1] == ("window_finish", "enable-window-smoke", "1")
    assert result["ok"] is False
    assert result["code"] == "DRIVE_WRITE_WINDOW_RESTORE_NOT_CONFIRMED"
    assert result["drive_write_window"]["restore_requested"] is True
    assert result["drive_write_window"]["restore_expected"] is True
    assert result["drive_write_window"]["restore_confirmed"] is False
    assert result["restart_required"] is False
    assert result["restart_deferred"] is False


def test_enable_window_write_failure_never_restores() -> None:
    result, events = run_enable_window_success_case(
        True, False, finish_code="DRIVE_WRITE_WINDOW_FINISH_KEEP_OFF",
        batch_readback_ok=False)
    assert events[-1] == ("window_finish", "enable-window-smoke", "0")
    assert result["ok"] is False
    assert result["code"] == "DRIVE_SET_PARTIAL"
    assert result["drive_write_window"]["restore_requested"] is False
    assert result["drive_write_window"]["restore_confirmed"] is True
    assert result["restart_required"] is False
    assert result["restart_deferred"] is False


def test_window_begin_failure_prevents_all_sdo() -> None:
    writes: List[str] = []
    target = {"axis": "X", "position": "0", "axis_cfg": {"axis": "X"}, "commands": {}}
    original_targets = bus.configured_drive_targets
    original_begin = bus.v5_drive_enable_window.begin
    original_write = bus.write_command
    try:
        bus.configured_drive_targets = lambda _timeout: ([target], {"axes": []}, {"ok": True})
        def reject_begin(*_args: Any, **_kwargs: Any) -> Dict[str, Any]:
            raise DriveActionError("DRIVE_WRITE_WINDOW_MACHINE_OFF_NOT_CONFIRMED", "未确认下使能。", {})
        bus.v5_drive_enable_window.begin = reject_begin
        bus.write_command = lambda *_args, **_kwargs: (writes.append("write") or {"ok": True})
        result = bus.run_set_drive(1.0, {"_run_id": "begin-failure-smoke"})
    finally:
        bus.configured_drive_targets = original_targets
        bus.v5_drive_enable_window.begin = original_begin
        bus.write_command = original_write
    assert result["ok"] is False
    assert result["code"] == "DRIVE_WRITE_WINDOW_MACHINE_OFF_NOT_CONFIRMED"
    assert writes == []


def test_auto_disable_transition_polls_fresh_status_without_false_error() -> None:
    target = {
        "axis": "X",
        "position": "0",
        "commands": {
            "drive.set_egear": {"supported": True, "requires_disabled": True},
            "drive.write_mode": {"supported": True, "requires_disabled": True},
        },
    }
    attempts: List[int] = []
    sleeps: List[float] = []
    original_supported = health.command_write_supported
    original_safety = health.assert_drive_write_safety
    original_sleep = health.time.sleep
    try:
        health.command_write_supported = lambda *_args, **_kwargs: None
        def fresh_safety(*_args: Any, **_kwargs: Any) -> Dict[str, Any]:
            attempts.append(1)
            if len(attempts) == 1:
                raise DriveActionError(
                    "DRIVE_WRITE_REQUIRES_DISABLED",
                    "transition pending",
                    {"statusword": 0x0004},
                )
            return {"ok": True, "statusword": 0, "no_motion_source": "statusword_not_operation_enabled"}
        health.assert_drive_write_safety = fresh_safety
        health.time.sleep = lambda delay: sleeps.append(delay)
        checks = health.precheck_targets_for_write(
            [target], ["drive.set_egear", "drive.write_mode"], 1.0,
            wait_disabled_transition=True)
    finally:
        health.command_write_supported = original_supported
        health.assert_drive_write_safety = original_safety
        health.time.sleep = original_sleep
    assert checks[0]["ok"] is True
    assert len(attempts) == 2
    assert sleeps == [health.DRIVE_DISABLE_TRANSITION_POLL_S]


def test_auto_disable_transition_timeout_remains_fail_closed() -> None:
    target = {"axis": "C", "position": "4", "commands": {}}
    times = iter((0.0, 3.0))
    original_supported = health.command_write_supported
    original_safety = health.assert_drive_write_safety
    original_monotonic = health.time.monotonic
    try:
        health.command_write_supported = lambda *_args, **_kwargs: None
        health.assert_drive_write_safety = lambda *_args, **_kwargs: (_ for _ in ()).throw(
            DriveActionError("DRIVE_WRITE_REQUIRES_DISABLED", "transition pending", {"statusword": 0x0004}))
        health.time.monotonic = lambda: next(times)
        try:
            health.precheck_targets_for_write(
                [target], ["drive.set_egear"], 1.0,
                wait_disabled_transition=True)
            raise AssertionError("persistent enabled state accepted")
        except DriveActionError as exc:
            assert exc.code == "DRIVE_WRITE_DISABLE_NOT_CONFIRMED"
    finally:
        health.command_write_supported = original_supported
        health.assert_drive_write_safety = original_safety
        health.time.monotonic = original_monotonic


test_nominal_has_no_state_switch()
test_op_transients_poll_without_state_switch()
test_persistently_failed_target_fails_without_state_switch()
test_non_op_target_can_only_recover_via_fresh_reread()
test_multiple_failed_targets_never_trigger_al_state_commands()
test_terminal_failure_preserves_actual_and_expected()
test_enable_window_precedes_all_sdo_and_restores_initial_enabled()
test_enable_window_success_preserves_initial_machine_off()
test_enable_window_restore_not_confirmed_fails_closed()
test_enable_window_write_failure_never_restores()
test_window_begin_failure_prevents_all_sdo()
test_auto_disable_transition_polls_fresh_status_without_false_error()
test_auto_disable_transition_timeout_remains_fail_closed()
print("set-drive batch readback smoke ok")
