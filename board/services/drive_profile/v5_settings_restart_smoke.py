#!/usr/bin/env python3
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import v5_drive_bus_contract as contract
import v5_settings_action_runtime as action_runtime
import v5_settings_restart as restart


class SettingsRestartSmoke(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.run_dir = Path(self.temporary.name) / "run"
        self.run_dir_patch = mock.patch.object(restart, "RUN_DIR", self.run_dir)
        self.run_dir_patch.start()
        self.zero_binding_patch = mock.patch.object(
            restart,
            "migrate_active_rotary_drive_owner_for_restart",
            return_value={
                "ok": True,
                "code": "SETTINGS_MODEL_ROTARY_DRIVE_OWNER_VALID",
                "axis": "A",
                "inactive_axis": "B",
                "slave_position": 3,
                "donor_axis": "A",
                "zero_counts": 5.0,
                "counts_per_unit": 10000.0,
                "raw_zero_position": 0.0005,
                "changed": False,
            },
        )
        self.zero_binding_patch.start()
        self.rotary_profile_patch = mock.patch.object(
            restart,
            "sync_active_rotary_wcheckpoint_profiles_for_restart",
            return_value={
                "ok": True,
                "code": "SETTINGS_ROTARY_PROFILE_CREV_VALID",
                "active_axes": ["A", "C"],
                "counts_per_rev": {"A": 3600000, "C": 3600000},
                "changed": False,
            },
        )
        self.rotary_profile_patch.start()

    def tearDown(self) -> None:
        self.rotary_profile_patch.stop()
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
        self.write(
            contract.RUNTIME_SETTINGS_INI,
            "[TRAJ]\nCOORDINATES = X Y Z B C\n"
            "[AXIS_B]\nWCHECKPOINT_COUNTS_PER_REV = 3600000\n"
            "[AXIS_C]\nWCHECKPOINT_COUNTS_PER_REV = 3600000\n"
            "[JOINT_3]\nSCALE = 10000\n"
            "[JOINT_4]\nSCALE = 10000\n")
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
            "captured_at": "2026-07-13T15:54:16Z",
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
                    "counts_per_unit": 10000.0,
                    "raw_zero_position": 0.0005,
                    "captured_at": "2026-07-17T03:58:51Z",
                    "slave_position": 3,
                }, "rotary_load_counts_per_rev": 3600000,
                    "actual_counts_per_motor_rev": 3600000},
                {"axis": "B", "zero_model": b_zero,
                 "rotary_load_counts_per_rev": 360000,
                 "actual_counts_per_motor_rev": 360000},
                {"axis": "C", "zero_model": {
                    "zero_counts": 0.0,
                    "counts_per_unit": 10000.0,
                    "raw_zero_position": 0.0,
                    "slave_position": 4,
                }, "rotary_load_counts_per_rev": 3600000},
            ],
        }

    def test_bc_mapping_moves_slave_three_owner_without_copying_b_axis_scale(self) -> None:
        self.write(contract.SETTINGS_RUNTIME_JSON, json.dumps(self.runtime(self.b_zero())))
        result = restart.migrate_active_rotary_drive_owner_for_restart()
        self.assertTrue(result["changed"])
        self.assertEqual("B", result["axis"])
        self.assertEqual("A", result["donor_axis"])
        self.assertEqual(3, result["slave_position"])
        reread = json.loads(contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
        a_axis = next(item for item in reread["axes"] if item["axis"] == "A")
        b_axis = next(item for item in reread["axes"] if item["axis"] == "B")
        self.assertEqual({"axis": "A"}, a_axis)
        self.assertEqual(5.0, b_axis["zero_model"]["zero_counts"])
        self.assertEqual(10000.0, b_axis["zero_model"]["counts_per_unit"])
        self.assertEqual(3, b_axis["zero_model"]["slave_position"])
        self.assertNotIn("axis", b_axis["zero_model"].get("drive_position", {}))
        self.assertEqual(3600000, b_axis["rotary_load_counts_per_rev"])
        self.assertEqual(3600000, b_axis["actual_counts_per_motor_rev"])
        idempotent = restart.migrate_active_rotary_drive_owner_for_restart()
        self.assertFalse(idempotent["changed"])
        handoff = restart.run_restart_handoff(
            "settings_save_and_restart", {"owner": "settings_restart"})
        self.assertTrue(handoff["ok"])
        self.assertTrue(Path(handoff["handoff_script"]).is_file())
        self.assertFalse(handoff["rotary_wcheckpoint_profile"]["changed"])
        ini_text = contract.RUNTIME_SETTINGS_INI.read_text(encoding="utf-8")
        self.assertIn(
            "[AXIS_B]\nWCHECKPOINT_COUNTS_PER_REV = 3600000", ini_text)
        self.assertIn(
            "[AXIS_C]\nWCHECKPOINT_COUNTS_PER_REV = 3600000", ini_text)

    def test_stale_b_axis_zero_for_other_slave_is_removed(self) -> None:
        self.write(contract.SETTINGS_RUNTIME_JSON, json.dumps(self.runtime(self.b_zero("4"))))
        result = restart.run_restart_handoff(
            "settings_save_and_restart", {"owner": "settings_restart"})
        self.assertTrue(result["ok"])
        self.assertTrue(result["restart_commit_required"])
        reread = json.loads(contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
        a_axis = next(item for item in reread["axes"] if item["axis"] == "A")
        b_axis = next(item for item in reread["axes"] if item["axis"] == "B")
        self.assertEqual({"axis": "A"}, a_axis)
        self.assertEqual(5.0, b_axis["zero_model"]["zero_counts"])
        self.assertEqual(3, b_axis["zero_model"]["slave_position"])

    def test_wrong_zero_scale_cannot_restart_with_active_runtime_scale(self) -> None:
        runtime = self.runtime(self.b_zero())
        c_axis = next(item for item in runtime["axes"] if item["axis"] == "C")
        c_axis["zero_model"]["counts_per_unit"] = 1000.0
        c_axis["rotary_load_counts_per_rev"] = 360000
        self.write(contract.SETTINGS_RUNTIME_JSON, json.dumps(runtime))
        with self.assertRaises(contract.DriveActionError) as caught:
            restart.sync_active_rotary_wcheckpoint_profiles_for_restart()
        self.assertEqual(
            "SETTINGS_ROTARY_PROFILE_SCALE_CHAIN_MISMATCH",
            caught.exception.code)


if __name__ == "__main__":
    unittest.main()
