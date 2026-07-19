#!/usr/bin/env python3
from __future__ import annotations

import ast
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import v5_settings_action_runtime as action_runtime
import v5_settings_restart as restart


class SettingsRestartSmoke(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.run_dir = Path(self.temporary.name) / "run"
        self.run_dir_patch = mock.patch.object(restart, "RUN_DIR", self.run_dir)
        self.run_dir_patch.start()

    def tearDown(self) -> None:
        self.run_dir_patch.stop()
        self.temporary.cleanup()

    def test_prepare_returns_terminal_result_without_starting_reboot(self) -> None:
        with mock.patch.object(restart.subprocess, "Popen") as popen:
            result = restart.run_restart_handoff(
                "settings_save_and_restart",
                {"owner": "settings_restart"},
            )

        self.assertTrue(result["ok"])
        self.assertEqual("SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED", result["code"])
        self.assertFalse(result["write_executed"])
        self.assertFalse(result["restart_executed"])
        self.assertTrue(result["restart_commit_required"])
        self.assertEqual("board_reboot_after_ui_ack", result["clean_restart_equivalent"])
        self.assertRegex(result["generated_at"], r"^\d{4}-\d{2}-\d{2}T")
        self.assertNotIn("rotary_drive_owner", result)
        self.assertNotIn("rotary_wcheckpoint_profile", result)
        handoff_script = Path(result["handoff_script"])
        self.assertTrue(handoff_script.is_file())
        script_text = handoff_script.read_text(encoding="utf-8")
        self.assertIn("reboot -f", script_text)
        self.assertLess(script_text.index('while [ ! -f "$GO" ]'), script_text.index("sleep 1.0"))
        self.assertLess(script_text.index("sleep 1.0"), script_text.index("sync || true"))
        popen.assert_not_called()

    def test_prepare_contains_only_restart_handoff_operations(self) -> None:
        result = restart.run_restart_handoff(
            "settings_save_and_restart",
            {"owner": "settings_restart"},
        )
        self.assertTrue(result["ok"])
        source = Path(restart.__file__).read_text(encoding="utf-8")
        functions = {
            node.name
            for node in ast.parse(source).body
            if isinstance(node, ast.FunctionDef)
        }
        self.assertEqual({
            "restart_handoff_paths",
            "remove_runtime_marker",
            "now_utc",
            "run_restart_handoff",
            "commit_restart_handoff",
            "release_restart_handoff",
            "abort_restart_handoff",
        }, functions)

    def test_commit_arms_prepared_handoff_and_release_opens_gate(self) -> None:
        restart.run_restart_handoff(
            "settings_save_and_restart",
            {"owner": "settings_restart"},
        )
        with mock.patch.object(restart.subprocess, "Popen") as popen:
            result = restart.commit_restart_handoff()

        self.assertTrue(result["ok"])
        self.assertTrue(result["accepted"])
        self.assertEqual("SETTINGS_SAVE_RESTART_COMMIT_ACK", result["code"])
        self.assertEqual(1.0, result["handoff_delay_s"])
        _script, _log, armed_marker, go_marker = restart.restart_handoff_paths()
        self.assertTrue(armed_marker.is_file())
        self.assertFalse(go_marker.exists())
        popen.assert_called_once()
        release = restart.release_restart_handoff()
        self.assertTrue(release["ok"])
        self.assertEqual("SETTINGS_SAVE_RESTART_COMMIT_RELEASED", release["code"])
        self.assertFalse(armed_marker.exists())
        self.assertTrue(go_marker.is_file())

    def test_commit_spawn_failure_is_a_result_not_a_secondary_exception(self) -> None:
        restart.run_restart_handoff(
            "settings_save_and_restart",
            {"owner": "settings_restart"},
        )
        with mock.patch.object(
            restart.subprocess,
            "Popen",
            side_effect=OSError("spawn denied"),
        ):
            result = restart.commit_restart_handoff()

        self.assertFalse(result["ok"])
        self.assertFalse(result["accepted"])
        self.assertEqual("SETTINGS_SAVE_RESTART_COMMIT_SPAWN_FAILED", result["code"])
        self.assertIn("OSError: spawn denied", result["detail"])
        _script, _log, armed_marker, go_marker = restart.restart_handoff_paths()
        self.assertFalse(armed_marker.exists())
        self.assertFalse(go_marker.exists())


class SettingsAxisZeroRuntimeSmoke(unittest.TestCase):
    def run_axis_zero(self, drive_result: dict) -> tuple[dict, dict | None]:
        with tempfile.TemporaryDirectory() as temporary:
            result_path = Path(temporary) / "axis-zero-result.json"
            spec = dict(action_runtime.ACTIONS["settings_axis_zero"])
            spec["result_path"] = str(result_path)
            with mock.patch.dict(
                action_runtime.ACTIONS,
                {"settings_axis_zero": spec},
            ), mock.patch.object(
                action_runtime.v5_drive_bus_action,
                "run_action",
                return_value=dict(drive_result),
            ):
                result = action_runtime.execute_action(
                    "settings_axis_zero", {"axis": "B", "slave_position": 3}
                )
            persisted = (
                json.loads(result_path.read_text(encoding="utf-8"))
                if result_path.is_file()
                else None
            )
        return result, persisted

    def test_axis_zero_preserves_drive_success_without_restarting_publisher(self) -> None:
        drive_result = {
            "ok": True,
            "code": "SETTINGS_AXIS_ZERO_OK",
            "axis": "B",
            "zero_counts": 1234,
            "raw_zero_position": 1.234,
            "backend_restart_required": True,
            "canonical_clean_restart_required": True,
            "restart_deferred": True,
            "drive_position": {"readback": {"actual_position_counts": 1234}},
        }
        result, persisted = self.run_axis_zero(drive_result)
        self.assertTrue(result["ok"])
        self.assertEqual(drive_result["code"], result["code"])
        self.assertNotIn("position_publisher_reload", result)
        self.assertIsNotNone(persisted)
        self.assertEqual(drive_result, persisted)
        self.assertEqual(1234, persisted["drive_position"]["readback"]["actual_position_counts"])

    def test_axis_zero_preserves_drive_failure_without_restart_override(self) -> None:
        drive_result = {
            "ok": False,
            "code": "SETTINGS_AXIS_ZERO_READBACK_MISMATCH",
            "axis": "B",
            "message_cn": "设0源位置读回不一致。",
            "drive_position": {"readback": {"actual_position_counts": 1200}},
        }
        result, persisted = self.run_axis_zero(drive_result)
        self.assertFalse(result["ok"])
        self.assertEqual(drive_result["code"], result["code"])
        self.assertNotIn("position_publisher_reload", result)
        self.assertIsNone(persisted)

    def test_action_runtime_contains_no_publisher_restart_path(self) -> None:
        self.assertFalse(hasattr(action_runtime, "reload_position_publisher"))
        source = Path(action_runtime.__file__).read_text(encoding="utf-8")
        for token in (
            "reload_position_publisher",
            "position_publisher_reload",
            "SETTINGS_AXIS_ZERO_POSITION_RELOAD_FAILED",
            "/etc/init.d/v5-wcs-status-publisher",
            "subprocess.run",
        ):
            self.assertNotIn(token, source)


if __name__ == "__main__":
    unittest.main()
