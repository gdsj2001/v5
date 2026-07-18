#!/usr/bin/env python3
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
import importlib.util
import os
import struct
import sys
import tempfile


HERE = Path(__file__).resolve().parent
POLICY_PATH = HERE / "check_v5_board_runtime_policy.py"
INIT_PATH = HERE.parents[1] / "services" / "state_publisher" / "init.d" / "v5-position-status-publisher"
POSITION_STRUCT = struct.Struct("<IIIIIIQ14dII")


def load_remote_namespace():
    spec = importlib.util.spec_from_file_location("v5_board_policy", POLICY_PATH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    prefix, separator, _tail = module.REMOTE_CHECK.partition("\nrc = 0\n")
    assert separator, "remote definition boundary missing"
    sentinel = object()
    saved_pwd = sys.modules.get("pwd", sentinel)
    if saved_pwd is sentinel:
        sys.modules["pwd"] = SimpleNamespace(getpwuid=lambda uid: SimpleNamespace(pw_name=str(uid)))
    namespace = {"__name__": "v5_board_policy_remote_smoke"}
    try:
        exec(prefix, namespace)
    finally:
        if saved_pwd is sentinel:
            sys.modules.pop("pwd", None)
        else:
            sys.modules["pwd"] = saved_pwd
    return namespace, module.REMOTE_CHECK


def fnv1a(payload: bytes) -> int:
    value = 2166136261
    for byte in payload:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


class PositionFixture:
    def __init__(self, namespace, root: Path):
        self.ns = namespace
        self.root = root
        self.proc_root = root / "proc"
        self.proc_locks = root / "proc-locks"
        self.pidfile = root / "position.pid"
        self.lockfile = root / "position.lock"
        self.block = root / "position.bin"
        self.pid = 4242
        self.start_ticks = 987654
        self.writer_identity = 0x10203040
        self.daemon = namespace["POSITION_DAEMON_PATH"]
        self.proc_root.mkdir()
        self.lockfile.write_text(
            f"{self.pid} {self.start_ticks} {self.writer_identity}\n", encoding="ascii")
        os_api = os
        if not hasattr(os, "major"):
            os_api = SimpleNamespace(
                stat=os.stat,
                major=lambda dev: (int(dev) >> 8) & 0xfff,
                minor=lambda dev: int(dev) & 0xff,
            )
        namespace.update({
            "os": os_api,
            "PROC_ROOT": str(self.proc_root),
            "PROC_LOCKS_PATH": str(self.proc_locks),
            "POSITION_LOCK_PATH": str(self.lockfile),
            "POSITION_BLOCK_PATH": str(self.block),
        })
        self.write_process(self.pid, self.start_ticks, ["/usr/bin/python3", self.daemon, "--path", str(self.block)])
        self.write_owner_record()
        self.write_lock_record(self.pid)
        self.write_block(self.writer_identity)

    def write_process(self, pid: int, start_ticks: int, argv: list[str]):
        proc = self.proc_root / str(pid)
        proc.mkdir(exist_ok=True)
        tail = ["S"] + ["0"] * 18 + [str(start_ticks)] + ["0"] * 20
        (proc / "stat").write_text(
            f"{pid} (v5 position worker) {' '.join(tail)}\n", encoding="ascii")
        (proc / "cmdline").write_bytes(b"\0".join(item.encode("utf-8") for item in argv) + b"\0")

    def write_owner_record(self, text: str | None = None):
        value = text or f"{self.pid} {self.start_ticks} {self.writer_identity}\n"
        self.pidfile.write_text(value, encoding="ascii")
        self.lockfile.write_text(value, encoding="ascii")

    def write_lock_record(self, owner_pid: int):
        lock_stat = self.ns["os"].stat(self.lockfile)
        major = self.ns["os"].major(lock_stat.st_dev)
        minor = self.ns["os"].minor(lock_stat.st_dev)
        self.proc_locks.write_text(
            f"1: FLOCK ADVISORY WRITE {owner_pid} {major:x}:{minor:x}:{lock_stat.st_ino} 0 EOF\n",
            encoding="ascii",
        )

    def write_block(self, writer_identity: int):
        values = [0x56504F53, 2, POSITION_STRUCT.size, 3, 5, writer_identity, 123456]
        values.extend([0.0] * 14)
        values.extend([0, 0])
        payload = POSITION_STRUCT.pack(*values)
        values[-2] = fnv1a(payload[:-8])
        self.block.write_bytes(POSITION_STRUCT.pack(*values))

    def audit(self):
        stdout = StringIO()
        stderr = StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            pid, rc = self.ns["position_service_audit"](str(self.pidfile))
        return pid, rc, stdout.getvalue() + stderr.getvalue()


def expect_case(namespace, mutate, marker: str, expected_rc: int = 1):
    with tempfile.TemporaryDirectory(prefix="v5-position-policy-") as raw_root:
        fixture = PositionFixture(namespace, Path(raw_root))
        mutate(fixture)
        pid, rc, output = fixture.audit()
        assert rc == expected_rc, output
        assert marker in output, (marker, output)
        assert "SKIP v5_position_status_publisher" not in output
        if expected_rc == 0:
            assert pid == fixture.pid


def remove_fixture_process(fixture: PositionFixture):
    proc = fixture.proc_root / str(fixture.pid)
    (proc / "cmdline").unlink()
    (proc / "stat").unlink()
    proc.rmdir()


def corrupt_fixture_block_crc(fixture: PositionFixture):
    payload = bytearray(fixture.block.read_bytes())
    payload[32] ^= 0x01
    fixture.block.write_bytes(payload)


def check_init_exact_argv(text: str) -> bool:
    return (
        "tr '\\000' '\\n'" in text
        and 'grep -Fqx "$DAEMON"' in text
        and "tr '\\000' ' '" not in text
        and 'grep -F "$DAEMON"' not in text
    )


def real_proc_locks_smoke(namespace):
    if os.name == "nt":
        print("SKIP_REAL_PROC_LOCKS_WINDOWS")
        return
    import fcntl

    with tempfile.TemporaryDirectory(prefix="v5-real-flock-") as raw_root:
        lock_path = Path(raw_root) / "owner.lock"
        with lock_path.open("w+") as handle:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            namespace["os"] = os
            namespace["POSITION_LOCK_PATH"] = str(lock_path)
            namespace["PROC_LOCKS_PATH"] = "/proc/locks"
            owner = namespace["position_lock_owner_pid"]()
            assert owner == os.getpid(), (owner, os.getpid())
            print(f"V5_BOARD_RUNTIME_REAL_PROC_LOCKS_OK pid={owner}")


def main() -> int:
    namespace, remote_source = load_remote_namespace()
    expect_case(namespace, lambda fixture: None, "OK_POSITION_IDENTITY", 0)
    expect_case(namespace, lambda fixture: fixture.pidfile.unlink(), "owner_record=pidfile_missing")
    expect_case(namespace, lambda fixture: fixture.write_owner_record("4242 broken 7\n"), "owner_record=pidfile_fields")
    expect_case(namespace, remove_fixture_process, "owner_record=dead_pid")
    expect_case(
        namespace,
        lambda fixture: fixture.write_owner_record(
            f"{fixture.pid} {fixture.start_ticks + 1} {fixture.writer_identity}\n"),
        "owner_record=start_ticks",
    )
    expect_case(
        namespace,
        lambda fixture: fixture.write_process(
            fixture.pid,
            fixture.start_ticks,
            ["/usr/bin/python3", f"--other={fixture.daemon}"],
        ),
        "identity=canonical_cmdline",
    )
    expect_case(
        namespace,
        lambda fixture: fixture.write_process(
            fixture.pid + 1,
            fixture.start_ticks + 1,
            ["/usr/bin/python3", fixture.daemon],
        ),
        "identity=unique_process",
    )
    expect_case(
        namespace,
        lambda fixture: fixture.write_lock_record(fixture.pid + 1),
        "identity=lock_owner",
    )
    expect_case(
        namespace,
        lambda fixture: fixture.write_block(fixture.writer_identity + 1),
        "identity=block_identity",
    )
    expect_case(namespace, corrupt_fixture_block_crc, "identity=block_crc")

    assert 'if name == "v5_position_status_publisher":\n        pid, service_rc = position_service_audit(pidfile)' in remote_source
    assert 'print(f"SKIP {name}: no live pid")' in remote_source
    assert '["flock", "-n", lock_path, "true"]' not in remote_source
    assert "import fcntl" not in remote_source

    init_text = INIT_PATH.read_text(encoding="utf-8")
    assert check_init_exact_argv(init_text)
    mutated_init = init_text.replace('grep -Fqx "$DAEMON"', 'grep -F "$DAEMON"', 1)
    assert not check_init_exact_argv(mutated_init), "substring argv mutation survived"
    real_proc_locks_smoke(namespace)
    print("V5_BOARD_RUNTIME_POSITION_IDENTITY_POLICY_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
