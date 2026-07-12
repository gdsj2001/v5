#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import mmap
import os
import subprocess
import sys
import time
from pathlib import Path

from v5_touch_core import (
    APPLY_HELPER,
    CAL_PATH,
    CAL_POINT_TIMEOUT_S,
    CAL_TARGETS,
    DISPLAY_HEIGHT,
    DISPLAY_WIDTH,
    EDGE_MAX_ERROR_PX,
    FIT_MAX_ERROR_PX,
    POINTERCAL_PATH,
    RESTART_HELPER,
    fit_affine,
    save_calibration,
    validate_fit,
    write_text_atomic,
)
from v5_touch_evdev import RawTouchDevice


FB_DEVICE = os.environ.get("V5_FB_DEVICE", "/dev/fb0")
FB_SYSFS = Path(os.environ.get("V5_FB_SYSFS", "/sys/class/graphics/fb0"))
BACKGROUND = (7, 20, 31)
TARGET = (255, 224, 72)
COMPLETE = (42, 210, 132)
PENDING = (55, 83, 102)
FAILED = (210, 50, 62)


class DirectFramebuffer:
    def __init__(self, device=FB_DEVICE, sysfs_root=FB_SYSFS):
        self.width = DISPLAY_WIDTH
        self.height = DISPLAY_HEIGHT
        self.stride = int((sysfs_root / "stride").read_text(encoding="ascii").strip())
        self.bits_per_pixel = int(
            (sysfs_root / "bits_per_pixel").read_text(encoding="ascii").strip()
        )
        virtual = (sysfs_root / "virtual_size").read_text(encoding="ascii").strip().split(",")
        self.virtual_height = int(virtual[1])
        if self.bits_per_pixel not in (24, 32):
            raise RuntimeError("unsupported framebuffer bpp=%d" % self.bits_per_pixel)
        self.bytes_per_pixel = self.bits_per_pixel // 8
        if self.stride < self.width * self.bytes_per_pixel:
            raise RuntimeError("invalid framebuffer stride=%d" % self.stride)
        self.fd = os.open(device, os.O_RDWR)
        self.length = self.stride * self.virtual_height
        self.memory = mmap.mmap(
            self.fd,
            self.length,
            flags=mmap.MAP_SHARED,
            prot=mmap.PROT_READ | mmap.PROT_WRITE,
        )
        self.page_offsets = [offset for offset in range(0, self.virtual_height, self.height)]
        if not self.page_offsets:
            self.page_offsets = [0]

    def close(self):
        if self.memory is not None:
            self.memory.flush()
            self.memory.close()
            self.memory = None
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback):
        self.close()

    def _pixel(self, color):
        red, green, blue = color
        if self.bytes_per_pixel == 3:
            return bytes((blue, green, red))
        return bytes((blue, green, red, 255))

    def rectangle(self, x, y, width, height, color):
        x = max(0, int(x))
        y = max(0, int(y))
        width = min(int(width), self.width - x)
        height = min(int(height), self.height - y)
        if width <= 0 or height <= 0:
            return
        row = self._pixel(color) * width
        for page_y in self.page_offsets:
            for line in range(y, y + height):
                start = (page_y + line) * self.stride + x * self.bytes_per_pixel
                self.memory[start : start + len(row)] = row

    def clear(self, color=BACKGROUND):
        self.rectangle(0, 0, self.width, self.height, color)

    def circle(self, center_x, center_y, radius, thickness, color):
        outer_sq = radius * radius
        inner = max(0, radius - thickness)
        inner_sq = inner * inner
        for dy in range(-radius, radius + 1):
            extent = int((max(0, outer_sq - dy * dy)) ** 0.5)
            inner_extent = int((max(0, inner_sq - dy * dy)) ** 0.5) if abs(dy) <= inner else -1
            self.rectangle(center_x - extent, center_y + dy, extent - inner_extent, 1, color)
            self.rectangle(center_x + inner_extent + 1, center_y + dy, extent - inner_extent, 1, color)

    def _digit(self, digit, x, y, scale, color):
        segments = {
            0: "abcedf",
            1: "bc",
            2: "abged",
            3: "abgcd",
            4: "fgbc",
            5: "afgcd",
            6: "afgecd",
            7: "abc",
            8: "abcdefg",
            9: "abfgcd",
        }[int(digit)]
        thickness = scale
        length = scale * 5
        geometry = {
            "a": (x + thickness, y, length, thickness),
            "b": (x + length + thickness, y + thickness, thickness, length),
            "c": (x + length + thickness, y + length + 2 * thickness, thickness, length),
            "d": (x + thickness, y + 2 * length + 2 * thickness, length, thickness),
            "e": (x, y + length + 2 * thickness, thickness, length),
            "f": (x, y + thickness, thickness, length),
            "g": (x + thickness, y + length + thickness, length, thickness),
        }
        for segment in segments:
            self.rectangle(*geometry[segment], color)

    def show_target(self, point_index, seconds_left):
        self.clear()
        box_width = 116
        start_x = (self.width - (box_width * len(CAL_TARGETS))) // 2
        for index in range(len(CAL_TARGETS)):
            color = COMPLETE if index < point_index else TARGET if index == point_index else PENDING
            self.rectangle(start_x + index * box_width, 24, box_width - 12, 14, color)
        target_x, target_y = CAL_TARGETS[point_index]
        self.circle(target_x, target_y, 36, 5, TARGET)
        self.rectangle(target_x - 28, target_y - 2, 56, 5, TARGET)
        self.rectangle(target_x - 2, target_y - 28, 5, 56, TARGET)
        tens, ones = divmod(max(0, int(seconds_left)), 10)
        self._digit(tens, 840, 56, 5, TARGET)
        self._digit(ones, 884, 56, 5, TARGET)
        self.memory.flush()

    def show_result(self, ok):
        color = COMPLETE if ok else FAILED
        self.clear((5, 18, 24) if ok else (34, 7, 12))
        self.circle(self.width // 2, self.height // 2, 72, 10, color)
        if ok:
            self.rectangle(476, 300, 28, 10, color)
            self.rectangle(500, 316, 75, 10, color)
        else:
            self.rectangle(472, 260, 10, 82, color)
            self.rectangle(542, 260, 10, 82, color)
            self.rectangle(482, 300, 60, 10, color)
        self.memory.flush()


def _snapshot(path):
    path = Path(path)
    return path.read_bytes() if path.is_file() else None


def _restore(path, payload):
    path = Path(path)
    if payload is None:
        if path.exists():
            path.unlink()
        return
    write_text_atomic(path, payload.decode("utf-8"))


def _run_checked(arguments, timeout):
    result = subprocess.run(
        arguments,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
        check=False,
        timeout=timeout,
    )
    if result.returncode != 0:
        raise RuntimeError("command failed rc=%d output=%s" % (result.returncode, result.stdout[:240]))
    return result.stdout.strip()


def run_calibration():
    samples = []
    with DirectFramebuffer() as framebuffer, RawTouchDevice() as touch:
        print(
            "[touch-cal] device=%s raw_max=%d,%d framebuffer=%s bpp=%d stride=%d"
            % (touch.device_path, touch.max_x, touch.max_y, FB_DEVICE, framebuffer.bits_per_pixel, framebuffer.stride),
            flush=True,
        )
        for point_index in range(len(CAL_TARGETS)):
            deadline = time.monotonic() + CAL_POINT_TIMEOUT_S
            sample = touch.wait_for_tap(
                deadline,
                lambda seconds, index=point_index: framebuffer.show_target(index, seconds),
            )
            if sample is None:
                framebuffer.show_result(False)
                time.sleep(0.4)
                print("[touch-cal] timeout point=%d no parameters written" % (point_index + 1), flush=True)
                return 2
            samples.append(sample)
            print("[touch-cal] sample point=%d raw=%d,%d" % (point_index + 1, sample[0], sample[1]), flush=True)

        coefficients = fit_affine(samples)
        average_error, maximum_error, edge_error = validate_fit(samples, coefficients)
        print(
            "[touch-cal] quality avg=%.3f max=%.3f edge=%.3f"
            % (average_error, maximum_error, edge_error),
            flush=True,
        )
        if maximum_error > FIT_MAX_ERROR_PX or edge_error > EDGE_MAX_ERROR_PX:
            framebuffer.show_result(False)
            time.sleep(0.4)
            print("[touch-cal] quality rejected; no parameters written", flush=True)
            return 3

        previous_calibration = _snapshot(CAL_PATH)
        previous_pointercal = _snapshot(POINTERCAL_PATH)
        try:
            save_calibration(coefficients)
            _run_checked([APPLY_HELPER], 10.0)
            framebuffer.show_result(True)
            time.sleep(0.5)
            output = _run_checked([RESTART_HELPER, "--reboot-board"], 10.0)
            if "BOARD_REBOOT_DISPATCHED" not in output:
                raise RuntimeError("board reboot helper did not confirm dispatch")
        except Exception:
            _restore(CAL_PATH, previous_calibration)
            _restore(POINTERCAL_PATH, previous_pointercal)
            raise
        print("[touch-cal] calibration committed and board reboot dispatched", flush=True)
        return 0


def main():
    try:
        return run_calibration()
    except Exception as error:
        print("[touch-cal] fatal: %s" % error, file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
