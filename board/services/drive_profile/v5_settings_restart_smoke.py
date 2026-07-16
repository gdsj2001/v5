#!/usr/bin/env python3
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import v5_drive_bus_contract as contract
import v5_settings_restart as restart


class SettingsRestartSmoke(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.run_dir = Path(self.temporary.name) / "run"
        self.run_dir_patch = mock.patch.object(restart, "RUN_DIR", self.run_dir)
        self.run_dir_patch.start()
        self.zero_binding_patch = mock.patch.object(
            restart,
            "ensure_active_rotary_zero_binding_for_restart",
            return_value={
                "ok": True,
                "code": "SETTINGS_MODEL_ROTARY_ZERO_BINDING_VALID",
                "axis": "A",
                "inactive_axis": "B",
                "slave_position": 3,
                "zero_counts": 5.0,
                "counts_per_unit": 1000.0,
                "raw_zero_position": 0.005,
                "changed": False,
            },
        )
        self.zero_binding_patch.start()

    def tearDown(self) -> None:
        self.zero_binding_patch.stop()
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


class SettingsModelZeroBindingSmoke(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.path_patches = [
            mock.patch.object(contract, "SETTINGS_RUNTIME_JSON", self.root / "settings_runtime.json"),
            mock.patch.object(contract, "RUNTIME_SETTINGS_INI", self.root / "v5_bus.ini"),
            mock.patch.object(contract, "SELF_PARAMETER_TABLE", self.root / "self_parameter_table.tsv"),
            mock.patch.object(restart, "RUN_DIR", self.root / "run"),
        ]
        for patcher in self.path_patches:
            patcher.start()
        self.write(contract.RUNTIME_SETTINGS_INI, "[TRAJ]\nCOORDINATES = X Y Z B C\n")
        self.write(contract.SELF_PARAMETER_TABLE, "A\tslave\tNAT\nB\tslave\t3\n")

    def tearDown(self) -> None:
        for patcher in reversed(self.path_patches):
            patcher.stop()
        self.temporary.cleanup()

    @staticmethod
    def write(path: Path, text: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    @staticmethod
    def b_zero(position: str = "3") -> dict:
        return {
            "zero_counts": 0.0,
            "counts_per_unit": 1000.0,
            "raw_zero_position": 0.0,
            "drive_position": {
                "axis": "B",
                "actual_position_counts": 0.0,
                "readback": {"upload": {"argv": [
                    "ethercat", "upload", "-p", position,
                    "-t", "int32", "0x6064", "0x00",
                ]}},
            },
        }

    def runtime(self, b_zero: dict) -> dict:
        return {
            "schema": contract.SETTINGS_RUNTIME_SCHEMA,
            "axes": [
                {"axis": "A", "zero_model": {
                    "zero_counts": 5.0,
                    "counts_per_unit": 1000.0,
                    "raw_zero_position": 0.005,
                    "slave_position": 3,
                }},
                {"axis": "B", "zero_model": b_zero},
            ],
        }

    def test_bc_rebind_uses_b_evidence_without_copying_a_zero(self) -> None:
        self.write(contract.SETTINGS_RUNTIME_JSON, json.dumps(self.runtime(self.b_zero())))
        result = restart.ensure_active_rotary_zero_binding_for_restart()
        self.assertTrue(result["changed"])
        self.assertEqual("B", result["axis"])
        self.assertEqual(3, result["slave_position"])
        reread = json.loads(contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
        a_axis = next(item for item in reread["axes"] if item["axis"] == "A")
        b_axis = next(item for item in reread["axes"] if item["axis"] == "B")
        self.assertEqual(5.0, a_axis["zero_model"]["zero_counts"])
        self.assertEqual(0.0, b_axis["zero_model"]["zero_counts"])
        self.assertEqual(3, b_axis["zero_model"]["slave_position"])
        idempotent = restart.ensure_active_rotary_zero_binding_for_restart()
        self.assertFalse(idempotent["changed"])
        handoff = restart.run_restart_handoff(
            "settings_save_and_restart", {"owner": "settings_restart"})
        self.assertTrue(handoff["ok"])
        self.assertTrue(Path(handoff["handoff_script"]).is_file())

    def test_conflicting_b_evidence_rejects_restart(self) -> None:
        self.write(contract.SETTINGS_RUNTIME_JSON, json.dumps(self.runtime(self.b_zero("4"))))
        result = restart.run_restart_handoff(
            "settings_save_and_restart", {"owner": "settings_restart"})
        self.assertFalse(result["ok"])
        self.assertEqual("SETTINGS_MODEL_ROTARY_ZERO_SLAVE_MISMATCH", result["code"])
        self.assertFalse(result["restart_commit_required"])
        self.assertFalse((restart.RUN_DIR / "settings_clean_restart_handoff.sh").exists())


if __name__ == "__main__":
    unittest.main()
