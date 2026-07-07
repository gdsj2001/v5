#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import fcntl
import os
import select
import struct
import time

from v5_touch_core import *  # noqa: F401,F403

class EvdevTouchThread(QtCore.QThread):
    sample = QtCore.pyqtSignal(int, int, bool)
    press = QtCore.pyqtSignal(int, int)
    release = QtCore.pyqtSignal(int, int)
    absInfoReady = QtCore.pyqtSignal(int, int)

    def __init__(self, device_path=None, parent=None):
        super().__init__(parent)
        self.device_path = device_path or _resolve_raw_touch_device()
        self._stop = False
        self._device = None
        self._device_fd = None

    def stop(self):
        self._stop = True
        dev = self._device
        if dev is not None:
            try:
                dev.close()
            except Exception:
                pass
        fd = self._device_fd
        if fd is not None:
            try:
                os.close(fd)
            except Exception:
                pass

    @staticmethod
    def _ioc(direction, type_char, nr, size):
        # Linux _IOC helper for EVIOCGABS ioctl code generation.
        return (direction << 30) | (ord(type_char) << 8) | (nr << 0) | (size << 16)

    def _read_abs_limits_raw(self, fd):
        # struct input_absinfo { value,min,max,fuzz,flat,resolution; }
        size = 24
        ioc_read = 2
        max_x = 1023
        max_y = 599
        queries = (
            (ecodes.ABS_X, "x"),
            (ecodes.ABS_Y, "y"),
            (ecodes.ABS_MT_POSITION_X, "x"),
            (ecodes.ABS_MT_POSITION_Y, "y"),
        )
        for code, axis in queries:
            req = self._ioc(ioc_read, "E", 0x40 + int(code), size)
            buf = bytearray(size)
            try:
                fcntl.ioctl(fd, req, buf, True)
                _value, mn, mx, _fuzz, _flat, _res = struct.unpack("iiiiii", bytes(buf))
                if int(mx) > int(mn):
                    if axis == "x":
                        max_x = int(mx)
                    else:
                        max_y = int(mx)
            except Exception:
                continue
        return max_x, max_y

    def _run_python_evdev_backend(self):
        if not self.device_path:
            return
        try:
            dev = InputDevice(self.device_path)
        except Exception:
            return
        self._device = dev
        caps = dict(dev.capabilities().get(ecodes.EV_ABS, []))
        max_x = int(getattr(caps.get(ecodes.ABS_X), "max", 1023))
        max_y = int(getattr(caps.get(ecodes.ABS_Y), "max", 599))
        self.absInfoReady.emit(max_x, max_y)

        raw_x = 0
        raw_y = 0
        touching = False
        dirty = False
        pressed_edge = False
        released_edge = False

        try:
            while not self._stop:
                try:
                    ready, _, _ = select.select([dev.fd], [], [], 0.2)
                except Exception:
                    if self._stop:
                        break
                    time.sleep(0.05)
                    continue
                if not ready:
                    continue
                try:
                    events = dev.read()
                except BlockingIOError:
                    continue
                except OSError:
                    if self._stop:
                        break
                    time.sleep(0.05)
                    continue
                for event in events:
                    if self._stop:
                        break
                    if event.type == ecodes.EV_ABS:
                        if event.code in (ecodes.ABS_X, ecodes.ABS_MT_POSITION_X):
                            raw_x = int(event.value)
                            dirty = True
                        elif event.code in (ecodes.ABS_Y, ecodes.ABS_MT_POSITION_Y):
                            raw_y = int(event.value)
                            dirty = True
                        elif event.code == ecodes.ABS_MT_TRACKING_ID:
                            tracking_id = int(event.value)
                            if tracking_id >= 0:
                                if not touching:
                                    pressed_edge = True
                                touching = True
                            else:
                                if touching:
                                    released_edge = True
                                touching = False
                    elif event.type == ecodes.EV_KEY and event.code == ecodes.BTN_TOUCH:
                        if event.value:
                            if not touching:
                                pressed_edge = True
                            touching = True
                        else:
                            if touching:
                                released_edge = True
                            touching = False
                    elif event.type == ecodes.EV_SYN and event.code == ecodes.SYN_REPORT:
                        if dirty or pressed_edge or released_edge:
                            self.sample.emit(raw_x, raw_y, touching)
                            if pressed_edge:
                                self.press.emit(raw_x, raw_y)
                            if released_edge:
                                self.release.emit(raw_x, raw_y)
                            dirty = False
                            pressed_edge = False
                            released_edge = False
        finally:
            try:
                dev.close()
            except Exception:
                pass
            self._device = None

    def _run_raw_event_backend(self):
        if not self.device_path:
            return
        try:
            fd = os.open(self.device_path, os.O_RDONLY | os.O_NONBLOCK)
        except Exception:
            return
        self._device_fd = fd
        try:
            max_x, max_y = self._read_abs_limits_raw(fd)
        except Exception:
            max_x, max_y = 1023, 599
        self.absInfoReady.emit(int(max_x), int(max_y))

        raw_x = 0
        raw_y = 0
        touching = False
        dirty = False
        pressed_edge = False
        released_edge = False
        fmt = "llHHi"
        size = struct.calcsize(fmt)
        buf = b""

        try:
            while not self._stop:
                try:
                    ready, _, _ = select.select([fd], [], [], 0.2)
                except Exception:
                    if self._stop:
                        break
                    time.sleep(0.05)
                    continue
                if not ready:
                    continue
                try:
                    chunk = os.read(fd, size * 64)
                except BlockingIOError:
                    continue
                except OSError:
                    if self._stop:
                        break
                    time.sleep(0.05)
                    continue
                if not chunk:
                    if self._stop:
                        break
                    time.sleep(0.02)
                    continue
                buf += chunk
                while len(buf) >= size:
                    evt = buf[:size]
                    buf = buf[size:]
                    _tv_sec, _tv_usec, etype, ecode, evalue = struct.unpack(fmt, evt)

                    if etype == ecodes.EV_ABS:
                        if ecode in (ecodes.ABS_X, ecodes.ABS_MT_POSITION_X):
                            raw_x = int(evalue)
                            dirty = True
                        elif ecode in (ecodes.ABS_Y, ecodes.ABS_MT_POSITION_Y):
                            raw_y = int(evalue)
                            dirty = True
                        elif ecode == ecodes.ABS_MT_TRACKING_ID:
                            tracking_id = int(evalue)
                            if tracking_id >= 0:
                                if not touching:
                                    pressed_edge = True
                                touching = True
                            else:
                                if touching:
                                    released_edge = True
                                touching = False
                    elif etype == ecodes.EV_KEY and ecode == ecodes.BTN_TOUCH:
                        if int(evalue) != 0:
                            if not touching:
                                pressed_edge = True
                            touching = True
                        else:
                            if touching:
                                released_edge = True
                            touching = False
                    elif etype == ecodes.EV_SYN and ecode == ecodes.SYN_REPORT:
                        if dirty or pressed_edge or released_edge:
                            self.sample.emit(raw_x, raw_y, touching)
                            if pressed_edge:
                                self.press.emit(raw_x, raw_y)
                            if released_edge:
                                self.release.emit(raw_x, raw_y)
                            dirty = False
                            pressed_edge = False
                            released_edge = False
        finally:
            try:
                os.close(fd)
            except Exception:
                pass
            self._device_fd = None

    def run(self):
        if not self.device_path:
            return
        if EVDEV_AVAILABLE:
            self._run_python_evdev_backend()
        else:
            self._run_raw_event_backend()



__all__ = [name for name in globals() if not name.startswith("__")]

