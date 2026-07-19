#!/usr/bin/env python3
from __future__ import annotations

import ast
from pathlib import Path

from v5_drive_result import compact_action_result_payload


def main() -> int:
    source = Path(__file__).with_name("v5_settings_actiond.py").read_text(encoding="utf-8")
    tree = ast.parse(source)
    functions = {
        node.name: node for node in tree.body if isinstance(node, ast.FunctionDef)
    }
    start_text = ast.get_source_segment(source, functions["start_action_process"]) or ""
    worker_text = ast.get_source_segment(source, functions["action_worker"]) or ""
    assert 'worker_request["_run_id"] = run_id' in start_text
    assert "action_worker(write_fd, action, worker_request)" in start_text
    assert "execute_action(action, request)" in worker_text
    compact = compact_action_result_payload({
        "schema": "re-v5-device-dna-register-v1",
        "action": "device_dna_register",
        "ok": True,
        "code": "DNA_REGISTER_UPLOADED_PENDING_AUTH",
        "vpsDistributionId": "390529",
    })
    assert compact["vpsDistributionId"] == "390529"
    runtime_source = Path(__file__).with_name(
        "v5_settings_action_runtime.py").read_text(encoding="utf-8")
    runtime_tree = ast.parse(runtime_source)
    runtime_functions = {
        node.name: node
        for node in runtime_tree.body
        if isinstance(node, ast.FunctionDef)
    }
    status_text = ast.get_source_segment(
        runtime_source, runtime_functions["set_last_status"]) or ""
    assert '"vpsDistributionId": vps_distribution_id' in status_text
    assert 'action != "device_dna_register"' in status_text
    print("V5_SETTINGS_ACTIOND_RUN_ID_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
