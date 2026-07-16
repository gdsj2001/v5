from __future__ import annotations

import json
from types import SimpleNamespace
from typing import Any, List

from v5_drive_bus_contract import DriveActionError
import v5_drive_enable_window as window


def test_fixed_argv_and_restore_flag() -> None:
    calls: List[List[str]] = []
    original = window._run_gate

    def fake_run(argv: List[str], _timeout: float) -> Any:
        calls.append(list(argv))
        action = argv[1]
        run_id = argv[2]
        payload = {
            "ok": True,
            "code": "DRIVE_WRITE_WINDOW_%s_OK" % action.upper(),
            "run_id": run_id,
            "initial_machine_enabled": True,
            "final_machine_enabled": False,
        }
        return SimpleNamespace(returncode=0, stdout=json.dumps(payload), stderr="")

    try:
        window._run_gate = fake_run
        window.begin("run-123", 2.0)
        window.finish("run-123", 2.0, restore=False)
        window.finish("run-123", 2.0, restore=True)
        window.abort("run-123", 2.0)
    finally:
        window._run_gate = original

    prefix = str(window.DRIVE_WINDOW_CLI)
    assert calls == [
        [prefix, "begin", "run-123"],
        [prefix, "finish", "run-123", "0"],
        [prefix, "finish", "run-123", "1"],
        [prefix, "abort", "run-123"],
    ]


def test_bad_run_id_and_gate_failure_fail_closed() -> None:
    try:
        window.begin("bad run id", 1.0)
        raise AssertionError("invalid run id accepted")
    except DriveActionError as exc:
        assert exc.code == "DRIVE_WRITE_WINDOW_RUN_ID_INVALID"

    original = window._run_gate
    try:
        window._run_gate = lambda _argv, _timeout: SimpleNamespace(
            returncode=1,
            stdout=json.dumps({
                "ok": False,
                "code": "DRIVE_WRITE_WINDOW_MACHINE_OFF_NOT_CONFIRMED",
                "message_cn": "未确认下使能。",
                "run_id": "run-456",
                "initial_machine_enabled": True,
                "final_machine_enabled": True,
            }),
            stderr="",
        )
        window.begin("run-456", 1.0)
        raise AssertionError("failed gate accepted")
    except DriveActionError as exc:
        assert exc.code == "DRIVE_WRITE_WINDOW_MACHINE_OFF_NOT_CONFIRMED"
    finally:
        window._run_gate = original


test_fixed_argv_and_restore_flag()
test_bad_run_id_and_gate_failure_fail_closed()
print("drive enable window smoke ok")
