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
    STATUS_SHM_CMD_MCS_OFFSET,
    STATUS_SHM_DISPLAY_DIGITS_OFFSET,
    STATUS_SHM_FOLLOWING_ERROR_OFFSET,
    STATUS_SHM_FRAME_SIZE,
    STATUS_SHM_MCS_OFFSET,
    STATUS_SHM_PAYLOAD_SIZE,
    STATUS_SHM_PAYLOAD_OFFSET,
    STATUS_SHM_SCENE_BREAK_BEFORE_OFFSET,
    STATUS_SHM_SCENE_BUILD_COUNT_OFFSET,
    STATUS_SHM_SCENE_FLAGS_OFFSET,
    STATUS_SHM_SCENE_MARKER_COUNT_OFFSET,
    STATUS_SHM_SCENE_MARKERS_OFFSET,
    STATUS_SHM_SCENE_NATIVE_GENERATION_OFFSET,
    STATUS_SHM_SCENE_PLANE_OFFSET,
    STATUS_SHM_SCENE_POINT_COUNT_OFFSET,
    STATUS_SHM_SCENE_POINTS_OFFSET,
    STATUS_SHM_SCENE_PROJECT_COUNT_OFFSET,
    STATUS_SHM_SCENE_SEGMENT_COUNT_OFFSET,
    STATUS_SHM_SCENE_SEGMENTS_OFFSET,
    STATUS_SHM_SCENE_VIEW_GENERATION_OFFSET,
    STATUS_SHM_SCENE_FIT_GENERATION_OFFSET,
    STATUS_SHM_SEQ_OFFSET,
    STATUS_SHM_TRAJECTORY_OFFSET,
    STATUS_SHM_TRAJECTORY_COUNT_OFFSET,
    STATUS_SHM_UNIT_PER_COUNT_OFFSET,
    STATUS_VALID_CMD_MCS,
    STATUS_VALID_CPU_USAGE,
    STATUS_VALID_DISPLAY_SCENE,
    STATUS_VALID_MCS,
    STATUS_VALID_TRAJECTORY,
    V5StatusShmReader,
    status_shm_payload_valid,
)
from v5_ui_boot_ready import status_shm_payload_valid as boot_ready_payload_valid


NOW_NS = 10_000_000_000


def build_frame(
    *,
    seq: int = 2,
    version: int = 3,
    status_epoch: int = NOW_NS - 100_000_000,
    writer_identity: int = 17,
    source_acquired_ns: int = NOW_NS - 100_000_000,
    source_generation: int = 5,
    scene_generation: int = 5,
    valid_mask: int = STATUS_VALID_CPU_USAGE,
    cpu0: float = 12.5,
    cpu1: float = 34.5,
    generation: int = 7,
    sampled_ns: int = NOW_NS - 200_000_000,
    trajectory_count: int = 0,
    scene_point_count: int = 0,
    scene_segment_count: int = 0,
    scene_marker_count: int = 0,
    scene_plane: int = 3,
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
        STATUS_SHM_PAYLOAD_SIZE,
        0,
        seq,
        0,
    )
    struct.pack_into("<QII", raw, 32, status_epoch, valid_mask, 0)
    struct.pack_into(
        "<IIQQQ", raw, 48, writer_identity, 0,
        source_acquired_ns, source_generation, scene_generation)
    struct.pack_into("<5d", raw, STATUS_SHM_UNIT_PER_COUNT_OFFSET,
                     0.001, 0.001, 0.001, 0.0001, 0.0001)
    struct.pack_into("<5d", raw, STATUS_SHM_FOLLOWING_ERROR_OFFSET,
                     0.0, 0.0, 0.0, 0.0, 0.0)
    struct.pack_into("<5B", raw, STATUS_SHM_DISPLAY_DIGITS_OFFSET,
                     3, 3, 3, 3, 3)
    struct.pack_into("<d", raw, STATUS_SHM_CPU0_OFFSET, cpu0)
    struct.pack_into("<d", raw, STATUS_SHM_CPU1_OFFSET, cpu1)
    struct.pack_into("<Q", raw, STATUS_SHM_CPU_GENERATION_OFFSET, generation)
    struct.pack_into("<Q", raw, STATUS_SHM_CPU_SAMPLED_NS_OFFSET, sampled_ns)
    struct.pack_into(
        "<I", raw, STATUS_SHM_TRAJECTORY_COUNT_OFFSET, trajectory_count)
    struct.pack_into(
        "<I", raw, STATUS_SHM_SCENE_POINT_COUNT_OFFSET, scene_point_count)
    struct.pack_into(
        "<I", raw, STATUS_SHM_SCENE_SEGMENT_COUNT_OFFSET,
        scene_segment_count)
    struct.pack_into(
        "<I", raw, STATUS_SHM_SCENE_MARKER_COUNT_OFFSET, scene_marker_count)
    if valid_mask & STATUS_VALID_DISPLAY_SCENE:
        struct.pack_into(
            "<Q", raw, STATUS_SHM_SCENE_NATIVE_GENERATION_OFFSET,
            source_generation)
        struct.pack_into(
            "<Q", raw, STATUS_SHM_SCENE_VIEW_GENERATION_OFFSET, 1)
        struct.pack_into(
            "<Q", raw, STATUS_SHM_SCENE_FIT_GENERATION_OFFSET, 1)
        struct.pack_into(
            "<Q", raw, STATUS_SHM_SCENE_BUILD_COUNT_OFFSET, 1)
        struct.pack_into(
            "<Q", raw, STATUS_SHM_SCENE_PROJECT_COUNT_OFFSET, 1)
        struct.pack_into("<I", raw, STATUS_SHM_SCENE_FLAGS_OFFSET, 1)
        struct.pack_into(
            "<I", raw, STATUS_SHM_SCENE_PLANE_OFFSET, scene_plane)
    crc = binascii.crc32(raw[:STATUS_SHM_SEQ_OFFSET])
    crc = binascii.crc32(raw[STATUS_SHM_PAYLOAD_OFFSET:], crc) & 0xFFFFFFFF
    struct.pack_into("<I", raw, 28, crc)
    return bytes(raw)


