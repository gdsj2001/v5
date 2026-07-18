#!/usr/bin/env python3
from __future__ import annotations

import os
import stat
import struct
import tempfile
import types
from pathlib import Path

from device_dna_register_auth import DnaRegisterError
import device_dna_register_hardware as hardware
from device_dna_register_hardware import DNA_MAP_SIZE, _read_live_dna_fd


def write_fixture(path: Path, *, status: int = 0x00000007) -> None:
    payload = bytearray(DNA_MAP_SIZE)
    struct.pack_into(
        "<6I",
        payload,
        0,
        0x444E4130,
        0x00010000,
        status,
        57,
        0x89ABCDEF,
        0x01234567,
    )
    path.write_bytes(payload)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="v5-dna-uio-") as temp_dir:
        fixture = Path(temp_dir) / "dna-uio.bin"
        write_fixture(fixture)
        fd = os.open(os.fspath(fixture), os.O_RDONLY)
        try:
            report = _read_live_dna_fd(fd)
        finally:
            os.close(fd)
        assert report["source"] == "pl_dna_uio"
        assert report["bits"] == 57
        assert report["value"] == "0x0123456789ABCDEF"

        write_fixture(fixture, status=0)
        try:
            fd = os.open(os.fspath(fixture), os.O_RDONLY)
            try:
                _read_live_dna_fd(fd)
            finally:
                os.close(fd)
        except DnaRegisterError as exc:
            assert exc.code == "DNA_READ_FAILED"
        else:
            raise AssertionError("not-ready DNA reader must fail closed")

        original_fstat = hardware.os.fstat
        try:
            hardware.os.fstat = lambda _fd: types.SimpleNamespace(
                st_mode=stat.S_IFCHR | 0o600, st_uid=0, st_gid=0,
                st_dev=11, st_ino=22, st_rdev=33)
            try:
                hardware._validate_dna_uio_fd(123, (11, 99, 33))
            except ValueError as exc:
                assert "identity changed" in str(exc)
            else:
                raise AssertionError("same-permission replacement device must fail")
        finally:
            hardware.os.fstat = original_fstat

    print("V5_DEVICE_DNA_HARDWARE_SMOKE_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
