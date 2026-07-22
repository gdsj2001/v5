#!/usr/bin/env python3
from __future__ import annotations

import ast
from pathlib import Path


SOURCE = Path(__file__).with_name("v5_settings_actiond.py")


class FakeWindow:
    def __init__(self) -> None:
        self.active_run_id = ""
        self.abort_calls: list[str] = []

    def begin(self, run_id: str) -> bool:
        if self.active_run_id and self.active_run_id != run_id:
            return False
        self.active_run_id = run_id
        return True

    def abort(self, run_id: str):
        self.abort_calls.append(run_id)
        if self.active_run_id and self.active_run_id != run_id:
            raise RuntimeError("run mismatch")
        self.active_run_id = ""
        return {"ok": True, "code": "DRIVE_WRITE_WINDOW_ABORT_OK", "run_id": run_id}


def load_cleanup_helper(fake_window: FakeWindow):
    tree = ast.parse(SOURCE.read_text(encoding="utf-8"), filename=str(SOURCE))
    nodes = [
        node for node in tree.body
        if (isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "DRIVE_WRITE_WINDOW_ACTIONS"
                    for target in node.targets))
        or (isinstance(node, ast.FunctionDef) and node.name == "cleanup_failed_drive_write_window")
    ]
    module = ast.Module(body=nodes, type_ignores=[])
    ast.fix_missing_locations(module)
    namespace = {"Dict": dict, "Any": object, "v5_drive_enable_window": fake_window}
    exec(compile(module, str(SOURCE), "exec"), namespace)
    return namespace["cleanup_failed_drive_write_window"]


def load_named_function(name: str, namespace: dict):
    tree = ast.parse(SOURCE.read_text(encoding="utf-8"), filename=str(SOURCE))
    node = next(
        item for item in tree.body
        if isinstance(item, ast.FunctionDef) and item.name == name)
    module = ast.Module(body=[node], type_ignores=[])
    ast.fix_missing_locations(module)
    namespace.setdefault("Dict", dict)
    namespace.setdefault("Any", object)
    exec(compile(module, str(SOURCE), "exec"), namespace)
    return namespace[name]


class FakeBootEvent:
    def __init__(self, active: bool = True) -> None:
        self.active = active
        self.clear_calls = 0

    def is_set(self) -> bool:
        return self.active

    def clear(self) -> None:
        self.active = False
        self.clear_calls += 1


def main() -> int:
    window = FakeWindow()
    cleanup = load_cleanup_helper(window)
    assert window.begin("factory-run-1") is True
    result = cleanup("drive_factory_reset", "factory-run-1", False)
    assert result["ok"] is True
    assert window.abort_calls == ["factory-run-1"]
    assert window.begin("factory-run-2") is True
    result = cleanup("drive_factory_reset", "factory-run-2", True)
    assert result is None
    assert window.active_run_id == "factory-run-2"
    cleanup("drive_factory_reset", "factory-run-2", False)
    assert window.begin("set-run-1") is True
    assert cleanup("drive_set_parameters", "set-run-1", False) is None
    assert window.active_run_id == "set-run-1"
    window.abort("set-run-1")
    assert window.begin("save-restart-run-1") is True
    assert cleanup("settings_save_and_restart", "save-restart-run-1", False) is None
    assert window.active_run_id == "save-restart-run-1"
    window.abort("save-restart-run-1")
    assert window.begin("read-run-1") is True
    assert cleanup("drive_parameter_read", "read-run-1", False) is None
    assert window.active_run_id == "read-run-1"

    boot_event = FakeBootEvent()
    start_action = load_named_function(
        "start_action_process",
        {
            "ACTIONS": {"device_dna_register": {"owner": "auth_download"}},
            "boot_apply_active": boot_event,
            "restart_commit_lock": __import__("threading").Lock(),
            "restart_committed_run_id": "",
            "active_job_lock": __import__("threading").Lock(),
            "active_job": {},
        },
    )
    busy = start_action(
        "device_dna_register", "boot-active", {"action": "device_dna_register"})
    assert busy["accepted"] is False
    assert busy["code"] == "SETTINGS_BOOT_DRIVE_APPLY_ACTIVE"

    events: list[dict] = []
    finalized = {
        "ok": True,
        "code": "DRIVE_BOOT_APPLY_OK",
        "boot_id": "boot-smoke",
        "drive_write_executed": True,
    }
    worker = load_named_function(
        "post_restart_drive_apply_worker",
        {
            "stop_event": object(),
            "boot_apply_active": boot_event,
            "run_boot_drive_apply_until_final": lambda _event: dict(finalized),
            "append_event": events.append,
        },
    )
    worker()
    assert boot_event.clear_calls == 1 and not boot_event.is_set()
    assert events and events[-1]["drive_write_executed"] is True

    source_text = SOURCE.read_text(encoding="utf-8")
    assert source_text.index('request.get("query") or "") == "last_status"') < \
        source_text.index("response = start_action_process(action, run_id, request)")
    print("settings actiond drive write window cancel cleanup smoke ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
