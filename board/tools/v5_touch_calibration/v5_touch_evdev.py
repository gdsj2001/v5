#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import select
import struct
import time

from v5_touch_core import resolve_raw_touch_device


EV_SYN = 0
EV_KEY = 1
EV_ABS = 3
SYN_REPORT = 0
ABS_X = 0
ABS_Y = 1
ABS_MT_POSITION_X = 53
ABS_MT_POSITION_Y = 54
ABS_MT_TRACKING_ID = 57
BTN_TOUCH = 330


class RawTouchDevice:
    def __init__(self, device_path=None):
        self.device_path = device_path or resolve_raw_touch_device()
        if not self.device_path:
            raise RuntimeError("raw touch device unavailable")
        self.fd = os.open(self.device_path, os.O_RDONLY | os.O_NONBLOCK)
        self.event_struct = struct.Struct("llHHi")
        self.buffer = b""
        self.raw_x = 0
        self.raw_y = 0
        self.have_x = False
        self.have_y = False
        self.touching = False
        self.current_sample = None
        self.release_pending = False
        # The affine fit consumes raw samples directly; reported ABS maxima are
        # diagnostic only and must not add another rootfs module dependency.
        self.max_x = 1023
        self.max_y = 599

    def close(self):
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback):
        self.close()

    def _consume_event(self, event_type, event_code, event_value):
        if event_type == EV_ABS:
            if event_code in (ABS_X, ABS_MT_POSITION_X):
                self.raw_x = int(event_value)
                self.have_x = True
            elif event_code in (ABS_Y, ABS_MT_POSITION_Y):
                self.raw_y = int(event_value)
                self.have_y = True
            elif event_code == ABS_MT_TRACKING_ID:
                if int(event_value) >= 0:
                    self.touching = True
                    self.release_pending = False
                else:
                    self.touching = False
                    self.release_pending = True
        elif event_type == EV_KEY and event_code == BTN_TOUCH:
            if int(event_value) != 0:
                self.touching = True
                self.release_pending = False
            else:
                self.touching = False
                self.release_pending = True
        elif event_type == EV_SYN and event_code == SYN_REPORT:
            if self.touching and self.have_x and self.have_y:
                self.current_sample = (self.raw_x, self.raw_y)
            if self.release_pending:
                self.release_pending = False
                if self.current_sample is not None:
                    sample = self.current_sample
                    self.current_sample = None
                    return sample
        return None

    def _read_ready_events(self):
        try:
            chunk = os.read(self.fd, self.event_struct.size * 64)
        except BlockingIOError:
            return None
        if not chunk:
            return None
        self.buffer += chunk
        while len(self.buffer) >= self.event_struct.size:
            event = self.buffer[: self.event_struct.size]
            self.buffer = self.buffer[self.event_struct.size :]
            _seconds, _microseconds, event_type, event_code, event_value = self.event_struct.unpack(event)
            sample = self._consume_event(event_type, event_code, event_value)
            if sample is not None:
                return sample
        return None

    def wait_for_tap(self, deadline, tick_callback):
        last_tick = None
        while True:
            now = time.monotonic()
            remaining = deadline - now
            if remaining <= 0.0:
                return None
            seconds_left = int(remaining + 0.999)
            if seconds_left != last_tick:
                tick_callback(seconds_left)
                last_tick = seconds_left
            ready, _writable, _errors = select.select([self.fd], [], [], min(0.10, remaining))
            if not ready:
                continue
            sample = self._read_ready_events()
            if sample is not None:
                return sample


__all__ = ["RawTouchDevice"]
