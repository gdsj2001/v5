#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

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
        self.assertFalse(result["restart_executed"])
        self.assertTrue(result["restart_commit_required"])
        self.assertEqual("board_reboot_after_ui_ack", result["clean_restart_equivalent"])
        self.assertRegex(result["generated_at"], r"^\d{4}-\d{2}-\d{2}T")
        handoff_script = Path(result["handoff_script"])
        self.assertTrue(handoff_script.is_file())
        script_text = handoff_script.read_text(encoding="utf-8")
        self.assertIn("reboot -f", script_text)
        self.assertLess(script_text.index('while [ ! -f "$GO" ]'), script_text.index("sleep 1.0"))
        self.assertLess(script_text.index("sleep 1.0"), script_text.index("sync || true"))
        popen.assert_not_called()

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
        self.assertEqual("SETTINGS_SAVE_RESTART_COMMIT_GATE_RELEASED", release["code"])
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


if __name__ == "__main__":
    unittest.main()
