#!/usr/bin/env python3
from __future__ import annotations

import ast
import errno
import socket
from pathlib import Path


SOURCE = Path(__file__).with_name("v5_settings_actiond.py")


def load_isolation_helper():
    tree = ast.parse(SOURCE.read_text(encoding="utf-8"), filename=str(SOURCE))
    helper = next(
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == "handle_client_isolated"
    )
    module = ast.Module(body=[helper], type_ignores=[])
    ast.fix_missing_locations(module)
    namespace = {"errno": errno, "socket": socket}
    exec(compile(module, str(SOURCE), "exec"), namespace)
    return tree, namespace["handle_client_isolated"], namespace


def main() -> int:
    tree, helper, namespace = load_isolation_helper()
    calls = []

    def first_client(_conn):
        calls.append("broken")
        raise BrokenPipeError(32, "client disconnected")

    namespace["handle_client"] = first_client
    assert helper(object()) is False

    def reset_client(_conn):
        calls.append("reset")
        raise ConnectionResetError(errno.ECONNRESET, "peer reset")

    namespace["handle_client"] = reset_client
    assert helper(object()) is False

    def healthy_client(_conn):
        calls.append("healthy")

    namespace["handle_client"] = healthy_client
    assert helper(object()) is True
    assert calls == ["broken", "reset", "healthy"]

    def local_io_failure(_conn):
        raise OSError(errno.EIO, "local owner I/O failure")

    namespace["handle_client"] = local_io_failure
    try:
        helper(object())
    except OSError as exc:
        assert exc.errno == errno.EIO
    else:
        raise AssertionError("unexpected local OSError must not be hidden")

    serve = next(
        node for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == "serve"
    )
    called = {
        node.func.id for node in ast.walk(serve)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    assert "handle_client_isolated" in called
    assert "handle_client" not in called
    print("settings actiond disconnected-client isolation smoke ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
