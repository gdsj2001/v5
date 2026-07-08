#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import os
import select
import subprocess
import sys
import threading
import time
import fcntl
import struct
from pathlib import Path

from PyQt5 import QtCore, QtWidgets, QtGui

try:
    from evdev import InputDevice, ecodes
    EVDEV_AVAILABLE = True
except Exception:
    InputDevice = None
    EVDEV_AVAILABLE = False

    class _RawEvdevCodes:
        EV_ABS = 3
        EV_KEY = 1
        EV_SYN = 0
        ABS_X = 0
        ABS_Y = 1
        ABS_MT_POSITION_X = 53
        ABS_MT_POSITION_Y = 54
        ABS_MT_TRACKING_ID = 57
        BTN_TOUCH = 330
        SYN_REPORT = 0

    ecodes = _RawEvdevCodes()


CAL_PATH = Path("/opt/8ax/safe_ui/re_touch_calibration.json")
CAL_MODE = "raw-evdev-cal-v2"
CAL_PROFILE = "screen-fit-topleft-1024x600-v1"
CAL_ANCHOR = "topleft-v1"
TOUCH_UDEV_LINK = "/dev/input/by-path/z20-touchscreen"
RESTART_REQUEST = "/run/v5_touch_calibration/restart.request"
BOARD_REBOOT_MARKER = "/run/v5_touch_calibration/reboot.requested"
LINUXCNC_SERVICE = "v5-ui-relay"
CAL_POINT_TIMEOUT_S = 30.0
FIT_MAX_ERROR_PX = 36.0
EDGE_MAX_ERROR_PX = 48.0
_THIS_DIR = Path(__file__).resolve().parent

APPLY_HELPER = str(_THIS_DIR / "v5_apply_touch_calibration.py")
RESTART_HELPER = str(_THIS_DIR / "v5_restart_after_touch_calibration.sh")


def _resolve_raw_touch_device():
    preferred = os.environ.get("V5_RAW_TOUCH_DEVICE", "").strip()
    if preferred and os.path.exists(preferred):
        return preferred
    if os.path.exists(TOUCH_UDEV_LINK):
        return TOUCH_UDEV_LINK
    return ""


def _install_runtime_font(app: QtWidgets.QApplication) -> str:
    # On minimal rootfs builds the default font catalog can be empty, which
    # causes widgets to render without visible glyphs. Load a known TTF first.
    candidates = []
    env_font = os.environ.get("V5_UI_FONT_FILE", "").strip()
    if env_font:
        candidates.append(env_font)
    candidates.extend(
        [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        ]
    )

    for path in candidates:
        if not path or (not os.path.exists(path)):
            continue
        try:
            fid = QtGui.QFontDatabase.addApplicationFont(path)
        except Exception:
            continue
        if fid < 0:
            continue
        fams = QtGui.QFontDatabase.applicationFontFamilies(fid)
        if not fams:
            continue
        family = fams[0]
        app.setFont(QtGui.QFont(family, 12))
        return family

    app.setFont(QtGui.QFont("Sans Serif", 12))
    return "Sans Serif"


def _write_text_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f".{path.name}.tmp")
    tmp.write_text(text, encoding="utf-8")
    os.replace(tmp, path)


def _solve_3x3(matrix, rhs):
    rows = [list(matrix[i]) + [float(rhs[i])] for i in range(3)]
    for col in range(3):
        pivot = col
        pivot_abs = abs(rows[pivot][col])
        for r in range(col + 1, 3):
            val = abs(rows[r][col])
            if val > pivot_abs:
                pivot = r
                pivot_abs = val
        if pivot_abs < 1e-12:
            raise ValueError("singular calibration matrix")
        if pivot != col:
            rows[col], rows[pivot] = rows[pivot], rows[col]
        pivot_val = rows[col][col]
        for c in range(col, 4):
            rows[col][c] /= pivot_val
        for r in range(3):
            if r == col:
                continue
            factor = rows[r][col]
            if abs(factor) < 1e-12:
                continue
            for c in range(col, 4):
                rows[r][c] -= factor * rows[col][c]
    return rows[0][3], rows[1][3], rows[2][3]


def _fit_affine(samples, targets):
    n = float(len(samples))
    if n < 3:
        raise ValueError("not enough calibration samples")
    s_xx = s_xy = s_x = 0.0
    s_yy = s_y = 0.0
    tx_x = tx_y = tx_1 = 0.0
    ty_x = ty_y = ty_1 = 0.0
    for sample, target in zip(samples, targets):
        rx = float(sample[0])
        ry = float(sample[1])
        px = float(target[0])
        py = float(target[1])
        s_xx += rx * rx
        s_xy += rx * ry
        s_x += rx
        s_yy += ry * ry
        s_y += ry
        tx_x += px * rx
        tx_y += px * ry
        tx_1 += px
        ty_x += py * rx
        ty_y += py * ry
        ty_1 += py
    mat = (
        (s_xx, s_xy, s_x),
        (s_xy, s_yy, s_y),
        (s_x, s_y, n),
    )
    sx, sxy, ox = _solve_3x3(mat, (tx_x, tx_y, tx_1))
    syx, sy, oy = _solve_3x3(mat, (ty_x, ty_y, ty_1))
    det = (sx * sy) - (sxy * syx)
    if abs(det) < 1e-9:
        raise ValueError("degenerate affine fit")
    return sx, sxy, ox, syx, sy, oy



__all__ = [name for name in globals() if not name.startswith("__")]
