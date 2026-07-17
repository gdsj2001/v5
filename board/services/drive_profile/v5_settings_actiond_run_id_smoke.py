#!/usr/bin/env python3
from __future__ import annotations

import ast
from pathlib import Path


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
    print("V5_SETTINGS_ACTIOND_RUN_ID_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
