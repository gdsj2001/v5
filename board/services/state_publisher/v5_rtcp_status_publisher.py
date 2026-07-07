#!/usr/bin/env python3
import argparse
import os
import struct
import sys
import time
import ctypes
import resource
from typing import Iterable, Tuple



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

MAGIC = 0x56525443
VERSION = 1
BLOCK_STRUCT = struct.Struct('<IIIIIIQII')
DEFAULT_PATH = '/dev/shm/v5_native_rtcp_status.bin'
DEFAULT_INTERVAL_MS = 100


def crc32_like(prefix: bytes) -> int:
    value = 2166136261
    for b in prefix:
        value ^= b
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def status_from_mcodes(mcodes: Iterable[int]) -> Tuple[int, int]:
    saw_m128 = False
    saw_m129 = False
    for code in mcodes:
        if code == 128:
            saw_m128 = True
        elif code == 129:
            saw_m129 = True
    if saw_m128 == saw_m129:
        return 0, 0
    return 1, 1 if saw_m128 else 0


def write_status(path: str, valid: int, active: int) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    monotonic_ns = time.monotonic_ns()
    prefix = struct.pack('<IIIIIIQ', MAGIC, VERSION, BLOCK_STRUCT.size, 1 if valid else 0, 1 if active else 0, 0, monotonic_ns)
    crc = crc32_like(prefix)
    payload = BLOCK_STRUCT.pack(MAGIC, VERSION, BLOCK_STRUCT.size, 1 if valid else 0, 1 if active else 0, 0, monotonic_ns, crc, 0)
    tmp = f'{path}.tmp.{os.getpid()}'
    with open(tmp, 'wb') as fp:
        fp.write(payload)
        fp.flush()
        os.fsync(fp.fileno())
    os.replace(tmp, path)


def parse_mock(text: str):
    if not text:
        return None
    return [int(part.strip()) for part in text.split(',') if part.strip()]


def load_linuxcnc():
    dist = '/usr/lib/python3/dist-packages'
    if os.path.isdir(dist) and dist not in sys.path:
        sys.path.insert(0, dist)
    import linuxcnc  # type: ignore
    return linuxcnc


def poll_once(stat, path: str) -> Tuple[int, int]:
    stat.poll()
    valid, active = status_from_mcodes(getattr(stat, 'mcodes', ()))
    write_status(path, valid, active)
    return valid, active


lock_process_memory("v5_rtcp_status_publisher")

def main() -> int:
    parser = argparse.ArgumentParser(description='Publish v5 RTCP actual status from LinuxCNC native mcodes.')
    parser.add_argument('--path', default=DEFAULT_PATH)
    parser.add_argument('--interval-ms', type=int, default=DEFAULT_INTERVAL_MS)
    parser.add_argument('--once', action='store_true')
    parser.add_argument('--mock-mcodes', default='')
    args = parser.parse_args()

    mock = parse_mock(args.mock_mcodes)
    if mock is not None:
        valid, active = status_from_mcodes(mock)
        write_status(args.path, valid, active)
        print(f'v5_rtcp_status_publisher mock valid={valid} active={active} path={args.path}')
        return 0

    linuxcnc = load_linuxcnc()
    stat = linuxcnc.stat()
    interval = max(args.interval_ms, 20) / 1000.0
    while True:
        try:
            valid, active = poll_once(stat, args.path)
            print(f'v5_rtcp_status_publisher valid={valid} active={active}', flush=True)
        except Exception as exc:
            write_status(args.path, 0, 0)
            print(f'v5_rtcp_status_publisher unavailable: {exc}', file=sys.stderr, flush=True)
        if args.once:
            return 0
        time.sleep(interval)


if __name__ == '__main__':
    raise SystemExit(main())
