#!/usr/bin/env python3
from __future__ import annotations

import binascii
import math
import mmap
import os
import struct
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


DEFAULT_STATUS_SHM_PATH = "/dev/shm/v3_status_shm"
STATUS_SHM_MAGIC = 0x56355348
STATUS_SHM_VERSION = 3
STATUS_SHM_FRAME_SIZE = 7128
STATUS_SHM_PAYLOAD_SIZE = 7096
STATUS_SHM_SEQ_OFFSET = 24
STATUS_SHM_CRC_OFFSET = 28
STATUS_SHM_PAYLOAD_OFFSET = 32
STATUS_SHM_UNIT_PER_COUNT_OFFSET = 160
STATUS_SHM_FOLLOWING_ERROR_OFFSET = 200
STATUS_SHM_DISPLAY_DIGITS_OFFSET = 240
STATUS_SHM_MCS_OFFSET = 80
STATUS_SHM_CMD_MCS_OFFSET = 120
STATUS_SHM_TRAJECTORY_OFFSET = 248
STATUS_SHM_CPU0_OFFSET = 928
STATUS_SHM_CPU1_OFFSET = 936
STATUS_SHM_CPU_GENERATION_OFFSET = 944
STATUS_SHM_CPU_SAMPLED_NS_OFFSET = 952
STATUS_SHM_SCENE_OFFSET = 960
STATUS_SHM_SCENE_NATIVE_GENERATION_OFFSET = 976
STATUS_SHM_SCENE_VIEW_GENERATION_OFFSET = 1008
STATUS_SHM_SCENE_FIT_GENERATION_OFFSET = 1016
STATUS_SHM_SCENE_BUILD_COUNT_OFFSET = 1024
STATUS_SHM_SCENE_PROJECT_COUNT_OFFSET = 1040
STATUS_SHM_SCENE_FLAGS_OFFSET = 1052
STATUS_SHM_SCENE_POINT_COUNT_OFFSET = 1056
STATUS_SHM_SCENE_SEGMENT_COUNT_OFFSET = 1060
STATUS_SHM_SCENE_MARKER_COUNT_OFFSET = 1064
STATUS_SHM_SCENE_PROGRAM_WCS_MASK_OFFSET = 1068
STATUS_SHM_SCENE_CURRENT_WCS_INDEX_OFFSET = 1082
STATUS_SHM_SCENE_PLANE_OFFSET = 1084
STATUS_SHM_SCENE_PRIMARY_CENTER_OFFSET = 1088
STATUS_SHM_SCENE_CHILD_CENTER_OFFSET = 1100
STATUS_SHM_SCENE_POINTS_OFFSET = 1112
STATUS_SHM_SCENE_BREAK_BEFORE_OFFSET = 5208
STATUS_SHM_SCENE_SEGMENTS_OFFSET = 5720
STATUS_SHM_SCENE_MARKERS_OFFSET = 6872
STATUS_SHM_TRAJECTORY_COUNT_OFFSET = 888
STATUS_TRAJECTORY_POINT_COUNT = 16
STATUS_SCENE_POINT_COUNT = 512
STATUS_SCENE_SEGMENT_COUNT = 48
STATUS_SCENE_MARKER_COUNT = 16
STATUS_VALID_MCS = 1 << 0
STATUS_VALID_CMD_MCS = 1 << 1
STATUS_VALID_TRAJECTORY = 1 << 2
STATUS_VALID_SPINDLE_SPEED = 1 << 4
STATUS_VALID_LINEAR_VELOCITY = 1 << 5
STATUS_VALID_FEED_OVERRIDE = 1 << 6
STATUS_VALID_SPINDLE_OVERRIDE = 1 << 7
STATUS_VALID_CPU_USAGE = 1 << 8
STATUS_VALID_DISPLAY_SCENE = 1 << 9
STATUS_KNOWN_VALID_MASK = (
    STATUS_VALID_MCS | STATUS_VALID_CMD_MCS | STATUS_VALID_TRAJECTORY |
    STATUS_VALID_SPINDLE_SPEED | STATUS_VALID_LINEAR_VELOCITY |
    STATUS_VALID_FEED_OVERRIDE | STATUS_VALID_SPINDLE_OVERRIDE |
    STATUS_VALID_CPU_USAGE | STATUS_VALID_DISPLAY_SCENE)
