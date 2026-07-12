#!/usr/bin/env python3
from __future__ import annotations

import os
import struct
import tempfile
from pathlib import Path

from device_dna_register_auth import DnaRegisterError
from device_dna_register_hardware import DNA_MAP_SIZE, read_live_dna


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
        report = read_live_dna(os.fspath(fixture))
        assert report["source"] == "pl_dna_uio"
        assert report["bits"] == 57
        assert report["value"] == "0x0123456789ABCDEF"

        write_fixture(fixture, status=0)
        try:
            read_live_dna(os.fspath(fixture))
        except DnaRegisterError as exc:
            assert exc.code == "DNA_READ_FAILED"
        else:
            raise AssertionError("not-ready DNA reader must fail closed")

    print("V5_DEVICE_DNA_HARDWARE_SMOKE_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
