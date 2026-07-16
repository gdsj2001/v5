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
    cleanup("drive_set_parameters", "set-run-1", False)
    assert window.begin("read-run-1") is True
    assert cleanup("drive_parameter_read", "read-run-1", False) is None
    assert window.active_run_id == "read-run-1"
    print("settings actiond drive write window cancel cleanup smoke ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
