from __future__ import annotations

import time
from typing import Any, Dict, List

from v5_drive_bus_contract import DriveActionError
from v5_drive_health import (
    recover_full_target_set_mailbox,
    request_full_target_set_state,
)
from v5_drive_sdo import run_ethercat_slaves


RESET_RECOVERY_TIMEOUT_S = 60.0
RESET_RECOVERY_POLL_S = 1.0
RESET_TOPOLOGY_STABLE_SAMPLES = 2


def prepare_frozen_set_for_reset(
    targets: List[Dict[str, Any]], timeout_s: float,
) -> Dict[str, Any]:
    result = request_full_target_set_state(targets, "PREOP", timeout_s)
    if not result.get("ok"):
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_PRE_RESET_PREOP_FAILED",
            "software reset 前未能把完整冻结从站集合统一降到 PREOP。",
            result,
        )
    return result


def _topology_ready(
    targets: List[Dict[str, Any]], scan: Dict[str, Any],
) -> bool:
    expected = {str(target.get("position") or "") for target in targets}
    slaves = [
        item for item in (scan.get("slaves") or [])
        if isinstance(item, dict)
    ]
    actual = {str(item.get("position") or "") for item in slaves}
    return (
        bool(scan.get("ok"))
        and bool(expected)
        and actual == expected
        and all(
            str(item.get("state") or "").upper()
            in {"PREOP", "SAFEOP", "OP"}
            and "ERROR" not in str(item.get("line") or "").upper()
            for item in slaves
        )
    )


def recover_frozen_set_after_reset(
    targets: List[Dict[str, Any]], timeout_s: float,
) -> Dict[str, Any]:
    budget_s = min(max(float(timeout_s), RESET_RECOVERY_POLL_S),
                   RESET_RECOVERY_TIMEOUT_S)
    deadline = time.monotonic() + budget_s
    stable_samples = 0
    attempts: List[Dict[str, Any]] = []
    last_result: Dict[str, Any] = {
        "ok": False,
        "code": "DRIVE_TARGET_SET_RECOVERY_TIMEOUT",
        "failed_positions": [
            str(target.get("position") or "") for target in targets
        ],
    }
    while time.monotonic() < deadline:
        remaining = max(0.1, deadline - time.monotonic())
        scan = run_ethercat_slaves(min(2.0, remaining))
        stable_samples = stable_samples + 1 if _topology_ready(
            targets, scan) else 0
        if stable_samples >= RESET_TOPOLOGY_STABLE_SAMPLES:
            last_result = recover_full_target_set_mailbox(
                targets, min(10.0, remaining))
            attempts.append({
                "code": str(last_result.get("code") or ""),
                "failed_positions": list(
                    last_result.get("failed_positions") or []),
            })
            if last_result.get("ok"):
                result = dict(last_result)
                result["attempts"] = attempts
                return result
            stable_samples = 0
        time.sleep(min(RESET_RECOVERY_POLL_S,
                       max(0.0, deadline - time.monotonic())))
    result = dict(last_result)
    result["attempts"] = attempts
    return result
