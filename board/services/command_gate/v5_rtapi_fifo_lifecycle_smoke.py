#!/usr/bin/env python3
from __future__ import annotations

import errno
import os
import socket
import tempfile
import time
from pathlib import Path

INIT = Path(__file__).with_name("init.d") / "v5-linuxcnc-command-gate"
ASSIGNMENT = "RTAPI_FIFO_PATH=$RUN_DIR/v5_rtapi_fifo"
ENV_PREFIX = "RTAPI_FIFO_PATH='$RTAPI_FIFO_PATH' TCLLIBPATH=/usr/lib/tcltk /usr/bin/linuxcnc"


def validate_init(text: str) -> None:
    assert text.count("RUN_DIR=/run/8ax") == 1
    assert text.count(ASSIGNMENT) == 1
    assert text.count("RTAPI_FIFO_PATH=") == 3
    assert 'chown petalinux:petalinux "$RUN_DIR"' in text
    assert 'rm -f "$RTAPI_FIFO_PATH"' not in text
    assert ".rtapi_fifo" not in text
    assert "${V5_RTAPI_FIFO_PATH" not in text
    linuxcnc_commands = [line.strip() for line in text.splitlines()
                         if "/usr/bin/linuxcnc " in line]
    assert len(linuxcnc_commands) == 2
    assert all(ENV_PREFIX in line for line in linuxcnc_commands)
    assert "start_backend\n    start_gate" in text
    assert "stop_gate\n    stop_backend" in text
    assert text.count("stop_backend\n    start_backend") == 1


_fake_live: set[Path] = set()


class FakeListener:
    def __init__(self, path: Path) -> None:
        self.path = path

    def close(self) -> None:
        _fake_live.discard(self.path)


def bind_listener(path: Path):
    if hasattr(socket, "AF_UNIX"):
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(path))
        listener.listen(1)
        return listener
    if path in _fake_live:
        raise OSError(errno.EADDRINUSE, "address in use")
    path.touch(exist_ok=True)
    _fake_live.add(path)
    return FakeListener(path)


source = INIT.read_text(encoding="utf-8")
validate_init(source)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    persistent = root / ".rtapi_fifo"
    canonical = root / "v5_rtapi_fifo"

    # A closed persistent socket is deliberately left stale. The product's
    # explicit volatile path binds immediately and never reads or removes it.
    stale = bind_listener(persistent)
    stale.close()
    started = time.monotonic()
    current = bind_listener(canonical)
    assert time.monotonic() - started < 0.25
    assert persistent.exists()
    current.close()
    canonical.unlink()

    # A live owner on the canonical path remains authoritative: a second bind
    # fails EADDRINUSE, connection succeeds, and the path is not unlinked.
    live = bind_listener(canonical)
    try:
        try:
            contender = bind_listener(canonical)
        except OSError as exc:
            assert exc.errno == errno.EADDRINUSE
        else:
            contender.close()
            raise AssertionError("live RTAPI master accepted a second bind")
        if hasattr(socket, "AF_UNIX"):
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.connect(str(canonical))
            accepted, _ = live.accept()
            accepted.close()
            client.close()
        else:
            assert canonical in _fake_live
        assert canonical.exists()
    finally:
        live.close()

# Anti-resurrection: dropping either lifecycle environment, restoring HOME,
# adding an override/fallback, or changing the unique path must fail.
mutations = (
    source.replace(ENV_PREFIX, "TCLLIBPATH=/usr/lib/tcltk /usr/bin/linuxcnc", 1),
    source.replace(ASSIGNMENT, "RTAPI_FIFO_PATH=${V5_RTAPI_FIFO_PATH:-$HOME/.rtapi_fifo}"),
    source + "\n# $HOME/.rtapi_fifo\n",
    source.replace(ASSIGNMENT, "RTAPI_FIFO_PATH=$RUN_DIR/another_fifo"),
    source.replace(ASSIGNMENT, ASSIGNMENT + '\nrm -f "$RTAPI_FIFO_PATH"'),
)
for mutation in mutations:
    try:
        validate_init(mutation)
    except AssertionError:
        pass
    else:
        raise AssertionError("retired RTAPI FIFO path/lifecycle was reintroduced")

print("V5_RTAPI_FIFO_LIFECYCLE_SMOKE_OK")
