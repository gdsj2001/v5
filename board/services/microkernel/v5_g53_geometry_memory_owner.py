#!/usr/bin/env python3
import argparse
import ctypes
import math
import os
import resource
import struct
import sys
import time
from typing import Dict, List


def lock_process_memory(process_name: str) -> None:
    try:
        soft, hard = resource.getrlimit(resource.RLIMIT_MEMLOCK)
        target = hard if hard != resource.RLIM_INFINITY else resource.RLIM_INFINITY
        if soft != target:
            resource.setrlimit(resource.RLIMIT_MEMLOCK, (target, hard))
    except Exception:
        pass
    libc = ctypes.CDLL("libc.so.6", use_errno=True)
    if libc.mlockall(1 | 2) != 0:
        err = ctypes.get_errno()
        raise SystemExit(f"{process_name} mlockall(MCL_CURRENT|MCL_FUTURE) failed: errno={err}")


MAGIC = 0x56354753
VERSION = 1
CENTER_COUNT = 3
AXIS_COUNT = 3
VALUE_COUNT = CENTER_COUNT * AXIS_COUNT
BLOCK_STRUCT = struct.Struct("<IIIIIIIIQ" + ("d" * VALUE_COUNT) + "II")
PREFIX_STRUCT = struct.Struct("<IIIIIIIIQ" + ("d" * VALUE_COUNT))
DEFAULT_PATH = "/dev/shm/v5_native_g53_geometry_status.bin"
DEFAULT_INI = "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini"
DEFAULT_INTERVAL_MS = 100
REQUIRED_KEYS = ("G53_A_Y", "G53_A_Z", "G53_B_X", "G53_B_Z", "G53_C_X", "G53_C_Y")


def crc32_like(prefix: bytes) -> int:
    value = 2166136261
    for byte in prefix:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def flatten_centers(centers: List[List[float]]) -> List[float]:
    values: List[float] = []
    for center in centers:
        values.extend(float(value) for value in center)
    if len(values) != VALUE_COUNT or not all(math.isfinite(value) for value in values):
        raise ValueError("g53_geometry_values_invalid")
    return values


def geometry_epoch(centers: List[List[float]]) -> int:
    packed = struct.pack("<" + ("d" * VALUE_COUNT), *flatten_centers(centers))
    epoch = crc32_like(packed)
    return epoch or 1


def atomic_write(path: str, payload: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f"{path}.tmp.{os.getpid()}"
    with open(tmp, "wb") as fp:
        fp.write(payload)
        fp.flush()
        os.fsync(fp.fileno())
    os.replace(tmp, path)


def write_status(path: str, valid: int, centers: List[List[float]], epoch: int) -> None:
    values = flatten_centers(centers)
    valid_value = 1 if valid and epoch else 0
    epoch_value = int(epoch) if valid_value else 0
    monotonic_ns = time.monotonic_ns()
    prefix = PREFIX_STRUCT.pack(
        MAGIC,
        VERSION,
        BLOCK_STRUCT.size,
        valid_value,
        CENTER_COUNT,
        AXIS_COUNT,
        epoch_value,
        0,
        monotonic_ns,
        *values,
    )
    payload = BLOCK_STRUCT.pack(
        MAGIC,
        VERSION,
        BLOCK_STRUCT.size,
        valid_value,
        CENTER_COUNT,
        AXIS_COUNT,
        epoch_value,
        0,
        monotonic_ns,
        *values,
        crc32_like(prefix),
        0,
    )
    atomic_write(path, payload)


def parse_value(raw: str, key: str) -> float:
    value = float(raw.split("#", 1)[0].split(";", 1)[0].strip())
    if not math.isfinite(value):
        raise ValueError(f"{key}_not_finite")
    return value


def read_rtcp_geometry_from_ini(ini_path: str) -> List[List[float]]:
    values: Dict[str, float] = {}
    section = ""
    with open(ini_path, "r", encoding="utf-8", errors="replace") as fp:
        for raw in fp:
            line = raw.strip()
            if not line or line.startswith(("#", ";")):
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1].strip().upper()
                continue
            if section != "RTCP" or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip().upper()
            if key in REQUIRED_KEYS:
                values[key] = parse_value(value, key)

    missing = [key for key in REQUIRED_KEYS if key not in values]
    if missing:
        raise SystemExit(f"g53_geometry_ini_missing_keys: {','.join(missing)} source={ini_path}")

    return [
        [0.0, values["G53_A_Y"], values["G53_A_Z"]],
        [values["G53_B_X"], 0.0, values["G53_B_Z"]],
        [values["G53_C_X"], values["G53_C_Y"], 0.0],
    ]


def parse_mock_centers(text: str) -> List[List[float]]:
    parts = [float(part.strip()) for part in text.split(",") if part.strip()]
    if len(parts) != VALUE_COUNT:
        raise SystemExit(f"mock_centers_requires_{VALUE_COUNT}_values")
    centers = [parts[0:3], parts[3:6], parts[6:9]]
    flatten_centers(centers)
    return centers


lock_process_memory("v5_g53_geometry_memory_owner")


def main() -> int:
    parser = argparse.ArgumentParser(description="Load RTCP G53 geometry into v5 native microkernel parameter memory.")
    parser.add_argument("--path", default=os.environ.get("V5_G53_GEOMETRY_STATUS_PATH", DEFAULT_PATH))
    parser.add_argument("--ini", default=os.environ.get("V5_LINUXCNC_INI", os.environ.get("INI_FILE_NAME", DEFAULT_INI)))
    parser.add_argument("--interval-ms", type=int, default=int(os.environ.get("V5_G53_GEOMETRY_INTERVAL_MS", DEFAULT_INTERVAL_MS)))
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--mock-centers", default="")
    args = parser.parse_args()

    centers = parse_mock_centers(args.mock_centers) if args.mock_centers else read_rtcp_geometry_from_ini(args.ini)
    epoch = geometry_epoch(centers)
    source = "mock" if args.mock_centers else args.ini
    interval = max(args.interval_ms, 20) / 1000.0
    print(f"v5_g53_geometry_memory_owner loaded source={source} epoch={epoch} path={args.path}", flush=True)

    while True:
        write_status(args.path, 1, centers, epoch)
        if args.once or args.verbose:
            print(
                "v5_g53_geometry_memory_owner valid=1 "
                f"A={centers[0][0]:.6g},{centers[0][1]:.6g},{centers[0][2]:.6g} "
                f"B={centers[1][0]:.6g},{centers[1][1]:.6g},{centers[1][2]:.6g} "
                f"C={centers[2][0]:.6g},{centers[2][1]:.6g},{centers[2][2]:.6g}",
                flush=True,
            )
        if args.once:
            return 0
        time.sleep(interval)


if __name__ == "__main__":
    raise SystemExit(main())
