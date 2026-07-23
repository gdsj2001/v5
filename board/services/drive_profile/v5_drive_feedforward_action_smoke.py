#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
from typing import Any, Callable, Dict, List, Tuple

import v5_drive_feedforward_action as action


def target(axis: str, position: str) -> Dict[str, Any]:
    commands = {
        name: {
            "supported": True,
            "access": "write" if name.startswith("drive.write_")
            or name == action.SOFTWARE_RESET else "read",
            "requires_disabled": name.startswith("drive.write_")
            or name == action.SOFTWARE_RESET,
            "requires_no_motion": name.startswith("drive.write_")
            or name == action.SOFTWARE_RESET,
        }
        for name in (
            action.READ_SOURCE,
            action.READ_FILTER,
            action.READ_GAIN,
            action.WRITE_GAIN,
            action.READ_EEPROM,
            action.WRITE_EEPROM,
            action.SOFTWARE_RESET,
            action.READ_ERROR,
        )
    }
    return {
        "axis": axis,
        "position": position,
        "profile": {"profile_id": "sv630n-test"},
        "commands": commands,
        "axis_cfg": {"axis": axis},
    }


def run_case(
    target_raw: int,
    *,
    initial_policy: int = 0,
    fail_first_gain_write: bool = False,
    post_reset_error_once: bool = False,
) -> Tuple[Dict[str, Any], List[Tuple[Any, ...]], Dict[str, Any]]:
    events: List[Tuple[Any, ...]] = []
    targets = [target("X", "0"), target("C", "1")]
    runtime = {"schema": "re.v3.settings_runtime.drive_only.v1",
               "axes": [item["axis_cfg"] for item in targets]}
    values = {
        "0": {
            action.READ_SOURCE: 1,
            action.READ_FILTER: 7,
            action.READ_GAIN: 0,
            action.READ_EEPROM: initial_policy,
            action.READ_ERROR: 0,
        },
        "1": {
            action.READ_SOURCE: 1,
            action.READ_FILTER: 7,
            action.READ_GAIN: 0,
            action.READ_EEPROM: 0,
            action.READ_ERROR: 0,
        },
    }
    originals: Dict[str, Callable[..., Any]] = {}
    gain_write_failed = False
    reset_error_injected = False

    def replace(name: str, value: Callable[..., Any]) -> None:
        originals[name] = getattr(action, name)
        setattr(action, name, value)

    replace("configured_drive_targets", lambda _timeout: (
        targets,
        runtime,
        {"ok": True, "profile_snapshot": {
            "generated_at": "snapshot-test", "profile_count": 1}},
    ))
    replace("precheck_targets_for_write", lambda selected, names, _timeout,
            wait_disabled_transition=False: events.append((
                "precheck",
                tuple(item["axis"] for item in selected),
                tuple(names),
                wait_disabled_transition,
            )) or [{"ok": True}])

    def read(position: str, command_name: str, _command: Dict[str, Any],
             _required: bool) -> Dict[str, Any]:
        events.append(("read", position, command_name))
        return {
            "ok": True,
            "upload": {"ok": True, "value": values[position][command_name]},
        }

    def write(position: str, command_name: str, _command: Dict[str, Any],
              value: Any = None) -> Dict[str, Any]:
        nonlocal gain_write_failed, reset_error_injected
        raw = int(value)
        events.append(("write", position, command_name, raw))
        if (fail_first_gain_write
                and command_name == action.WRITE_GAIN
                and not gain_write_failed):
            gain_write_failed = True
            return {"ok": False, "code": "TEST_GAIN_WRITE_FAILED"}
        if command_name == action.WRITE_GAIN:
            values[position][action.READ_GAIN] = raw
        elif command_name == action.WRITE_EEPROM:
            values[position][action.READ_EEPROM] = raw
        elif (command_name == action.SOFTWARE_RESET
              and post_reset_error_once and not reset_error_injected):
            values["1"][action.READ_ERROR] = 3605
            reset_error_injected = True
        return {"ok": True, "code": "OK"}

    replace("read_command", read)
    replace("write_command", write)
    replace("prepare_frozen_set_for_reset",
            lambda selected, _timeout: events.append(("prepare", tuple(
                item["position"] for item in selected))) or {
                "ok": True, "code": "DRIVE_STATE_TARGET_SET_OK"})

    def recover(selected: List[Dict[str, Any]], _timeout: float) -> Dict[str, Any]:
        events.append(("recover", tuple(
            item["position"] for item in selected)))
        return {
            "ok": True,
            "code": "DRIVE_TARGET_SET_RECOVERY_OK",
            "operations": [{"state": "PREOP"}, {"state": "OP"}],
        }

    replace("recover_frozen_set_after_reset", recover)
    def fault_reset(selected: List[Dict[str, Any]], _timeout: float) -> Dict[str, Any]:
        events.append(("fault_reset", tuple(
            item["position"] for item in selected)))
        for item in selected:
            values[item["position"]][action.READ_ERROR] = 0
        return {"ok": True, "code": "DRIVE_FAULT_RESET_BATCH_OK"}

    replace("fault_reset_batch", fault_reset)
    original_sleep = action.time.sleep
    action.time.sleep = lambda delay: events.append(("recovery_wait", delay))
    replace("persist_settings_runtime", lambda payload:
            events.append(("persist", payload["axes"][0][
                "velocity_feedforward_evidence"]["gain_after_reset_raw"])) or {
                "ok": True, "code": "SETTINGS_RUNTIME_WRITEBACK_OK"})

    original_begin = action.v5_drive_enable_window.begin
    original_finish = action.v5_drive_enable_window.finish_safely
    action.v5_drive_enable_window.begin = lambda run_id, _timeout: (
        events.append(("window_begin", run_id)) or {
            "ok": True,
            "initial_machine_enabled": True,
            "final_machine_enabled": False,
        })
    action.v5_drive_enable_window.finish_safely = (
        lambda run_id, _timeout, restore=False:
        events.append(("window_finish", run_id, restore)) or {
            "ok": True,
            "initial_machine_enabled": True,
            "final_machine_enabled": False,
        })
    try:
        result = action.run_velocity_feedforward_commission(
            5.0,
            {
                "_run_id": "feedforward-smoke",
                "axis": "X",
                "target_gain_raw": target_raw,
            },
        )
    finally:
        action.time.sleep = original_sleep
        action.v5_drive_enable_window.begin = original_begin
        action.v5_drive_enable_window.finish_safely = original_finish
        for name, value in originals.items():
            setattr(action, name, value)
    return result, events, runtime


