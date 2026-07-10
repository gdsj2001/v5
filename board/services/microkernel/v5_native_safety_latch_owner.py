#!/usr/bin/env python3
import argparse
import ctypes
import mmap
import os
import resource
import signal
import struct
import sys
import time


MAGIC = 0x56355346
VERSION = 1
FRAME = struct.Struct("<10I")
DEFAULT_PATH = "/dev/shm/v5_native_safety_latch.bin"
DEFAULT_COMPONENT = "v5_native_safety"
DEFAULT_INTERVAL_MS = 5

IDX_MAGIC = 0
IDX_VERSION = 1
IDX_FORCE_EPOCH = 2
IDX_FORCE_ACK = 3
IDX_RESET_EPOCH = 4
IDX_RESET_ACK = 5
IDX_ESTOP_KNOWN = 6
IDX_ESTOP_ACTIVE = 7
IDX_ENABLE_KNOWN = 8
IDX_ENABLED = 9


def lock_process_memory(process_name):
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
        raise SystemExit("%s mlockall(MCL_CURRENT|MCL_FUTURE) failed: errno=%d" % (process_name, err))


def load_hal():
    dist = "/usr/lib/python3/dist-packages"
    if os.path.isdir(dist) and dist not in sys.path:
        sys.path.insert(0, dist)
    import hal  # type: ignore
    return hal


class Latch:
    def __init__(self, path):
        self.path = path
        os.makedirs(os.path.dirname(path), exist_ok=True)
        fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o666)
        try:
            if os.fstat(fd).st_size != FRAME.size:
                os.ftruncate(fd, FRAME.size)
            self.map = mmap.mmap(fd, FRAME.size)
        finally:
            os.close(fd)
        fields = self.read()
        if fields[IDX_MAGIC] != MAGIC or fields[IDX_VERSION] != VERSION:
            self.map[:] = FRAME.pack(MAGIC, VERSION, 0, 0, 0, 0, 1, 0, 0, 0)
            self.map.flush()
        else:
            self.acknowledge_current_epochs()

    def close(self):
        self.map.close()

    def read(self):
        return list(FRAME.unpack(self.map[:FRAME.size]))

    def write_field(self, index, value):
        struct.pack_into("<I", self.map, index * 4, int(value) & 0xFFFFFFFF)

    def acknowledge_current_epochs(self):
        fields = self.read()
        if fields[IDX_FORCE_ACK] == fields[IDX_FORCE_EPOCH] and fields[IDX_RESET_ACK] == fields[IDX_RESET_EPOCH]:
            return
        self.write_field(IDX_FORCE_ACK, fields[IDX_FORCE_EPOCH])
        self.write_field(IDX_RESET_ACK, fields[IDX_RESET_EPOCH])
        self.map.flush()

    def set_status(self, estop_active, machine_enable_known=0, machine_enabled=0):
        values = (
            1,
            1 if estop_active else 0,
            1 if machine_enable_known else 0,
            1 if machine_enabled else 0,
        )
        fields = self.read()
        if tuple(fields[IDX_ESTOP_KNOWN:IDX_ENABLED + 1]) == values:
            return
        self.write_field(IDX_ESTOP_KNOWN, values[0])
        self.write_field(IDX_ESTOP_ACTIVE, values[1])
        self.write_field(IDX_ENABLE_KNOWN, values[2])
        self.write_field(IDX_ENABLED, values[3])
        self.map.flush()

    def ack_force(self, epoch, machine_enabled):
        self.write_field(IDX_FORCE_ACK, epoch)
        self.set_status(estop_active=1, machine_enable_known=1, machine_enabled=machine_enabled)

    def ack_reset(self, epoch, machine_enabled):
        self.write_field(IDX_RESET_ACK, epoch)
        self.set_status(estop_active=0, machine_enable_known=1, machine_enabled=machine_enabled)


def main():
    parser = argparse.ArgumentParser(description="v5 native safety latch HAL owner")
    parser.add_argument("--path", default=DEFAULT_PATH)
    parser.add_argument("--component", default=DEFAULT_COMPONENT)
    parser.add_argument("--interval-ms", type=int, default=DEFAULT_INTERVAL_MS)
    args = parser.parse_args()

    stop = {"value": False}

    def on_signal(_signo, _frame):
        stop["value"] = True

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    lock_process_memory("v5_native_safety_latch_owner")
    hal = load_hal()
    latch = Latch(args.path)
    component = hal.component(args.component)
    component.newpin("estop-ok", hal.HAL_BIT, hal.HAL_OUT)
    component.newpin("estop-active", hal.HAL_BIT, hal.HAL_OUT)
    component.newpin("machine-enabled-in", hal.HAL_BIT, hal.HAL_IN)
    component.newpin("machine-enable-known", hal.HAL_BIT, hal.HAL_OUT)
    component.newpin("machine-enabled", hal.HAL_BIT, hal.HAL_OUT)
    component["estop-ok"] = True
    component["estop-active"] = False
    component["machine-enable-known"] = True
    component["machine-enabled"] = False
    latch.set_status(estop_active=0, machine_enable_known=1, machine_enabled=0)
    component.ready()

    estop_active = False
    interval = max(args.interval_ms, 1) / 1000.0
    try:
        while not stop["value"]:
            fields = latch.read()
            force_epoch = fields[IDX_FORCE_EPOCH]
            reset_epoch = fields[IDX_RESET_EPOCH]
            machine_enabled = bool(component["machine-enabled-in"])
            if force_epoch != fields[IDX_FORCE_ACK]:
                estop_active = True
                component["estop-ok"] = False
                component["estop-active"] = True
                latch.ack_force(force_epoch, machine_enabled)
            elif reset_epoch != fields[IDX_RESET_ACK]:
                estop_active = False
                component["estop-ok"] = True
                component["estop-active"] = False
                latch.ack_reset(reset_epoch, machine_enabled)
            component["machine-enable-known"] = True
            component["machine-enabled"] = machine_enabled
            latch.set_status(estop_active=estop_active, machine_enable_known=1, machine_enabled=machine_enabled)
            time.sleep(interval)
    finally:
        component["estop-ok"] = False
        component["estop-active"] = True
        component["machine-enable-known"] = True
        component["machine-enabled"] = False
        latch.set_status(estop_active=1, machine_enable_known=1, machine_enabled=0)
        component.exit()
        latch.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
