#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import itertools
import os
from pathlib import Path


CAL_PATH = Path("/opt/8ax/safe_ui/re_touch_calibration.json")
POINTERCAL_PATH = Path("/etc/pointercal")
CAL_MODE = "raw-evdev-cal-v2"
CAL_PROFILE = "screen-fit-topleft-1024x600-v1"
CAL_ANCHOR = "topleft-v1"
TOUCH_UDEV_LINK = "/dev/input/by-path/z20-touchscreen"
CAL_POINT_TIMEOUT_S = 30.0
FIT_MAX_ERROR_PX = 36.0
EDGE_MAX_ERROR_PX = 48.0
DISPLAY_WIDTH = 1024
DISPLAY_HEIGHT = 600
CAL_TARGETS = ((80, 80), (944, 80), (944, 520), (80, 520), (512, 300))
_THIS_DIR = Path(__file__).resolve().parent
APPLY_HELPER = str(_THIS_DIR / "v5_apply_touch_calibration.py")
RESTART_HELPER = str(_THIS_DIR / "v5_restart_after_touch_calibration.sh")


def resolve_raw_touch_device():
    preferred = os.environ.get("V5_RAW_TOUCH_DEVICE", "").strip()
    if preferred and os.path.exists(preferred):
        return preferred
    if os.path.exists(TOUCH_UDEV_LINK):
        return TOUCH_UDEV_LINK
    return ""


def write_text_atomic(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(".%s.tmp.%d" % (path.name, os.getpid()))
    try:
        tmp.write_text(text, encoding="utf-8")
        os.chmod(str(tmp), 0o644)
        os.replace(str(tmp), str(path))
    finally:
        if tmp.exists():
            tmp.unlink()


def solve_3x3(matrix, rhs):
    rows = [list(matrix[i]) + [float(rhs[i])] for i in range(3)]
    for col in range(3):
        pivot = col
        pivot_abs = abs(rows[pivot][col])
        for row_index in range(col + 1, 3):
            value = abs(rows[row_index][col])
            if value > pivot_abs:
                pivot = row_index
                pivot_abs = value
        if pivot_abs < 1e-12:
            raise ValueError("singular calibration matrix")
        if pivot != col:
            rows[col], rows[pivot] = rows[pivot], rows[col]
        pivot_value = rows[col][col]
        for item_index in range(col, 4):
            rows[col][item_index] /= pivot_value
        for row_index in range(3):
            if row_index == col:
                continue
            factor = rows[row_index][col]
            if abs(factor) < 1e-12:
                continue
            for item_index in range(col, 4):
                rows[row_index][item_index] -= factor * rows[col][item_index]
    return rows[0][3], rows[1][3], rows[2][3]


def fit_affine(samples, targets=CAL_TARGETS):
    count = float(len(samples))
    if len(samples) != len(targets) or count < 3:
        raise ValueError("invalid calibration sample count")
    s_xx = s_xy = s_x = 0.0
    s_yy = s_y = 0.0
    tx_x = tx_y = tx_1 = 0.0
    ty_x = ty_y = ty_1 = 0.0
    for sample, target in zip(samples, targets):
        raw_x, raw_y = float(sample[0]), float(sample[1])
        target_x, target_y = float(target[0]), float(target[1])
        s_xx += raw_x * raw_x
        s_xy += raw_x * raw_y
        s_x += raw_x
        s_yy += raw_y * raw_y
        s_y += raw_y
        tx_x += target_x * raw_x
        tx_y += target_x * raw_y
        tx_1 += target_x
        ty_x += target_y * raw_x
        ty_y += target_y * raw_y
        ty_1 += target_y
    matrix = ((s_xx, s_xy, s_x), (s_xy, s_yy, s_y), (s_x, s_y, count))
    sx, sxy, ox = solve_3x3(matrix, (tx_x, tx_y, tx_1))
    syx, sy, oy = solve_3x3(matrix, (ty_x, ty_y, ty_1))
    if abs((sx * sy) - (sxy * syx)) < 1e-9:
        raise ValueError("degenerate affine fit")
    return sx, sxy, ox, syx, sy, oy


def map_point(coefficients, raw_point):
    sx, sxy, ox, syx, sy, oy = coefficients
    raw_x, raw_y = float(raw_point[0]), float(raw_point[1])
    return (raw_x * sx) + (raw_y * sxy) + ox, (raw_x * syx) + (raw_y * sy) + oy


def _orientation_invariant_corner_error(mapped_raw_corners, target_corners):
    if len(mapped_raw_corners) != 4 or len(target_corners) != 4:
        raise ValueError("corner quality check requires four raw and target corners")

    best_maximum_error = None
    for target_order in itertools.permutations(target_corners):
        errors = []
        for pixel, target in zip(mapped_raw_corners, target_order):
            dx = pixel[0] - target[0]
            dy = pixel[1] - target[1]
            errors.append((dx * dx + dy * dy) ** 0.5)
        maximum_error = max(errors)
        if best_maximum_error is None or maximum_error < best_maximum_error:
            best_maximum_error = maximum_error
    return best_maximum_error


def validate_fit(samples, coefficients, targets=CAL_TARGETS):
    fit_errors = []
    for sample, target in zip(samples, targets):
        pixel_x, pixel_y = map_point(coefficients, sample)
        dx = pixel_x - float(target[0])
        dy = pixel_y - float(target[1])
        fit_errors.append((dx * dx + dy * dy) ** 0.5)

    raw_min_x = min(float(point[0]) for point in samples)
    raw_max_x = max(float(point[0]) for point in samples)
    raw_min_y = min(float(point[1]) for point in samples)
    raw_max_y = max(float(point[1]) for point in samples)
    target_min_x = min(float(point[0]) for point in targets)
    target_max_x = max(float(point[0]) for point in targets)
    target_min_y = min(float(point[1]) for point in targets)
    target_max_y = max(float(point[1]) for point in targets)
    raw_corners = (
        (raw_min_x, raw_min_y),
        (raw_max_x, raw_min_y),
        (raw_max_x, raw_max_y),
        (raw_min_x, raw_max_y),
    )
    target_corners = (
        (target_min_x, target_min_y),
        (target_max_x, target_min_y),
        (target_max_x, target_max_y),
        (target_min_x, target_max_y),
    )
    mapped_raw_corners = tuple(map_point(coefficients, raw_point) for raw_point in raw_corners)
    edge_error = _orientation_invariant_corner_error(mapped_raw_corners, target_corners)
    average_error = sum(fit_errors) / float(len(fit_errors))
    return average_error, max(fit_errors), edge_error


def calibration_payload(coefficients):
    sx, sxy, ox, syx, sy, oy = coefficients
    return {
        "mode": CAL_MODE,
        "profile": CAL_PROFILE,
        "anchor": CAL_ANCHOR,
        "display": [DISPLAY_WIDTH, DISPLAY_HEIGHT],
        "sx": sx,
        "sxy": sxy,
        "syx": syx,
        "sy": sy,
        "ox": ox,
        "oy": oy,
    }


def save_calibration(coefficients):
    payload = json.dumps(calibration_payload(coefficients), ensure_ascii=False, indent=2) + "\n"
    write_text_atomic(CAL_PATH, payload)
    if CAL_PATH.read_text(encoding="utf-8") != payload:
        raise RuntimeError("calibration source-position readback mismatch")


__all__ = [name for name in globals() if not name.startswith("__")]
