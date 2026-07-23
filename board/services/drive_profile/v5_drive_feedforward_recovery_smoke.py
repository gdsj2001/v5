#!/usr/bin/env python3
from __future__ import annotations

from typing import Any, Callable, Dict, List

import v5_drive_feedforward_recovery as recovery
from v5_drive_bus_contract import DriveActionError


TARGETS = [{"position": "0"}, {"position": "1"}]
events: List[Any] = []
originals: Dict[str, Callable[..., Any]] = {}


def replace(name: str, value: Callable[..., Any]) -> None:
    originals[name] = getattr(recovery, name)
    setattr(recovery, name, value)


replace("request_full_target_set_state", lambda targets, state, _timeout:
        events.append(("prepare", state, len(targets))) or {
            "ok": True, "code": "DRIVE_STATE_TARGET_SET_OK"})
prepared = recovery.prepare_frozen_set_for_reset(TARGETS, 5.0)
assert prepared["ok"] is True
assert events == [("prepare", "PREOP", 2)]

scans = [
    {"ok": True, "slaves": [{"position": "0", "state": "PREOP"}]},
    {"ok": True, "slaves": [
        {"position": "0", "state": "PREOP"},
        {"position": "1", "state": "PREOP"},
    ]},
    {"ok": True, "slaves": [
        {"position": "0", "state": "PREOP"},
        {"position": "1", "state": "PREOP"},
    ]},
]
replace("run_ethercat_slaves", lambda _timeout: scans.pop(0))
replace("recover_full_target_set_mailbox", lambda targets, _timeout:
        events.append(("recover", len(targets))) or {
            "ok": True,
            "code": "DRIVE_TARGET_SET_RECOVERY_OK",
            "failed_positions": [],
        })
original_sleep = recovery.time.sleep
recovery.time.sleep = lambda delay: events.append(("wait", delay))
try:
    recovered = recovery.recover_frozen_set_after_reset(TARGETS, 5.0)
finally:
    recovery.time.sleep = original_sleep
assert recovered["ok"] is True
assert events.count(("recover", 2)) == 1
assert len(recovered["attempts"]) == 1

recovery.request_full_target_set_state = lambda _targets, _state, _timeout: {
    "ok": False, "code": "TEST_PREOP_FAILED"}
try:
    recovery.prepare_frozen_set_for_reset(TARGETS, 5.0)
    raise AssertionError("expected pre-reset failure")
except DriveActionError as exc:
    assert exc.code == "DRIVE_FEEDFORWARD_PRE_RESET_PREOP_FAILED"

for name, value in originals.items():
    setattr(recovery, name, value)

print("drive feedforward recovery smoke ok")
