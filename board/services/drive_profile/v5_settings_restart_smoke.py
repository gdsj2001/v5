#!/usr/bin/env python3
from __future__ import annotations

import subprocess
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
        self.sleep_patch = mock.patch.object(restart.time, "sleep", return_value=None)
        self.run_dir_patch.start()
        self.sleep_patch.start()

    def tearDown(self) -> None:
        self.sleep_patch.stop()
        self.run_dir_patch.stop()
        self.temporary.cleanup()

    def test_success_returns_terminal_reboot_result_without_exception(self) -> None:
        with mock.patch.object(restart.subprocess, "Popen") as popen:
            result = restart.run_restart_handoff(
                "settings_save_and_restart",
                {"owner": "settings_restart"},
            )

        self.assertTrue(result["ok"])
        self.assertEqual("SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED", result["code"])
        self.assertTrue(result["restart_executed"])
        self.assertEqual("board_reboot", result["clean_restart_equivalent"])
        self.assertRegex(result["generated_at"], r"^\d{4}-\d{2}-\d{2}T")
        handoff_script = Path(result["handoff_script"])
        self.assertTrue(handoff_script.is_file())
        self.assertIn("reboot -f", handoff_script.read_text(encoding="utf-8"))
        popen.assert_called_once()

    def test_spawn_failure_is_a_result_not_a_secondary_exception(self) -> None:
        with mock.patch.object(
            restart.subprocess,
            "Popen",
            side_effect=OSError("spawn denied"),
        ):
            result = restart.run_restart_handoff(
                "settings_save_and_restart",
                {"owner": "settings_restart"},
            )

        self.assertFalse(result["ok"])
        self.assertEqual("SETTINGS_SAVE_RESTART_HANDOFF_SPAWN_FAILED", result["code"])
        self.assertFalse(result["restart_executed"])
        self.assertEqual("", result["clean_restart_equivalent"])
        self.assertIn("OSError: spawn denied", result["detail"])


if __name__ == "__main__":
    unittest.main()