STATUS_SCENE_FLAG_VALID = 1 << 0
STATUS_SCENE_FLAG_MODEL = 1 << 3
STATUS_SCENE_PLANE_3D = 3
STATUS_SHM_READ_RETRIES = 3
STATUS_SHM_MAX_AGE_NS = 5_000_000_000


@dataclass(frozen=True)
class V5StatusShmSnapshot:
    seq: int
    status_epoch: int
    position_writer_identity: int
    source_acquired_mono_ns: int
    source_generation: int
    scene_generation: int
    unit_per_count: tuple[float, ...]
    following_error: tuple[float, ...]
    display_digits: tuple[int, ...]
    cpu0_percent: float
    cpu1_percent: float
    cpu_sample_generation: int
    cpu_sample_monotonic_ns: int


def _finite_values(raw: bytes, offset: int, count: int, code: str) -> bool:
    return all(math.isfinite(value) for value in struct.unpack_from(
        f"<{count}{code}", raw, offset))


def status_shm_payload_valid(raw: bytes) -> bool:
    """Apply the C reader's fail-closed typed payload contract."""
    if len(raw) != STATUS_SHM_FRAME_SIZE:
        return False
    typed_valid_mask = struct.unpack_from("<I", raw, 40)[0]
    writer_identity = struct.unpack_from("<I", raw, 48)[0]
    source_time, source_generation, scene_generation = struct.unpack_from(
        "<QQQ", raw, 56)
    trajectory_count = struct.unpack_from(
        "<I", raw, STATUS_SHM_TRAJECTORY_COUNT_OFFSET)[0]
    point_count = struct.unpack_from(
        "<I", raw, STATUS_SHM_SCENE_POINT_COUNT_OFFSET)[0]
    segment_count = struct.unpack_from(
        "<I", raw, STATUS_SHM_SCENE_SEGMENT_COUNT_OFFSET)[0]
    marker_count = struct.unpack_from(
        "<I", raw, STATUS_SHM_SCENE_MARKER_COUNT_OFFSET)[0]
    if (
        writer_identity == 0
        or source_time == 0
        or source_generation == 0
        or typed_valid_mask & ~STATUS_KNOWN_VALID_MASK
        or trajectory_count > STATUS_TRAJECTORY_POINT_COUNT
        or point_count > STATUS_SCENE_POINT_COUNT
        or segment_count > STATUS_SCENE_SEGMENT_COUNT
        or marker_count > STATUS_SCENE_MARKER_COUNT
    ):
        return False
    if typed_valid_mask & (STATUS_VALID_MCS | STATUS_VALID_CMD_MCS):
        unit_per_count = struct.unpack_from(
            "<5d", raw, STATUS_SHM_UNIT_PER_COUNT_OFFSET)
        following_error = struct.unpack_from(
            "<5d", raw, STATUS_SHM_FOLLOWING_ERROR_OFFSET)
        display_digits = struct.unpack_from(
            "<5B", raw, STATUS_SHM_DISPLAY_DIGITS_OFFSET)
        if (
            not all(math.isfinite(value) and value > 0.0
                    for value in unit_per_count)
            or not all(math.isfinite(value) for value in following_error)
            or display_digits != (3, 3, 3, 3, 3)
        ):
            return False
    if (typed_valid_mask & STATUS_VALID_MCS and
            not _finite_values(raw, STATUS_SHM_MCS_OFFSET, 5, "d")):
        return False
    if (typed_valid_mask & STATUS_VALID_CMD_MCS and
            not _finite_values(raw, STATUS_SHM_CMD_MCS_OFFSET, 5, "d")):
        return False
    if typed_valid_mask & STATUS_VALID_TRAJECTORY:
        for index in range(trajectory_count):
            if not _finite_values(
                    raw, STATUS_SHM_TRAJECTORY_OFFSET + index * 40, 5, "d"):
                return False
    scalar_fields = (
        (STATUS_VALID_SPINDLE_SPEED, 896),
        (STATUS_VALID_LINEAR_VELOCITY, 904),
        (STATUS_VALID_FEED_OVERRIDE, 912),
        (STATUS_VALID_SPINDLE_OVERRIDE, 920),
    )
    for valid_bit, offset in scalar_fields:
        if (typed_valid_mask & valid_bit and
                not _finite_values(raw, offset, 1, "d")):
            return False
    if typed_valid_mask & STATUS_VALID_CPU_USAGE:
        cpu0, cpu1 = struct.unpack_from("<2d", raw, STATUS_SHM_CPU0_OFFSET)
        generation, sampled_ns = struct.unpack_from(
            "<QQ", raw, STATUS_SHM_CPU_GENERATION_OFFSET)
        if (
            not math.isfinite(cpu0) or not 0.0 <= cpu0 <= 100.0
            or not math.isfinite(cpu1) or not 0.0 <= cpu1 <= 100.0
            or generation == 0 or sampled_ns == 0
        ):
            return False
    if typed_valid_mask & STATUS_VALID_DISPLAY_SCENE:
        native_generation = struct.unpack_from(
            "<Q", raw, STATUS_SHM_SCENE_NATIVE_GENERATION_OFFSET)[0]
        view_generation = struct.unpack_from(
            "<Q", raw, STATUS_SHM_SCENE_VIEW_GENERATION_OFFSET)[0]
        fit_generation = struct.unpack_from(
            "<Q", raw, STATUS_SHM_SCENE_FIT_GENERATION_OFFSET)[0]
        build_count = struct.unpack_from(
            "<Q", raw, STATUS_SHM_SCENE_BUILD_COUNT_OFFSET)[0]
        project_count = struct.unpack_from(
            "<Q", raw, STATUS_SHM_SCENE_PROJECT_COUNT_OFFSET)[0]
        scene_flags = struct.unpack_from(
            "<I", raw, STATUS_SHM_SCENE_FLAGS_OFFSET)[0]
        wcs_mask = struct.unpack_from(
            "<I", raw, STATUS_SHM_SCENE_PROGRAM_WCS_MASK_OFFSET)[0]
        current_wcs = raw[STATUS_SHM_SCENE_CURRENT_WCS_INDEX_OFFSET]
        scene_plane = struct.unpack_from(
            "<I", raw, STATUS_SHM_SCENE_PLANE_OFFSET)[0]
        if (
            scene_generation == 0
            or native_generation == 0
            or view_generation == 0
            or fit_generation == 0
            or build_count == 0
            or project_count == 0
            or not scene_flags & STATUS_SCENE_FLAG_VALID
            or scene_plane > STATUS_SCENE_PLANE_3D
            or wcs_mask & ~0x1FF
            or current_wcs > 8 and current_wcs != 255
        ):
            return False
        for index in range(point_count):
            if (not _finite_values(
                    raw, STATUS_SHM_SCENE_POINTS_OFFSET + index * 8, 2, "f")
                    or raw[STATUS_SHM_SCENE_BREAK_BEFORE_OFFSET + index] > 1):
                return False
        for index in range(segment_count):
            if not _finite_values(
                    raw, STATUS_SHM_SCENE_SEGMENTS_OFFSET + index * 24,
                    4, "f"):
                return False
        for index in range(marker_count):
            if not _finite_values(
                    raw, STATUS_SHM_SCENE_MARKERS_OFFSET + index * 16,
                    2, "f"):
                return False
        if scene_flags & STATUS_SCENE_FLAG_MODEL:
            if (not _finite_values(
                    raw, STATUS_SHM_SCENE_PRIMARY_CENTER_OFFSET, 3, "f")
                    or not _finite_values(
                        raw, STATUS_SHM_SCENE_CHILD_CENTER_OFFSET, 3, "f")):
                return False
    return True


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
        self._fd = -1
        self._mapping: mmap.mmap | None = None
        self._device_inode: tuple[int, int] | None = None
        self._failure_count = 0
        self._last_source_generation = 0
        self._stagnant_count = 0

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
        position_writer_identity = struct.unpack_from("<I", raw, 48)[0]
        source_acquired_mono_ns, source_generation, scene_generation = (
            struct.unpack_from("<QQQ", raw, 56))
        trajectory_count = struct.unpack_from(
            "<I", raw, STATUS_SHM_TRAJECTORY_COUNT_OFFSET)[0]
        scene_point_count = struct.unpack_from(
            "<I", raw, STATUS_SHM_SCENE_POINT_COUNT_OFFSET)[0]
        scene_segment_count = struct.unpack_from(
            "<I", raw, STATUS_SHM_SCENE_SEGMENT_COUNT_OFFSET)[0]
        scene_marker_count = struct.unpack_from(
            "<I", raw, STATUS_SHM_SCENE_MARKER_COUNT_OFFSET)[0]
        if (not status_shm_payload_valid(raw) or
                not typed_valid_mask & STATUS_VALID_CPU_USAGE):
            return None
        unit_per_count = struct.unpack_from(
            "<5d", raw, STATUS_SHM_UNIT_PER_COUNT_OFFSET)
        following_error = struct.unpack_from(
            "<5d", raw, STATUS_SHM_FOLLOWING_ERROR_OFFSET)
        display_digits = struct.unpack_from(
            "<5B", raw, STATUS_SHM_DISPLAY_DIGITS_OFFSET)

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
            or source_acquired_mono_ns > now_ns
            or now_ns - source_acquired_mono_ns > STATUS_SHM_MAX_AGE_NS
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
            position_writer_identity=position_writer_identity,
            source_acquired_mono_ns=source_acquired_mono_ns,
            source_generation=source_generation,
            scene_generation=scene_generation,
            unit_per_count=unit_per_count,
            following_error=following_error,
            display_digits=display_digits,
            cpu0_percent=cpu0_percent,
            cpu1_percent=cpu1_percent,
            cpu_sample_generation=cpu_sample_generation,
            cpu_sample_monotonic_ns=cpu_sample_monotonic_ns,
        )

    def close(self) -> None:
        if self._mapping is not None:
            self._mapping.close()
            self._mapping = None
        if self._fd >= 0:
            os.close(self._fd)
            self._fd = -1
        self._device_inode = None

    def _open(self) -> bool:
        if self._mapping is not None:
            return True
        try:
            fd = os.open(
                self.path, os.O_RDONLY | getattr(os, "O_CLOEXEC", 0))
            status = os.fstat(fd)
            if status.st_size != STATUS_SHM_FRAME_SIZE:
                os.close(fd)
                return False
            mapping = mmap.mmap(fd, STATUS_SHM_FRAME_SIZE, access=mmap.ACCESS_READ)
        except OSError:
            return False
        self._fd = fd
        self._mapping = mapping
        self._device_inode = (status.st_dev, status.st_ino)
        return True

    def _backing_matches(self) -> bool:
        if self._fd < 0 or self._device_inode is None:
            return False
        try:
            descriptor = os.fstat(self._fd)
            path = self.path.stat()
        except OSError:
            return False
        return (
            descriptor.st_size == STATUS_SHM_FRAME_SIZE
            and path.st_size == STATUS_SHM_FRAME_SIZE
            and (descriptor.st_dev, descriptor.st_ino) == self._device_inode
            and (path.st_dev, path.st_ino) == self._device_inode
        )

    def read(self) -> V5StatusShmSnapshot | None:
        """Return one fresh immutable snapshot using one persistent mapping."""

        with self._lock:
            if not self._open() or self._mapping is None:
                return None
            try:
                for _ in range(STATUS_SHM_READ_RETRIES):
                    before_seq = struct.unpack_from(
                        "<I", self._mapping, STATUS_SHM_SEQ_OFFSET)[0]
                    if before_seq == 0 or before_seq & 1:
                        continue
                    raw = self._mapping[:]
                    after_seq = struct.unpack_from(
                        "<I", self._mapping, STATUS_SHM_SEQ_OFFSET)[0]
                    if before_seq != after_seq:
                        continue
                    if struct.unpack_from(
                            "<I", raw, STATUS_SHM_SEQ_OFFSET)[0] != before_seq:
                        continue
                    snapshot = self._decode(raw, int(self._monotonic_ns()))
                    if snapshot is not None:
                        self._failure_count = 0
                        if snapshot.source_generation == self._last_source_generation:
                            self._stagnant_count += 1
                        else:
                            self._last_source_generation = snapshot.source_generation
                            self._stagnant_count = 0
                        if self._stagnant_count >= 6:
                            self._stagnant_count = 0
                            if not self._backing_matches():
                                self.close()
                                return None
                        return snapshot
            except (OSError, ValueError):
                pass
            self._failure_count += 1
            if self._failure_count >= STATUS_SHM_READ_RETRIES:
                self._failure_count = 0
                if not self._backing_matches():
                    self.close()
            return None


def read_status_shm_snapshot(
    path: str | os.PathLike[str] = DEFAULT_STATUS_SHM_PATH,
    *,
    monotonic_ns: Callable[[], int] = time.monotonic_ns,
) -> V5StatusShmSnapshot | None:
    reader = V5StatusShmReader(path, monotonic_ns=monotonic_ns)
    try:
        return reader.read()
    finally:
        reader.close()
