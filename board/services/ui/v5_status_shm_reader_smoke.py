#!/usr/bin/env python3
from __future__ import annotations

import binascii
import os
import struct
import tempfile
from dataclasses import FrozenInstanceError
from pathlib import Path

from v5_status_shm_reader import (
    STATUS_SHM_CPU0_OFFSET,
    STATUS_SHM_CPU1_OFFSET,
    STATUS_SHM_CPU_GENERATION_OFFSET,
    STATUS_SHM_CPU_SAMPLED_NS_OFFSET,
    STATUS_SHM_FRAME_SIZE,
    STATUS_SHM_PAYLOAD_OFFSET,
    STATUS_SHM_SEQ_OFFSET,
    STATUS_VALID_CPU_USAGE,
    V5StatusShmReader,
)


NOW_NS = 10_000_000_000


def build_frame(
    *,
    seq: int = 2,
    version: int = 2,
    status_epoch: int = NOW_NS - 100_000_000,
    valid_mask: int = STATUS_VALID_CPU_USAGE,
    cpu0: float = 12.5,
    cpu1: float = 34.5,
    generation: int = 7,
    sampled_ns: int = NOW_NS - 200_000_000,
) -> bytes:
    raw = bytearray(STATUS_SHM_FRAME_SIZE)
    struct.pack_into(
        "<8I",
        raw,
        0,
        0x56355348,
        version,
        STATUS_SHM_FRAME_SIZE,
        STATUS_SHM_FRAME_SIZE,
        808,
        0,
        seq,
        0,
    )
    struct.pack_into("<QII", raw, 32, status_epoch, valid_mask, 0)
    struct.pack_into("<d", raw, STATUS_SHM_CPU0_OFFSET, cpu0)
    struct.pack_into("<d", raw, STATUS_SHM_CPU1_OFFSET, cpu1)
    struct.pack_into("<Q", raw, STATUS_SHM_CPU_GENERATION_OFFSET, generation)
    struct.pack_into("<Q", raw, STATUS_SHM_CPU_SAMPLED_NS_OFFSET, sampled_ns)
    crc = binascii.crc32(raw[:STATUS_SHM_SEQ_OFFSET])
    crc = binascii.crc32(raw[STATUS_SHM_PAYLOAD_OFFSET:], crc) & 0xFFFFFFFF
    struct.pack_into("<I", raw, 28, crc)
    return bytes(raw)


def write_frame(path: Path, raw: bytes) -> None:
    path.write_bytes(raw)


def expect_invalid(path: Path, raw: bytes) -> None:
    write_frame(path, raw)
    assert V5StatusShmReader(path, monotonic_ns=lambda: NOW_NS).read() is None


def run_smoke() -> None:
    with tempfile.TemporaryDirectory(prefix="v5-status-shm-") as temporary:
        path = Path(temporary) / "v3_status_shm"
        reader = V5StatusShmReader(path, monotonic_ns=lambda: NOW_NS)

        write_frame(path, build_frame())
        snapshot = reader.read()
        assert snapshot is not None
        assert snapshot.seq == 2
        assert snapshot.cpu0_percent == 12.5
        assert snapshot.cpu1_percent == 34.5
        assert snapshot.cpu_sample_generation == 7
        try:
            snapshot.cpu0_percent = 0.0
            raise AssertionError("snapshot must be immutable")
        except FrozenInstanceError:
            pass

        expect_invalid(path, build_frame(seq=3))

        bad_crc = bytearray(build_frame())
        bad_crc[STATUS_SHM_CPU0_OFFSET] ^= 0x80
        expect_invalid(path, bytes(bad_crc))

        expect_invalid(path, build_frame(version=1))
        expect_invalid(path, build_frame()[:-1])
        expect_invalid(
            path,
            build_frame(
                status_epoch=NOW_NS - 5_000_000_001,
                sampled_ns=NOW_NS - 5_000_000_002,
            ),
        )
        expect_invalid(
            path,
            build_frame(
                status_epoch=NOW_NS - 100_000_000,
                sampled_ns=NOW_NS - 5_000_000_001,
            ),
        )
        expect_invalid(path, build_frame(cpu0=100.001))
        expect_invalid(path, build_frame(cpu1=float("nan")))
        expect_invalid(path, build_frame(valid_mask=0))
        expect_invalid(path, build_frame(generation=0))
        expect_invalid(path, build_frame(sampled_ns=0))
        expect_invalid(
            path,
            build_frame(
                status_epoch=NOW_NS - 200_000_000,
                sampled_ns=NOW_NS - 100_000_000,
            ),
        )

        write_frame(path, build_frame(generation=41, cpu0=41.0))
        first = reader.read()
        assert first is not None and first.cpu_sample_generation == 41
        replacement = path.with_suffix(".replacement")
        write_frame(replacement, build_frame(generation=42, cpu0=42.0))
        os.replace(replacement, path)
        second = reader.read()
        assert second is not None
        assert second.cpu_sample_generation == 42
        assert second.cpu0_percent == 42.0

        path.unlink()
        assert reader.read() is None


if __name__ == "__main__":
    run_smoke()
    print("v5 status shm reader smoke: typed CPU projection ok")
