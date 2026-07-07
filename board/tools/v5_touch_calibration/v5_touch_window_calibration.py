#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import time

from v5_touch_core import *  # noqa: F401,F403


class TouchCalibrationCalibrationMixin:
    def _save_calibration(self):
        data = {
            "mode": CAL_MODE,
            "profile": CAL_PROFILE,
            "anchor": CAL_ANCHOR,
            "display": [int(self.report_width), int(self.report_height)],
            "sx": self.sx,
            "sxy": self.sxy,
            "syx": self.syx,
            "sy": self.sy,
            "ox": self.ox,
            "oy": self.oy,
        }
        payload = json.dumps(data, ensure_ascii=False, indent=2)
        _write_text_atomic(CAL_PATH, payload + "\n")
        self._apply_runtime_calibration()


    def _on_touch_sample(self, raw_x, raw_y, touching):
        self.current_raw = (int(raw_x), int(raw_y))
        self._update_touch_overlay(int(raw_x), int(raw_y))
        if self.calibrating and (not touching) and self.wait_release:
            # Some touch stacks may miss explicit release events.
            # When sample stream reports "not touching", consume it as release.
            self._consume_calibration_release(raw_x, raw_y)
            return
        if self.calibrating and self.wait_release:
            self.status.setText(f"Point {self.cal_index + 1}/{len(self.cal_targets)} recorded; release finger")

    def _on_touch_press(self, raw_x, raw_y):
        if not self.calibrating:
            return
        if self.wait_release:
            return
        if time.time() < self.ignore_until:
            return
        self.current_raw = (int(raw_x), int(raw_y))
        self.wait_release = True
        self.release_guard_deadline = time.time() + 0.80
        self.status.setText(f"Point {self.cal_index + 1}/{len(self.cal_targets)} recorded; release finger")

    def _consume_calibration_release(self, raw_x, raw_y):
        if not self.calibrating:
            return
        now = time.time()
        if not self.wait_release:
            return
        if self.current_raw is None:
            self.current_raw = (int(raw_x), int(raw_y))
        if self.cal_index >= len(self.cal_targets):
            self.wait_release = False
            self.release_guard_deadline = 0.0
            self.ignore_until = now + 0.25
            return
        self.cal_samples.append(self.current_raw)
        self.cal_index += 1
        self.wait_release = False
        self.release_guard_deadline = 0.0
        self.ignore_until = now + 0.25
        if self.cal_index >= len(self.cal_targets):
            self._finish_calibration()
        else:
            self._show_target()
            self.status.setText(f"Tap target {self.cal_index + 1}/{len(self.cal_targets)}")

    def _cal_release_guard_tick(self):
        if not self.calibrating or not self.wait_release:
            return
        if self.release_guard_deadline <= 0.0:
            return
        if time.time() < self.release_guard_deadline:
            return
        if self.current_raw is None:
            return
        # Release guard for touch stacks that miss explicit release events.
        self._consume_calibration_release(self.current_raw[0], self.current_raw[1])

    def _on_touch_release(self, raw_x, raw_y):
        if self._dispatch_touch_buttons(raw_x, raw_y):
            return
        self._consume_calibration_release(raw_x, raw_y)


    def _start_linuxcnc_now(self):
        if not self._linuxcnc_handoff_enabled():
            self.status.setText("LinuxCNC start is disabled in touch-only mode.")
            self.hint.setText("Set V5_ENABLE_LINUXCNC=1 in the v5 runtime environment to enable it.")
            return
        self.calibrating = False
        self.status.setText("Starting LinuxCNC ...")
        self.hint.setText("Switching back to LinuxCNC runtime.")
        self._begin_linuxcnc_handoff("manual-start")

    def _finish_calibration(self):
        pts = self.cal_samples
        if len(pts) != len(self.cal_targets):
            self._set_calibration_chrome_visible(True)
            self.calibrating = False
            self.wait_start_release = False
            self.wait_release = False
            self.target_ring.hide()
            self.target_h.hide()
            self.target_v.hide()
            self.status.setText("Calibration sample count mismatch.")
            self.hint.setText("Tap Retry/Exit Calibration, or Start LinuxCNC.")
            return
        try:
            self.sx, self.sxy, self.ox, self.syx, self.sy, self.oy = _fit_affine(pts, self.cal_targets)
        except Exception as exc:
            self._set_calibration_chrome_visible(True)
            self.status.setText(f"Calibration solve failed: {exc}")
            self.hint.setText("Please retry touch calibration.")
            return

        fit_errors = []
        for idx, sample in enumerate(pts):
            tx, ty = self.cal_targets[idx]
            px, py = self._map_screen_point(sample[0], sample[1])
            dx = float(px - tx)
            dy = float(py - ty)
            fit_errors.append((dx * dx + dy * dy) ** 0.5)
        avg_err = sum(fit_errors) / float(len(fit_errors))
        max_err = max(fit_errors)

        raw_min_x = min(float(p[0]) for p in pts)
        raw_max_x = max(float(p[0]) for p in pts)
        raw_min_y = min(float(p[1]) for p in pts)
        raw_max_y = max(float(p[1]) for p in pts)
        target_min_x = min(float(t[0]) for t in self.cal_targets)
        target_max_x = max(float(t[0]) for t in self.cal_targets)
        target_min_y = min(float(t[1]) for t in self.cal_targets)
        target_max_y = max(float(t[1]) for t in self.cal_targets)
        edge_samples = (
            ((raw_min_x, raw_min_y), (target_min_x, target_min_y)),
            ((raw_max_x, raw_min_y), (target_max_x, target_min_y)),
            ((raw_max_x, raw_max_y), (target_max_x, target_max_y)),
            ((raw_min_x, raw_max_y), (target_min_x, target_max_y)),
        )
        edge_errors = []
        for raw_pt, target_pt in edge_samples:
            px, py = self._map_screen_point(raw_pt[0], raw_pt[1])
            dx = float(px) - float(target_pt[0])
            dy = float(py) - float(target_pt[1])
            edge_errors.append((dx * dx + dy * dy) ** 0.5)
        max_edge_err = max(edge_errors)
        if max_err > FIT_MAX_ERROR_PX or max_edge_err > EDGE_MAX_ERROR_PX:
            self._set_calibration_chrome_visible(True)
            self.status.setText("Calibration quality check failed.")
            self.hint.setText(
                "fit_max=%.1fpx edge_max=%.1fpx exceeds threshold; retry calibration." % (max_err, max_edge_err)
            )
            self.calibrating = False
            self.target_ring.hide()
            self.target_h.hide()
            self.target_v.hide()
            QtCore.QTimer.singleShot(300, self.start_calibration)
            return

        try:
            self._save_calibration()
        except Exception as exc:
            self._set_calibration_chrome_visible(True)
            self.status.setText(f"Calibration save failed: {exc}")
            self.hint.setText("Please rerun touch calibration.")
            return

        fit_hint = f"fit_avg={avg_err:.1f}px fit_max={max_err:.1f}px edge_max={max_edge_err:.1f}px"

        self.calibrating = False
        self.target_ring.hide()
        self.target_h.hide()
        self.target_v.hide()
        self._set_calibration_chrome_visible(True)
        self.status.setText(
            "Calibration saved a=%.5f b=%.5f c=%.1f d=%.5f e=%.5f f=%.1f"
            % (self.sx, self.sxy, self.ox, self.syx, self.sy, self.oy)
        )
        if self._linuxcnc_handoff_enabled():
            self.hint.setText(f"Returning to LinuxCNC... ({fit_hint})")
            self._begin_linuxcnc_handoff("calibration-complete")
        else:
            self.hint.setText(f"Touch-only mode calibrated. {fit_hint}")

