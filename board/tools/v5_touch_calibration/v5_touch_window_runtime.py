#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import os
import time

from v5_touch_core import *  # noqa: F401,F403
from v5_touch_evdev import EvdevTouchThread


class TouchCalibrationRuntimeMixin:
    def _init_touch_reader(self):
        if self.touch_thread is not None:
            try:
                self.touch_thread.stop()
                self.touch_thread.wait(1200)
            except Exception:
                pass
            self.touch_thread = None

        touch_device = _resolve_raw_touch_device()
        self.touch_device_path = touch_device or ""
        self.touch_device_ready = bool(touch_device)
        if not self.touch_device_ready:
            self.status.setText("Touch device not found. Check /dev/input/by-path/z20-touchscreen.")
            self.hint.setText("Touch device unavailable. Check wiring and retry detection.")
            self.btn_exit.setText("Retry Touch Detect")
            self.calibrating = False
            return False

        self.btn_exit.setText("Exit Calibration" if self._linuxcnc_handoff_enabled() else "Reset Calibration UI")
        self.touch_thread = EvdevTouchThread(device_path=self.touch_device_path, parent=self)
        self.touch_thread.absInfoReady.connect(self._on_absinfo_ready)
        self.touch_thread.sample.connect(self._on_touch_sample)
        self.touch_thread.press.connect(self._on_touch_press)
        self.touch_thread.release.connect(self._on_touch_release)
        self.touch_thread.start()
        QtCore.QTimer.singleShot(0, self.start_calibration)
        return True

    def closeEvent(self, event):
        if self.restart_wait_timer.isActive():
            self.restart_wait_timer.stop()
        if self.touch_thread is not None:
            try:
                self.touch_thread.stop()
                if not self.touch_thread.wait(1500):
                    print("[touch-cal] touch thread stop timeout", file=sys.stderr)
            except Exception as exc:
                print(f"[touch-cal] stop thread failed: {exc}", file=sys.stderr)
        super().closeEvent(event)

    def _on_absinfo_ready(self, max_x, max_y):
        self.raw_max_x = max(1.0, float(max_x))
        self.raw_max_y = max(1.0, float(max_y))
        # If no valid persisted calibration has been loaded, keep a sane
        # full-screen linear mapping from raw range to panel range.
        if not self._calibration_loaded:
            self.sx = (self.report_width - 1.0) / self.raw_max_x
            self.sxy = 0.0
            self.syx = 0.0
            self.sy = (self.report_height - 1.0) / self.raw_max_y
            self.ox = 0.0
            self.oy = 0.0

    def _clamp_screen_point(self, sx, sy):
        x = int(round(float(sx)))
        y = int(round(float(sy)))
        max_x = max(0, int(self.report_width) - 1)
        max_y = max(0, int(self.report_height) - 1)
        if x < 0:
            x = 0
        elif x > max_x:
            x = max_x
        if y < 0:
            y = 0
        elif y > max_y:
            y = max_y
        return x, y

    def _screen_to_raw(self, sx, sy):
        x, y = self._clamp_screen_point(sx, sy)
        if self.report_width <= 1 or self.report_height <= 1:
            return int(x), int(y)
        rx = int(round((float(x) / float(self.report_width - 1.0)) * float(self.raw_max_x)))
        ry = int(round((float(y) / float(self.report_height - 1.0)) * float(self.raw_max_y)))
        return rx, ry

    def _raw_to_screen_point(self, raw_x, raw_y):
        return self._map_screen_point(raw_x, raw_y)

    def _touch_hits_widget(self, widget, sx, sy):
        if widget is None or not widget.isVisible():
            return False
        rect = widget.geometry()
        return rect.contains(QtCore.QPoint(int(sx), int(sy)))

    def _dispatch_touch_buttons(self, raw_x, raw_y):
        sx, sy = self._raw_to_screen_point(raw_x, raw_y)
        now = time.monotonic()
        if (now - float(self._raw_button_last_trigger)) < 0.22:
            return True
        if self._touch_hits_widget(self.btn_start_linuxcnc, sx, sy):
            self._raw_button_last_trigger = now
            self._start_linuxcnc_now()
            return True
        if self._touch_hits_widget(self.btn_exit, sx, sy):
            self._raw_button_last_trigger = now
            self._exit_touch_calibration()
            return True
        return False

    def _load_calibration(self):
        if not CAL_PATH.exists() or CAL_PATH.stat().st_size == 0:
            self.calibration_warning = "Calibration file missing; using default mapping."
            self._calibration_loaded = False
            return
        try:
            data = json.loads(CAL_PATH.read_text(encoding="utf-8"))
            mode = data.get("mode")
            if mode == CAL_MODE:
                profile = data.get("profile")
                if profile != CAL_PROFILE:
                    raise ValueError(f"unsupported calibration profile: {profile!r}")
                self.sx = float(data.get("sx", self.sx))
                self.sxy = float(data.get("sxy", self.sxy))
                self.syx = float(data.get("syx", self.syx))
                self.sy = float(data.get("sy", self.sy))
                self.ox = float(data.get("ox", self.ox))
                self.oy = float(data.get("oy", self.oy))
            self.calibration_warning = ""
            self._calibration_loaded = True
        except Exception as exc:
            self.calibration_warning = f"Calibration file invalid; using default mapping ({exc})."
            self._calibration_loaded = False

