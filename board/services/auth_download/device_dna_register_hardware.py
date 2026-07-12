from __future__ import annotations

import mmap
import os
import struct
from typing import Any, Dict, Tuple

from device_dna_register_auth import DnaRegisterError

DEFAULT_UIO_DEVICE = "/dev/v5-dna-uio"
DNA_MAP_SIZE = 0x10000
EXPECTED_DNA_BITS = 57
EXPECTED_MAGIC = 0x444E4130
EXPECTED_VERSION = 0x00010000
EXPECTED_STATUS = 0x00000007
DNA_HI_MASK = (1 << (EXPECTED_DNA_BITS - 32)) - 1


def _read_register_snapshot(registers: mmap.mmap) -> Tuple[int, int, int, int, int, int]:
    magic, version, status, bits, lo, hi = struct.unpack_from("<6I", registers, 0)
    status_after = struct.unpack_from("<I", registers, 8)[0]
    if status != status_after:
        raise ValueError("DNA reader status changed during readback")
    return magic, version, status, bits, lo, hi


def read_live_dna(device_path: str = DEFAULT_UIO_DEVICE) -> Dict[str, Any]:
    try:
        fd = os.open(device_path, os.O_RDWR | getattr(os, "O_CLOEXEC", 0))
        try:
            with mmap.mmap(fd, DNA_MAP_SIZE, access=mmap.ACCESS_READ) as registers:
                snapshots = [_read_register_snapshot(registers) for _ in range(3)]
        finally:
            os.close(fd)

        if len(set(snapshots)) != 1:
            raise ValueError("live DNA register snapshots were inconsistent")
        magic, version, status, bits, lo, hi = snapshots[0]
        if magic != EXPECTED_MAGIC:
            raise ValueError("unexpected DNA reader magic")
        if version != EXPECTED_VERSION:
            raise ValueError("unexpected DNA reader version")
        if status != EXPECTED_STATUS:
            raise ValueError("DNA reader is not ready")
        if bits != EXPECTED_DNA_BITS:
            raise ValueError("unexpected DNA bit count")
        if hi & ~DNA_HI_MASK:
            raise ValueError("DNA high word exceeds 57 bits")

        dna_value = (hi << 32) | lo
        if dna_value == 0:
            raise ValueError("DNA value is zero")
        return {
            "value": "0x%016X" % dna_value,
            "source": "pl_dna_uio",
            "type": "zynq7000_pl_device_dna_57",
            "bits": bits,
            "status_hex": "0x%08X" % status,
        }
    except DnaRegisterError:
        raise
    except Exception as exc:
        raise DnaRegisterError(
            "DNA_READ_FAILED",
            "本机 PL Device DNA live UIO 读取失败: %s" % type(exc).__name__,
        ) from exc