def patch_frame(raw: bytes, offset: int, fmt: str, *values) -> bytes:
    patched = bytearray(raw)
    struct.pack_into(fmt, patched, offset, *values)
    struct.pack_into("<I", patched, 28, 0)
    crc = binascii.crc32(patched[:STATUS_SHM_SEQ_OFFSET])
    crc = binascii.crc32(
        patched[STATUS_SHM_PAYLOAD_OFFSET:], crc) & 0xFFFFFFFF
    struct.pack_into("<I", patched, 28, crc)
    return bytes(patched)


def write_frame(path: Path, raw: bytes) -> None:
    path.write_bytes(raw)


def expect_invalid(path: Path, raw: bytes) -> None:
    write_frame(path, raw)
    reader = V5StatusShmReader(path, monotonic_ns=lambda: NOW_NS)
    try:
        assert reader.read() is None
    finally:
        reader.close()


def run_smoke() -> None:
    with tempfile.TemporaryDirectory(prefix="v5-status-shm-") as temporary:
        path = Path(temporary) / "v3_status_shm"
        reader = V5StatusShmReader(path, monotonic_ns=lambda: NOW_NS)

        write_frame(path, build_frame())
        snapshot = reader.read()
        assert snapshot is not None
        assert snapshot.seq == 2
        assert snapshot.position_writer_identity == 17
        assert snapshot.source_generation == 5
        assert snapshot.scene_generation == 5
        assert snapshot.unit_per_count == (
            0.001, 0.001, 0.001, 0.0001, 0.0001)
        assert snapshot.following_error == (0.0,) * 5
        assert snapshot.display_digits == (3,) * 5
        assert snapshot.cpu0_percent == 12.5
        assert snapshot.cpu1_percent == 34.5
        assert snapshot.cpu_sample_generation == 7
        try:
            snapshot.cpu0_percent = 0.0
            raise AssertionError("snapshot must be immutable")
        except FrozenInstanceError:
            pass
        assert reader.read() is not None
        assert reader._device_inode == (
            path.stat().st_dev, path.stat().st_ino)
        reader.close()

        invalid_index = 0
        def invalid_path() -> Path:
            nonlocal invalid_index
            invalid_index += 1
            return Path(temporary) / f"invalid-{invalid_index}.bin"

        bad_crc = bytearray(build_frame())
        bad_crc[STATUS_SHM_CPU0_OFFSET] ^= 0x80
        expect_invalid(invalid_path(), bytes(bad_crc))

        expect_invalid(invalid_path(), build_frame(seq=3))
        expect_invalid(invalid_path(), build_frame(version=1))
        expect_invalid(invalid_path(), build_frame()[:-1])
        expect_invalid(
            invalid_path(),
            build_frame(
                status_epoch=NOW_NS - 5_000_000_001,
                sampled_ns=NOW_NS - 5_000_000_002,
            ),
        )
        expect_invalid(
            invalid_path(),
            build_frame(
                status_epoch=NOW_NS - 100_000_000,
                sampled_ns=NOW_NS - 5_000_000_001,
            ),
        )
        expect_invalid(invalid_path(), build_frame(cpu0=100.001))
        expect_invalid(invalid_path(), build_frame(cpu1=float("nan")))
        expect_invalid(invalid_path(), build_frame(valid_mask=0))
        expect_invalid(invalid_path(), build_frame(writer_identity=0))
        expect_invalid(invalid_path(), build_frame(source_acquired_ns=0))
        expect_invalid(invalid_path(), build_frame(source_generation=0))
        expect_invalid(invalid_path(), build_frame(trajectory_count=17))
        expect_invalid(invalid_path(), build_frame(scene_point_count=513))
        expect_invalid(invalid_path(), build_frame(scene_segment_count=49))
        expect_invalid(invalid_path(), build_frame(scene_marker_count=17))
        payload_cases = (
            patch_frame(
                build_frame(valid_mask=STATUS_VALID_CPU_USAGE |
                            STATUS_VALID_MCS),
                STATUS_SHM_MCS_OFFSET, "<d", float("nan")),
            patch_frame(
                build_frame(valid_mask=STATUS_VALID_CPU_USAGE |
                            STATUS_VALID_CMD_MCS),
                STATUS_SHM_CMD_MCS_OFFSET, "<d", float("nan")),
            patch_frame(
                build_frame(
                    valid_mask=STATUS_VALID_CPU_USAGE |
                    STATUS_VALID_TRAJECTORY,
                    trajectory_count=1),
                STATUS_SHM_TRAJECTORY_OFFSET, "<d", float("nan")),
            patch_frame(
                build_frame(
                    valid_mask=STATUS_VALID_CPU_USAGE |
                    STATUS_VALID_DISPLAY_SCENE,
                    scene_point_count=1),
                STATUS_SHM_SCENE_POINTS_OFFSET, "<f", float("nan")),
            patch_frame(
                build_frame(
                    valid_mask=STATUS_VALID_CPU_USAGE |
                    STATUS_VALID_DISPLAY_SCENE,
                    scene_point_count=1),
                STATUS_SHM_SCENE_BREAK_BEFORE_OFFSET, "<B", 2),
            patch_frame(
                build_frame(
                    valid_mask=STATUS_VALID_CPU_USAGE |
                    STATUS_VALID_DISPLAY_SCENE,
                    scene_segment_count=1),
                STATUS_SHM_SCENE_SEGMENTS_OFFSET, "<f", float("nan")),
            patch_frame(
                build_frame(
                    valid_mask=STATUS_VALID_CPU_USAGE |
                    STATUS_VALID_DISPLAY_SCENE,
                    scene_marker_count=1),
                STATUS_SHM_SCENE_MARKERS_OFFSET, "<f", float("nan")),
            build_frame(
                valid_mask=STATUS_VALID_CPU_USAGE |
                STATUS_VALID_DISPLAY_SCENE,
                scene_plane=4),
        )
        for raw in payload_cases:
            assert not status_shm_payload_valid(raw)
            assert not boot_ready_payload_valid(raw)
            expect_invalid(invalid_path(), raw)
        write_frame(path, build_frame(scene_generation=0))
        scene_optional_reader = V5StatusShmReader(
            path, monotonic_ns=lambda: NOW_NS)
        try:
            assert scene_optional_reader.read() is not None
        finally:
            scene_optional_reader.close()
        expect_invalid(
            invalid_path(),
            build_frame(
                scene_generation=0,
                valid_mask=STATUS_VALID_CPU_USAGE |
                STATUS_VALID_DISPLAY_SCENE))
        expect_invalid(invalid_path(), build_frame(generation=0))
        expect_invalid(invalid_path(), build_frame(sampled_ns=0))
        expect_invalid(
            invalid_path(),
            build_frame(
                status_epoch=NOW_NS - 200_000_000,
                sampled_ns=NOW_NS - 100_000_000,
            ),
        )

        write_frame(
            path,
            build_frame(
                generation=41, cpu0=41.0,
                source_generation=41, scene_generation=41))
        reader = V5StatusShmReader(path, monotonic_ns=lambda: NOW_NS)
        first = reader.read()
        assert first is not None and first.cpu_sample_generation == 41
        if os.name == "posix":
            replacement = path.with_suffix(".replacement")
            write_frame(
                replacement,
                build_frame(
                    generation=42, cpu0=42.0,
                    source_generation=42, scene_generation=42))
            os.replace(replacement, path)
            second = None
            for _ in range(8):
                second = reader.read()
                if second is not None and second.cpu_sample_generation == 42:
                    break
            assert second is not None
            assert second.cpu_sample_generation == 42
            assert second.cpu0_percent == 42.0
            path.unlink()
            for _ in range(7):
                reader.read()
            assert reader.read() is None
        reader.close()


if __name__ == "__main__":
    run_smoke()
    print("v5 status shm reader smoke: typed CPU projection ok")
