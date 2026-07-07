#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import subprocess
import time

from v5_touch_core import *  # noqa: F401,F403


class TouchCalibrationRestartMixin:
    def _request_restart_via_helper(self) -> bool:
        def _result(accepted=False, completed=False, ok=False, state="failed", detail=""):
            return {
                "accepted": bool(accepted),
                "completed": bool(completed),
                "ok": bool(ok),
                "state": str(state),
                "detail": str(detail),
            }

        if not (os.path.isfile(RESTART_HELPER) and os.access(RESTART_HELPER, os.X_OK)):
            self.status.setText("LinuxCNC restart helper missing.")
            self.hint.setText(RESTART_HELPER)
            return _result(False, False, False, "failed", "helper-missing")
        worker_result = {"done": False, "ok": False, "rc": -1, "out": ""}

        def _worker():
            try:
                result = subprocess.run(
                    [RESTART_HELPER],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    check=False,
                    timeout=25.0,
                )
                worker_result["done"] = True
                worker_result["ok"] = (result.returncode == 0)
                worker_result["rc"] = int(result.returncode)
                worker_result["out"] = (result.stdout or "").strip()
                if result.returncode != 0:
                    output = (result.stdout or "").strip()
                    print(
                        f"[touch-cal] restart helper failed rc={result.returncode} out={output[:180]}",
                        file=sys.stderr,
                    )
            except subprocess.TimeoutExpired:
                worker_result["done"] = True
                worker_result["ok"] = False
                worker_result["rc"] = 124
                print("[touch-cal] restart helper timeout", file=sys.stderr)
            except Exception as exc:
                worker_result["done"] = True
                worker_result["ok"] = False
                worker_result["rc"] = 125
                worker_result["out"] = repr(exc)
                print(f"[touch-cal] restart helper exception: {exc}", file=sys.stderr)

        try:
            th = threading.Thread(
                target=_worker,
                name=f"touch-restart-helper-{int(time.time() * 1000)}",
                daemon=True,
            )
            th.start()
            # Helper includes process cleanup and can take >0.2s on busy boards.
            th.join(timeout=2.0)
            if worker_result["done"]:
                if worker_result["ok"]:
                    output = str(worker_result.get("out", ""))
                    if "DIRECT_LAUNCH" in output:
                        self.status.setText("LinuxCNC direct launch dispatched.")
                        self.hint.setText("Leaving calibration UI and switching to LinuxCNC.")
                        return _result(True, True, True, "direct-launched", "helper-direct-launch")
                    self.status.setText("LinuxCNC restart helper completed.")
                    self.hint.setText(f"Waiting for {LINUXCNC_SERVICE}.")
                    return _result(True, True, True, "completed", "rc=0")
                self.status.setText("LinuxCNC restart helper failed.")
                self.hint.setText(
                    f"rc={worker_result['rc']} out={str(worker_result.get('out', ''))[:180]}"
                )
                return _result(False, False, False, "failed", f"rc={worker_result['rc']}")
            self.status.setText("LinuxCNC restart helper dispatched (pending).")
            self.hint.setText(f"Waiting for {LINUXCNC_SERVICE} after helper launch.")
            return _result(True, False, False, "pending", "dispatched")
        except Exception as exc:
            self.status.setText("LinuxCNC restart helper dispatch failed.")
            self.hint.setText(str(exc))
            return _result(False, False, False, "failed", "dispatch-exception")

    def _map_screen_point(self, raw_x, raw_y):
        rx = float(raw_x)
        ry = float(raw_y)
        px = int(round((rx * self.sx) + (ry * self.sxy) + self.ox))
        py = int(round((rx * self.syx) + (ry * self.sy) + self.oy))
        px = max(0, min(int(self.report_width) - 1, px))
        py = max(0, min(int(self.report_height) - 1, py))
        return px, py

    def _update_touch_overlay(self, raw_x, raw_y):
        px, py = self._map_screen_point(raw_x, raw_y)
        self.raw_label.setText(f"raw=({raw_x}, {raw_y}) screen=({px}, {py})")
        self.touch_h.setGeometry(0, py, int(self.report_width), 2)
        self.touch_v.setGeometry(px, 0, 2, int(self.report_height))
        self.touch_h.show()
        self.touch_v.show()

    def _show_target(self):
        tx, ty = self.cal_targets[self.cal_index]
        self.target_ring.setGeometry(tx - 28, ty - 28, 56, 56)
        self.target_h.setGeometry(tx - 18, ty, 36, 2)
        self.target_v.setGeometry(tx, ty - 18, 2, 36)
        self.target_ring.show()
        self.target_h.show()
        self.target_v.show()

    def _set_calibration_chrome_visible(self, visible):
        for widget in (
            self.title,
            self.status,
            self.hint,
            self.btn_exit,
            self.btn_start_linuxcnc,
            self.raw_label,
        ):
            widget.setVisible(bool(visible))

    def start_calibration(self):
        if self.restart_wait_timer.isActive():
            self.restart_wait_timer.stop()
        if not self.touch_device_ready:
            self._set_calibration_chrome_visible(True)
            self.calibrating = False
            self.status.setText("Touch device unavailable.")
            self.hint.setText("Check touch device then press Retry Touch Detect.")
            return
        self._set_calibration_chrome_visible(False)
        self.calibrating = True
        self.cal_index = 0
        self.cal_samples = []
        self.current_raw = None
        self.wait_start_release = False
        self.wait_release = False
        self.release_guard_deadline = 0.0
        self.ignore_until = time.time() + 0.25
        self._show_target()
        self.status.setText(f"Tap target 1/{len(self.cal_targets)}")

    def _apply_runtime_calibration(self):
        if not (os.path.isfile(APPLY_HELPER) and os.access(APPLY_HELPER, os.X_OK)):
            raise RuntimeError(
                "apply helper missing, searched: %s"
                % " ; ".join(APPLY_HELPER_CANDIDATES)
            )
        result = subprocess.run(
            [APPLY_HELPER],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            output = (result.stdout or "").strip()
            raise RuntimeError(f"apply helper failed rc={result.returncode} output={output}")

    def _linuxcnc_handoff_enabled(self) -> bool:
        return bool(self.enable_linuxcnc or self.force_linuxcnc_handoff)

    def _request_linuxcnc_restart(self):
        def _result(accepted=False, completed=False, ok=False, state="failed", detail=""):
            return {
                "accepted": bool(accepted),
                "completed": bool(completed),
                "ok": bool(ok),
                "state": str(state),
                "detail": str(detail),
            }

        if not self._linuxcnc_handoff_enabled():
            return _result(False, False, False, "failed", "linuxcnc-disabled")
        self.status.setText("LinuxCNC restart helper dispatching.")
        self.hint.setText(f"Executing {RESTART_HELPER}.")
        return self._request_restart_via_helper()

    def _begin_linuxcnc_handoff(self, context: str) -> bool:
        restart_result = self._request_linuxcnc_restart()
        if not restart_result.get("accepted", False):
            self.status.setText("LinuxCNC restart request failed.")
            self.hint.setText("Check restart path/helper and v5-ui-relay, then retry.")
            return False
        if self.restart_wait_timer.isActive():
            self.restart_wait_timer.stop()
        state = restart_result.get("state", "")
        self.status.setText("LinuxCNC launch dispatched. Leaving calibration UI.")
        self.hint.setText(f"init.d helper state={state or 'accepted'} ({context}).")
        QtCore.QTimer.singleShot(200, QtWidgets.QApplication.instance().quit)
        return True

    def _poll_linuxcnc_service_active(self):
        if not self._linuxcnc_handoff_enabled():
            self.restart_wait_timer.stop()
            return
        self.restart_wait_timer.stop()
        self.status.setText("LinuxCNC launch dispatched. Leaving calibration UI.")
        self.hint.setText(f"Handoff completed ({self.restart_wait_context}).")
        QtCore.QTimer.singleShot(150, QtWidgets.QApplication.instance().quit)

    def _exit_touch_calibration(self):
        self._set_calibration_chrome_visible(True)
        if not self.touch_device_ready:
            self.status.setText("Retrying touch device detection ...")
            self.hint.setText("If still missing, verify /dev/input/by-path/z20-touchscreen.")
            self._init_touch_reader()
            return
        if not self._linuxcnc_handoff_enabled():
            self.calibrating = False
            self.target_ring.hide()
            self.target_h.hide()
            self.target_v.hide()
            self.status.setText("Calibration UI reset.")
            self.hint.setText("Touch-only mode keeps UI foreground; restarting calibration.")
            QtCore.QTimer.singleShot(150, self.start_calibration)
            return
        self.calibrating = False
        self.target_ring.hide()
        self.target_h.hide()
        self.target_v.hide()
        self.status.setText("Touch calibration exited. Returning to LinuxCNC.")
        self.hint.setText("Requesting LinuxCNC restart.")
        self._begin_linuxcnc_handoff("exit")

