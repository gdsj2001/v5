#!/usr/bin/env python3
from __future__ import annotations

import binascii
import math
import os
import struct
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


DEFAULT_STATUS_SHM_PATH = "/dev/shm/v3_status_shm"
STATUS_SHM_MAGIC = 0x56355348
STATUS_SHM_VERSION = 2
STATUS_SHM_FRAME_SIZE = 840
STATUS_SHM_PAYLOAD_SIZE = 808
STATUS_SHM_SEQ_OFFSET = 24
STATUS_SHM_CRC_OFFSET = 28
STATUS_SHM_PAYLOAD_OFFSET = 32
STATUS_SHM_CPU0_OFFSET = 808
STATUS_SHM_CPU1_OFFSET = 816
STATUS_SHM_CPU_GENERATION_OFFSET = 824
STATUS_SHM_CPU_SAMPLED_NS_OFFSET = 832
STATUS_VALID_CPU_USAGE = 1 << 8
STATUS_SHM_READ_RETRIES = 3
STATUS_SHM_MAX_AGE_NS = 5_000_000_000


@dataclass(frozen=True)
class V5StatusShmSnapshot:
    seq: int
    status_epoch: int
    cpu0_percent: float
    cpu1_percent: float
    cpu_sample_generation: int
    cpu_sample_monotonic_ns: int


class V5StatusShmReader:
    """Read the typed CPU projection without retaining an fd or stale data."""

    def __init__(
        self,
        path: str | os.PathLike[str] = DEFAULT_STATUS_SHM_PATH,
        *,
        monotonic_ns: Callable[[], int] = time.monotonic_ns,
    ) -> None:
        self.path = Path(path)
        self._monotonic_ns = monotonic_ns
        self._lock = threading.Lock()

    @staticmethod
    def _pread(fd: int, size: int, offset: int) -> bytes:
        if hasattr(os, "pread"):
            return os.pread(fd, size, offset)
        os.lseek(fd, offset, os.SEEK_SET)
        return os.read(fd, size)

    @staticmethod
    def _decode(raw: bytes, now_ns: int) -> V5StatusShmSnapshot | None:
        if len(raw) != STATUS_SHM_FRAME_SIZE:
            return None

        header = struct.unpack_from("<8I", raw, 0)
        magic, version, header_size, total_size, payload_size = header[:5]
        seq, stored_crc = header[6:8]
        if (
            magic != STATUS_SHM_MAGIC
            or version != STATUS_SHM_VERSION
            or header_size != STATUS_SHM_FRAME_SIZE
            or total_size != STATUS_SHM_FRAME_SIZE
            or payload_size != STATUS_SHM_PAYLOAD_SIZE
            or seq == 0
            or seq & 1
        ):
            return None

        calculated_crc = binascii.crc32(raw[:STATUS_SHM_SEQ_OFFSET])
        calculated_crc = binascii.crc32(
            raw[STATUS_SHM_PAYLOAD_OFFSET:], calculated_crc
        ) & 0xFFFFFFFF
        if stored_crc != calculated_crc:
            return None

        status_epoch, typed_valid_mask = struct.unpack_from("<QI", raw, 32)
        if not typed_valid_mask & STATUS_VALID_CPU_USAGE:
            return None

        cpu0_percent = struct.unpack_from("<d", raw, STATUS_SHM_CPU0_OFFSET)[0]
        cpu1_percent = struct.unpack_from("<d", raw, STATUS_SHM_CPU1_OFFSET)[0]
        cpu_sample_generation = struct.unpack_from(
            "<Q", raw, STATUS_SHM_CPU_GENERATION_OFFSET
        )[0]
        cpu_sample_monotonic_ns = struct.unpack_from(
            "<Q", raw, STATUS_SHM_CPU_SAMPLED_NS_OFFSET
        )[0]
        if (
            not math.isfinite(cpu0_percent)
            or not 0.0 <= cpu0_percent <= 100.0
            or not math.isfinite(cpu1_percent)
            or not 0.0 <= cpu1_percent <= 100.0
            or status_epoch == 0
            or cpu_sample_generation == 0
            or cpu_sample_monotonic_ns == 0
            or cpu_sample_monotonic_ns > status_epoch
            or status_epoch > now_ns
            or cpu_sample_monotonic_ns > now_ns
            or now_ns - status_epoch > STATUS_SHM_MAX_AGE_NS
            or now_ns - cpu_sample_monotonic_ns > STATUS_SHM_MAX_AGE_NS
        ):
            return None

        return V5StatusShmSnapshot(
            seq=seq,
            status_epoch=status_epoch,
            cpu0_percent=cpu0_percent,
            cpu1_percent=cpu1_percent,
            cpu_sample_generation=cpu_sample_generation,
            cpu_sample_monotonic_ns=cpu_sample_monotonic_ns,
        )

    def read(self) -> V5StatusShmSnapshot | None:
        """Return one fresh immutable snapshot, or ``None`` on any failure."""

        with self._lock:
            try:
                fd = os.open(
                    self.path,
                    os.O_RDONLY | getattr(os, "O_CLOEXEC", 0),
                )
            except OSError:
                return None
            try:
                if os.fstat(fd).st_size != STATUS_SHM_FRAME_SIZE:
                    return None
                for _ in range(STATUS_SHM_READ_RETRIES):
                    before_raw = self._pread(fd, 4, STATUS_SHM_SEQ_OFFSET)
                    raw = self._pread(fd, STATUS_SHM_FRAME_SIZE, 0)
                    after_raw = self._pread(fd, 4, STATUS_SHM_SEQ_OFFSET)
                    if len(before_raw) != 4 or len(after_raw) != 4:
                        continue
                    before_seq = struct.unpack("<I", before_raw)[0]
                    after_seq = struct.unpack("<I", after_raw)[0]
                    if before_seq == 0 or before_seq & 1:
                        continue
                    if before_seq != after_seq or len(raw) != STATUS_SHM_FRAME_SIZE:
                        continue
                    if struct.unpack_from("<I", raw, STATUS_SHM_SEQ_OFFSET)[0] != before_seq:
                        continue
                    snapshot = self._decode(raw, int(self._monotonic_ns()))
                    if snapshot is not None:
                        return snapshot
                return None
            except OSError:
                return None
            finally:
                os.close(fd)


def read_status_shm_snapshot(
    path: str | os.PathLike[str] = DEFAULT_STATUS_SHM_PATH,
    *,
    monotonic_ns: Callable[[], int] = time.monotonic_ns,
) -> V5StatusShmSnapshot | None:
    return V5StatusShmReader(path, monotonic_ns=monotonic_ns).read()
