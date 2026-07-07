#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os

from v5_touch_core import *  # noqa: F401,F403
from v5_touch_window_calibration import TouchCalibrationCalibrationMixin
from v5_touch_window_restart import TouchCalibrationRestartMixin
from v5_touch_window_runtime import TouchCalibrationRuntimeMixin

class TouchCalibrationWindow(TouchCalibrationRuntimeMixin, TouchCalibrationRestartMixin, TouchCalibrationCalibrationMixin, QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("v5 Touch Calibration")
        self.setStyleSheet("background-color: #0b1118; color: #dce9f8;")
        self.resize(1024, 600)
        self.setMouseTracking(True)
        self.setAttribute(QtCore.Qt.WA_AcceptTouchEvents, True)

        self.report_width = 1024.0
        self.report_height = 600.0
        # Default raw range follows the 1024x600 panel (0..1023, 0..599).
        # If python-evdev is available, absInfoReady will overwrite these with
        # the real kernel-reported ABS max values.
        self.raw_max_x = 1023.0
        self.raw_max_y = 599.0
        self.sx = (self.report_width - 1.0) / self.raw_max_x
        self.sxy = 0.0
        self.syx = 0.0
        self.sy = (self.report_height - 1.0) / self.raw_max_y
        self.ox = 0.0
        self.oy = 0.0
        self._calibration_loaded = False
        self.calibration_warning = ""
        self._load_calibration()

        self.cal_targets = [(80, 80), (944, 80), (944, 520), (80, 520), (512, 300)]
        self.calibrating = False
        self.cal_index = 0
        self.cal_samples = []
        self.current_raw = None
        self.wait_start_release = False
        self.wait_release = False
        self.release_guard_deadline = 0.0
        self.ignore_until = 0.0
        self.enable_linuxcnc = os.environ.get("V5_ENABLE_LINUXCNC", "1") == "1"
        # Default behavior: after calibration, return to LinuxCNC UI.
        # Touch-only mode can opt out via V5_TOUCH_CAL_EXIT_TO_LINUXCNC=0.
        self.force_linuxcnc_handoff = os.environ.get("V5_TOUCH_CAL_EXIT_TO_LINUXCNC", "1") == "1"
        self._raw_button_last_trigger = 0.0
        self.touch_thread = None
        self.touch_device_path = ""
        self.touch_device_ready = False
        self.restart_wait_deadline = 0.0
        self.restart_wait_context = ""
        self.restart_wait_timer = QtCore.QTimer(self)
        self.restart_wait_timer.setInterval(500)
        self.restart_wait_timer.timeout.connect(self._poll_linuxcnc_service_active)
        self.release_guard_timer = QtCore.QTimer(self)
        self.release_guard_timer.setInterval(120)
        self.release_guard_timer.timeout.connect(self._cal_release_guard_tick)
        self.release_guard_timer.start()

        self.title = QtWidgets.QLabel("v5 Touch Calibration", self)
        self.title.setGeometry(0, 18, 1024, 42)
        self.title.setAlignment(QtCore.Qt.AlignCenter)
        self.title.setStyleSheet('font: 75 22pt "Sans Serif"; color: #eaf2ff;')

        self.status = QtWidgets.QLabel("Preparing touch device...", self)
        self.status.setGeometry(24, 76, 976, 56)
        self.status.setAlignment(QtCore.Qt.AlignCenter)
        self.status.setStyleSheet(
            'background:#13202b; border:2px solid #22384a; border-radius:14px; font:75 18pt "Sans Serif"; color:#2ee6ff;'
        )

        self.hint = QtWidgets.QLabel("Tap targets in order; release finger after each sample.", self)
        self.hint.setGeometry(24, 142, 976, 28)
        self.hint.setAlignment(QtCore.Qt.AlignCenter)
        self.hint.setStyleSheet('font: 12pt "Sans Serif"; color: #86a7c2;')
        if self.calibration_warning:
            self.hint.setText(self.calibration_warning)

        self.btn_exit = QtWidgets.QPushButton("Exit Calibration", self)
        self.btn_exit.setGeometry(24, 18, 160, 42)
        self.btn_exit.setStyleSheet(
            'QPushButton{background:#2b3a46; color:#eaf2ff; border:2px solid #4f6475; border-radius:10px; font: 11pt "Sans Serif";}'
            'QPushButton:pressed{background:#1f2b34;}'
        )
        self.btn_exit.clicked.connect(self._exit_touch_calibration)

        self.btn_start_linuxcnc = QtWidgets.QPushButton("Start LinuxCNC", self)
        self.btn_start_linuxcnc.setGeometry(196, 18, 160, 42)
        self.btn_start_linuxcnc.setStyleSheet(
            'QPushButton{background:#174d3d; color:#dfffea; border:2px solid #2e7b62; border-radius:10px; font: 11pt "Sans Serif";}'
            'QPushButton:pressed{background:#123a2f;}'
        )
        self.btn_start_linuxcnc.clicked.connect(self._start_linuxcnc_now)
        if not self._linuxcnc_handoff_enabled():
            self.btn_start_linuxcnc.hide()
            self.btn_exit.setGeometry(24, 18, 220, 42)
            self.btn_exit.setText("Reset Calibration UI")

        self.raw_label = QtWidgets.QLabel("raw=(---, ---)", self)
        self.raw_label.setGeometry(24, 544, 976, 32)
        self.raw_label.setAlignment(QtCore.Qt.AlignCenter)
        self.raw_label.setStyleSheet('font: 14pt "Monospace"; color: #f4d35e;')

        self.target_ring = QtWidgets.QFrame(self)
        self.target_ring.setStyleSheet("background:transparent; border:4px solid #ffe35e; border-radius:28px;")
        self.target_ring.setGeometry(-100, -100, 56, 56)
        self.target_ring.hide()

        self.target_h = QtWidgets.QFrame(self)
        self.target_h.setStyleSheet("background:#ffe35e;")
        self.target_h.hide()

        self.target_v = QtWidgets.QFrame(self)
        self.target_v.setStyleSheet("background:#ffe35e;")
        self.target_v.hide()

        self.touch_h = QtWidgets.QFrame(self)
        self.touch_h.setStyleSheet("background:#ff5a5a;")
        self.touch_h.hide()

        self.touch_v = QtWidgets.QFrame(self)
        self.touch_v.setStyleSheet("background:#ff5a5a;")
        self.touch_v.hide()

        self._init_touch_reader()


__all__ = ["TouchCalibrationWindow"]

