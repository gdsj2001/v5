from __future__ import annotations

from typing import Any, Callable, Dict, List, Tuple

import v5_drive_bus_action as bus
from v5_drive_bus_contract import DriveActionError


TARGETS = [
    {
        "axis": "X", "position": "0", "profile": {"profile_id": "test-profile"},
        "commands": {"drive.restore_factory_defaults": {"supported": True, "requires_disabled": True}},
        "axis_cfg": {},
    },
    {
        "axis": "C", "position": "4", "profile": {"profile_id": "test-profile"},
        "commands": {"drive.restore_factory_defaults": {"supported": True, "requires_disabled": True}},
        "axis_cfg": {},
    },
]


def healthy_readback(position: str) -> Dict[str, Any]:
    health = {
        "ok": True, "failures": [], "statusword": 0x1230, "error_code": 0,
        "mode_of_operation": 8, "egear_numerator": 16384,
        "egear_denominator": 3125 if position == "0" else 1125,
    }
    return {
        "ok": True,
        "attempts": [{"attempt": 1, "read_ok": True, "health": health, "reads": {}}],
        "health": health,
    }


def run_case(begin_ok: bool, finish_ok: bool = True,
             failed_restore_positions: set[str] | None = None,
             raise_batch: bool = False) -> Tuple[Dict[str, Any], List[Tuple[Any, ...]]]:
    events: List[Tuple[Any, ...]] = []
    failed_positions = failed_restore_positions or set()
    originals: Dict[str, Callable[..., Any]] = {}

    def replace(name: str, value: Callable[..., Any]) -> None:
        originals[name] = getattr(bus, name)
        setattr(bus, name, value)

    replace("configured_drive_targets", lambda _timeout: (
        TARGETS, {"axes": []}, {"ok": True, "slaves": []}))

    def begin(run_id: str, _timeout: float) -> Dict[str, Any]:
        events.append(("window_begin", run_id))
        if not begin_ok:
            raise DriveActionError(
                "DRIVE_WRITE_MACHINE_OFF_NOT_CONFIRMED", "未确认自动下使能。", {})
        return {
            "ok": True, "run_id": run_id,
            "initial_machine_enabled": True, "final_machine_enabled": False,
        }

    def precheck(_targets: Any, names: Any, _timeout: float,
                 wait_disabled_transition: bool = False) -> List[Dict[str, Any]]:
        events.append(("precheck", tuple(names), wait_disabled_transition))
        assert wait_disabled_transition is True
        return [{"ok": True, "axis": target["axis"], "position": target["position"]}
                for target in TARGETS]

    def write(position: str, command_name: str, _command: Dict[str, Any]) -> Dict[str, Any]:
        events.append(("restore", position, command_name))
        return {"ok": position not in failed_positions, "code": "WRITE_RESULT"}

    def batch(targets: List[Dict[str, Any]], _timeout: float) -> Dict[str, Any]:
        events.append(("batch_readback", tuple(str(target["position"]) for target in targets)))
        if raise_batch:
            raise RuntimeError("batch failed unexpectedly")
        return {
            "ok": True, "code": "DRIVE_FAULT_RESET_BATCH_OK",
            "writes": {position: {"ok": True, "code": "RESET_OK"}
                       for position in ("0", "4")},
            "readbacks": {position: healthy_readback(position) for position in ("0", "4")},
            "failed_positions": [], "recovery_positions": [],
            "recovery": None, "write_executed": True,
        }

    def finish(run_id: str, _timeout: float, restore: bool = False) -> Dict[str, Any]:
        events.append(("window_finish", run_id, restore))
        return {
            "ok": finish_ok, "run_id": run_id,
            "code": "DRIVE_WRITE_WINDOW_FINISH_KEEP_OFF" if finish_ok else "DRIVE_WRITE_MACHINE_OFF_NOT_CONFIRMED",
            "initial_machine_enabled": True,
            "final_machine_enabled": not finish_ok,
        }

    original_begin = bus.v5_drive_enable_window.begin
    original_finish = bus.v5_drive_enable_window.finish_safely
    bus.v5_drive_enable_window.begin = begin
    bus.v5_drive_enable_window.finish_safely = finish
    replace("precheck_targets_for_write", precheck)
    replace("write_command", write)
    replace("fault_reset_batch", batch)
    replace("mark_reset_invalid", lambda *_args, **_kwargs: None)
    replace("drive_display_update_from_health", lambda axis, _health, status, position: {
        "axis": axis, "position": position, "write_status": status})
    replace("persist_settings_runtime", lambda _runtime: {"ok": True})
    replace("write_drive_parameter_display_rows", lambda _updates: {"ok": True})
    try:
        try:
            result = bus.run_factory_reset(2.0, {"_run_id": "factory-reset-run"})
        except RuntimeError as exc:
            result = {"exception": str(exc)}
    finally:
        bus.v5_drive_enable_window.begin = original_begin
        bus.v5_drive_enable_window.finish_safely = original_finish
        for name, value in originals.items():
            setattr(bus, name, value)
    return result, events


ok_result, ok_events = run_case(True)
assert ok_result["ok"] is True
assert ok_result["code"] == "DRIVE_RESET_OK"
assert ok_result["drive_write_window"]["final_off_confirmed"] is True
assert ok_events == [
    ("window_begin", "factory-reset-run"),
    ("precheck", ("drive.restore_factory_defaults",), True),
    ("restore", "0", "drive.restore_factory_defaults"),
    ("restore", "4", "drive.restore_factory_defaults"),
    ("batch_readback", ("0", "4")),
    ("window_finish", "factory-reset-run", False),
]

partial_result, partial_events = run_case(True, failed_restore_positions={"0"})
assert partial_result["ok"] is False
assert partial_result["code"] == "DRIVE_RESET_PARTIAL"
assert [event[0] for event in partial_events].count("restore") == 2
assert partial_events.index(("restore", "4", "drive.restore_factory_defaults")) < partial_events.index(("batch_readback", ("0", "4")))

all_failed, _all_failed_events = run_case(
    True, failed_restore_positions={"0", "4"})
assert all_failed["ok"] is False
assert all_failed["restart_required"] is False
assert all_failed["restart_deferred"] is False

exception_result, exception_events = run_case(True, raise_batch=True)
assert exception_result["exception"] == "batch failed unexpectedly"
assert exception_events[-1] == ("window_finish", "factory-reset-run", False)

begin_fail, begin_fail_events = run_case(False)
assert begin_fail["ok"] is False
assert begin_fail["code"] == "DRIVE_WRITE_MACHINE_OFF_NOT_CONFIRMED"
assert not any(event[0] == "restore" for event in begin_fail_events)

finish_fail, finish_fail_events = run_case(True, False)
assert finish_fail["ok"] is False
assert finish_fail["code"] == "DRIVE_RESET_WINDOW_CLOSE_FAILED"
assert finish_fail_events[-1] == ("window_finish", "factory-reset-run", False)

print("drive factory reset coordinated batch smoke ok")
