#!/usr/bin/env python3
import argparse
import mmap
import os
import struct
import sys
import time
import ctypes
import resource
from typing import Tuple



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

CONTROL_MAGIC = 0x56355254
CONTROL_VERSION = 1
CONTROL_STRUCT = struct.Struct('<8I')
DEFAULT_CONTROL_PATH = '/dev/shm/v5_native_rtcp_control_latch.bin'
CONTROL_STATUS_UNKNOWN = 0
CONTROL_STATUS_OK = 1
CONTROL_STATUS_FAILED = 2

IDX_MAGIC = 0
IDX_VERSION = 1
IDX_REQUEST_EPOCH = 2
IDX_ACK_EPOCH = 3
IDX_TARGET_ACTIVE = 4
IDX_ACTUAL_KNOWN = 5
IDX_ACTUAL_ACTIVE = 6
IDX_STATUS_CODE = 7


def crc32_like(prefix: bytes) -> int:
    value = 2166136261
    for b in prefix:
        value ^= b
        value = (value * 16777619) & 0xFFFFFFFF
    return value


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


def parse_mock_active(text: str):
    if not text:
        return None
    normalized = text.strip().lower()
    if normalized in ('1', 'on', 'true', 'yes'):
        return 1
    if normalized in ('0', 'off', 'false', 'no'):
        return 0
    raise SystemExit(f'bad --mock-active value: {text}')


def load_hal():
    dist = '/usr/lib/python3/dist-packages'
    if os.path.isdir(dist) and dist not in sys.path:
        sys.path.insert(0, dist)
    import hal  # type: ignore
    return hal


def status_from_hal_value(value) -> Tuple[int, int]:
    return 1, 1 if float(value) >= 0.5 else 0


def poll_once(hal_module, path: str) -> Tuple[int, int]:
    valid, active = status_from_hal_value(hal_module.get_value('motion.switchkins-type'))
    write_status(path, valid, active)
    return valid, active


class ControlLatch:
    def __init__(self, path: str):
        self.path = path
        os.makedirs(os.path.dirname(path), exist_ok=True)
        fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o666)
        try:
            if os.fstat(fd).st_size != CONTROL_STRUCT.size:
                os.ftruncate(fd, CONTROL_STRUCT.size)
            self.map = mmap.mmap(fd, CONTROL_STRUCT.size)
        finally:
            os.close(fd)
        fields = self.read()
        if fields[IDX_MAGIC] != CONTROL_MAGIC or fields[IDX_VERSION] != CONTROL_VERSION:
            self.map[:] = CONTROL_STRUCT.pack(
                CONTROL_MAGIC,
                CONTROL_VERSION,
                0,
                0,
                0,
                0,
                0,
                CONTROL_STATUS_UNKNOWN)
            self.map.flush()

    def close(self) -> None:
        self.map.close()

    def read(self):
        return list(CONTROL_STRUCT.unpack(self.map[:CONTROL_STRUCT.size]))

    def write_field(self, index: int, value: int) -> None:
        struct.pack_into('<I', self.map, index * 4, int(value) & 0xFFFFFFFF)

    def set_actual(self, valid: int, active: int) -> None:
        fields = self.read()
        values = (1 if valid else 0, 1 if active else 0)
        if tuple(fields[IDX_ACTUAL_KNOWN:IDX_ACTUAL_ACTIVE + 1]) == values:
            return
        self.write_field(IDX_ACTUAL_KNOWN, values[0])
        self.write_field(IDX_ACTUAL_ACTIVE, values[1])
        self.map.flush()

    def ack(self, epoch: int, ok: bool, valid: int, active: int) -> None:
        self.write_field(IDX_ACTUAL_KNOWN, 1 if valid else 0)
        self.write_field(IDX_ACTUAL_ACTIVE, 1 if active else 0)
        self.write_field(IDX_STATUS_CODE, CONTROL_STATUS_OK if ok else CONTROL_STATUS_FAILED)
        self.write_field(IDX_ACK_EPOCH, epoch)
        self.map.flush()


def set_switchkins_actual(hal_module, active: int) -> None:
    value = '1' if active else '0'
    errors = []
    set_p = getattr(hal_module, 'set_p', None)
    if callable(set_p):
        try:
            set_p('mux2.0.sel', value)
            return
        except Exception as exc:
            errors.append(f'set_p mux2.0.sel: {exc}')
    for fn_name in ('set_s', 'sets'):
        fn = getattr(hal_module, fn_name, None)
        if callable(fn):
            try:
                fn('re-switchkins-select', value)
                return
            except Exception as exc:
                errors.append(f'{fn_name}: {exc}')
    if callable(set_p):
        for name in ('re-switchkins-select', 'motion.switchkins-type'):
            try:
                set_p(name, value)
                return
            except Exception as exc:
                errors.append(f'set_p {name}: {exc}')
    raise RuntimeError('native HAL switchkins write failed: ' + '; '.join(errors))


def handle_control_or_poll(hal_module, latch: ControlLatch, path: str) -> Tuple[int, int]:
    fields = latch.read()
    request_epoch = fields[IDX_REQUEST_EPOCH]
    ack_epoch = fields[IDX_ACK_EPOCH]
    target = 1 if fields[IDX_TARGET_ACTIVE] else 0
    if request_epoch == ack_epoch:
        valid, active = poll_once(hal_module, path)
        latch.set_actual(valid, active)
        return valid, active

    try:
        set_switchkins_actual(hal_module, target)
        valid = 0
        active = 0
        for _attempt in range(25):
            valid, active = poll_once(hal_module, path)
            if valid and active == target:
                latch.ack(request_epoch, True, valid, active)
                return valid, active
            time.sleep(0.02)
        latch.ack(request_epoch, False, valid, active)
        return valid, active
    except Exception:
        write_status(path, 0, 0)
        latch.ack(request_epoch, False, 0, 0)
        raise


lock_process_memory("v5_rtcp_status_publisher")

def main() -> int:
    parser = argparse.ArgumentParser(description='Publish and control v5 RTCP actual status from LinuxCNC/HAL native switchkins actual.')
    parser.add_argument('--path', default=DEFAULT_PATH)
    parser.add_argument('--control-path', default=DEFAULT_CONTROL_PATH)
    parser.add_argument('--interval-ms', type=int, default=DEFAULT_INTERVAL_MS)
    parser.add_argument('--once', action='store_true')
    parser.add_argument('--mock-active', default='')
    args = parser.parse_args()

    mock = parse_mock_active(args.mock_active)
    if mock is not None:
        latch = ControlLatch(args.control_path)
        write_status(args.path, 1, mock)
        latch.set_actual(1, mock)
        latch.close()
        print(f'v5_rtcp_status_publisher mock valid=1 active={mock} path={args.path} control_path={args.control_path}')
        return 0

    hal_module = load_hal()
    latch = ControlLatch(args.control_path)
    hal_component = hal_module.component(f'v5_rtcp_pub_{os.getpid()}')
    hal_component.ready()
    interval = max(args.interval_ms, 20) / 1000.0
    try:
        while True:
            try:
                valid, active = handle_control_or_poll(hal_module, latch, args.path)
                print(f'v5_rtcp_status_publisher valid={valid} active={active} control_path={args.control_path}', flush=True)
            except Exception as exc:
                write_status(args.path, 0, 0)
                latch.set_actual(0, 0)
                print(f'v5_rtcp_status_publisher unavailable: {exc}', file=sys.stderr, flush=True)
            if args.once:
                return 0
            time.sleep(interval)
    finally:
        hal_component.exit()
        latch.close()


if __name__ == '__main__':
    raise SystemExit(main())