success, success_events, success_runtime = run_case(100)
assert success["ok"] is True
assert success["code"] == "DRIVE_FEEDFORWARD_COMMISSION_OK"
assert success["drive_write_window"]["final_off_confirmed"] is True
assert success["restart_required"] is True
assert success["restart_deferred"] is True
assert success["backend_restart_required"] is True
assert success["canonical_clean_restart_required"] is True
assert success_runtime["axes"][0]["velocity_feedforward_evidence"][
    "gain_after_reset_raw"] == 100
success_writes = [event for event in success_events if event[0] == "write"]
assert success_writes == [
    ("write", "0", action.WRITE_EEPROM, 1),
    ("write", "0", action.WRITE_GAIN, 100),
    ("write", "0", action.WRITE_EEPROM, 0),
    ("write", "0", action.SOFTWARE_RESET, 1),
]
assert ("recover", ("0", "1")) in success_events
assert success_events[-1] == ("window_finish", "feedforward-smoke", False)

reset_fault_clear, reset_fault_events, _runtime = run_case(
    100, post_reset_error_once=True)
assert reset_fault_clear["ok"] is True
assert ("fault_reset", ("0", "1")) in reset_fault_events

too_large, too_large_events, _runtime = run_case(101)
assert too_large["ok"] is False
assert too_large["code"] == "DRIVE_FEEDFORWARD_STEP_TOO_LARGE"
assert "restart_required" not in too_large
assert not any(event[0] == "write" for event in too_large_events)

legacy_policy, legacy_policy_events, _runtime = run_case(
    100, initial_policy=3)
assert legacy_policy["ok"] is True
assert legacy_policy["baseline"]["eeprom_policy"] == 3
assert ("write", "0", action.WRITE_EEPROM, 0) in legacy_policy_events

bad_policy, bad_policy_events, _runtime = run_case(100, initial_policy=9)
assert bad_policy["ok"] is False
assert bad_policy["code"] == "DRIVE_FEEDFORWARD_EEPROM_POLICY_INVALID"
assert not any(event[0] == "write" for event in bad_policy_events)

rolled_back, rollback_events, _runtime = run_case(
    100, fail_first_gain_write=True)
assert rolled_back["ok"] is False
assert rolled_back["rollback"]["ok"] is True
assert rolled_back["restart_required"] is True
assert rolled_back["restart_deferred"] is True
assert rolled_back["backend_restart_required"] is True
assert rolled_back["canonical_clean_restart_required"] is True
rollback_writes = [event for event in rollback_events if event[0] == "write"]
assert rollback_writes == [
    ("write", "0", action.WRITE_EEPROM, 1),
    ("write", "0", action.WRITE_GAIN, 100),
    ("write", "0", action.WRITE_EEPROM, 0),
    ("write", "0", action.WRITE_EEPROM, 1),
    ("write", "0", action.WRITE_GAIN, 0),
    ("write", "0", action.WRITE_EEPROM, 0),
    ("write", "0", action.SOFTWARE_RESET, 1),
]
assert rolled_back["drive_write_window"]["final_off_confirmed"] is True

actiond_source = Path(__file__).with_name("v5_settings_actiond.py").read_text(
    encoding="utf-8")
assert '"drive_velocity_feedforward_commission",' in actiond_source
assert "cancel_allowed=action not in NON_CANCELLABLE_ACTIONS" in actiond_source
assert '"SETTINGS_ACTION_CANCEL_NOT_ALLOWED"' in actiond_source

print("drive velocity feedforward action smoke ok")
