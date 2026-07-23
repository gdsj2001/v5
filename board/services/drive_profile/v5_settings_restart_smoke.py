#!/usr/bin/env python3
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import v5_drive_boot_validation as boot_apply
import v5_settings_action_runtime as action_runtime
import v5_settings_restart as restart


class SettingsRestartHandoffSmoke(unittest.TestCase):
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
        self.assertEqual(
            "SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED", result["code"])
        self.assertFalse(result["write_executed"])
        self.assertFalse(result["restart_executed"])
        self.assertTrue(result["restart_commit_required"])
        self.assertEqual(
            "board_reboot_after_ui_ack", result["clean_restart_equivalent"])
        handoff_script = Path(result["handoff_script"])
        self.assertTrue(handoff_script.is_file())
        self.assertIn("reboot -f", handoff_script.read_text(encoding="utf-8"))
        popen.assert_not_called()

    def test_commit_and_release_open_the_reboot_gate(self) -> None:
        restart.run_restart_handoff(
            "settings_save_and_restart",
            {"owner": "settings_restart"},
        )
        with mock.patch.object(restart.subprocess, "Popen") as popen:
            committed = restart.commit_restart_handoff()

        self.assertTrue(committed["ok"])
        self.assertEqual("SETTINGS_SAVE_RESTART_COMMIT_ACK", committed["code"])
        _script, _log, armed_marker, go_marker = restart.restart_handoff_paths()
        self.assertTrue(armed_marker.is_file())
        self.assertFalse(go_marker.exists())
        popen.assert_called_once()
        released = restart.release_restart_handoff()
        self.assertTrue(released["ok"])
        self.assertFalse(armed_marker.exists())
        self.assertTrue(go_marker.is_file())


class SettingsSaveRestartRuntimeSmoke(unittest.TestCase):
    def test_save_restart_only_prepares_reboot_handoff(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result_path = Path(temporary) / "settings-save-restart-result.json"
            spec = dict(action_runtime.ACTIONS["settings_save_and_restart"])
            spec["result_path"] = str(result_path)
            handoff = {
                "schema": "v5.settings_action_result.v1",
                "action": "settings_save_and_restart",
                "owner": "settings_restart",
                "ok": True,
                "code": "SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED",
                "restart_commit_required": True,
                "write_executed": False,
                "restart_executed": False,
            }
            with mock.patch.dict(
                action_runtime.ACTIONS,
                {"settings_save_and_restart": spec},
            ), mock.patch.object(
                action_runtime,
                "run_restart_handoff",
                return_value=dict(handoff),
            ) as restart_handoff, mock.patch.object(
                action_runtime.v5_drive_bus_action,
                "preload_resident_state",
            ) as preload, mock.patch.object(
                action_runtime.v5_drive_bus_action,
                "run_action",
            ) as drive_action:
                result = action_runtime.execute_action(
                    "settings_save_and_restart",
                    {"action": "settings_save_and_restart", "_run_id": "run-1"},
                )

            persisted = json.loads(result_path.read_text(encoding="utf-8"))

        self.assertTrue(result["ok"])
        self.assertEqual(handoff["code"], result["code"])
        self.assertEqual(result, persisted)
        self.assertNotIn("drive_sync", result)
        preload.assert_not_called()
        drive_action.assert_not_called()
        restart_handoff.assert_called_once_with(
            "settings_save_and_restart", spec)


class SettingsDriveOwnerReloadSmoke(unittest.TestCase):
    def test_drive_action_reloads_current_owners_before_preflight(self) -> None:
        events: list[str] = []

        def preload() -> dict:
            events.append("reload")
            return {
                "ok": True,
                "settings_runtime_loaded": True,
                "runtime_ini_loaded": True,
                "self_slave_bindings_loaded": True,
                "drive_snapshot_loaded": True,
            }

        def run_action(*_args, **_kwargs) -> dict:
            events.append("preflight")
            return {
                "ok": True,
                "code": "DRIVE_SET_RESTART_REQUIRED",
                "write_executed": False,
                "drive_write_executed": False,
                "motion_executed": False,
            }

        with mock.patch.object(
            action_runtime.v5_drive_bus_action,
            "preload_resident_state",
            side_effect=preload,
        ), mock.patch.object(
            action_runtime.v5_drive_bus_action,
            "run_action",
            side_effect=run_action,
        ), mock.patch.object(
            action_runtime.v5_drive_bus_action,
            "write_json",
        ):
            result = action_runtime.run_drive_action_from_current_owners(
                "set-drive", 8.0, {"trigger": "settings_set_drive"})

        self.assertEqual(["reload", "preflight"], events)
        self.assertTrue(result["owner_reload"]["ok"])
        self.assertFalse(result["drive_write_executed"])

    def test_incomplete_owner_reload_rejects_without_drive_action(self) -> None:
        with mock.patch.object(
            action_runtime.v5_drive_bus_action,
            "preload_resident_state",
            return_value={"ok": False, "runtime_ini_loaded": False},
        ), mock.patch.object(
            action_runtime.v5_drive_bus_action,
            "run_action",
        ) as run_action, mock.patch.object(
            action_runtime.v5_drive_bus_action,
            "write_json",
        ):
            result = action_runtime.run_drive_action_from_current_owners(
                "set-drive", 8.0, {"trigger": "settings_set_drive"})

        self.assertFalse(result["ok"])
        self.assertEqual("DRIVE_ACTION_OWNER_RELOAD_FAILED", result["code"])
        run_action.assert_not_called()


class PostRestartDriveApplySmoke(unittest.TestCase):
    def test_boot_apply_uses_final_owner_and_persists_current_boot_result(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result_path = Path(temporary) / "drive-boot-apply-result.json"
            boot_id_path = Path(temporary) / "boot_id"
            boot_id_path.write_text("boot-smoke\n", encoding="ascii")
            raw = {
                "ok": True,
                "code": "DRIVE_SET_OK",
                "drive_write_executed": True,
                "motion_executed": False,
            }
            native_attestation = {
                "ok": True,
                "code": "BACKEND_DRIVE_VERIFIED",
                "generation": 123,
                "owner_pid": 456,
                "owner_start_ticks": 789,
                "motion_ready": True,
            }
            current_backend = {
                "generation": 123,
                "owner_pid": 456,
                "owner_start_ticks": 789,
                "fields": {
                    "drive_verified": "1",
                    "motion_ready": "1",
                },
            }
            with mock.patch.object(
                boot_apply, "BOOT_APPLY_RESULT_PATH", result_path,
            ), mock.patch.object(
                boot_apply, "BOOT_ID_PATH", boot_id_path,
            ), mock.patch.object(
                boot_apply,
                "probe_axis_slave_mapping",
                return_value={"ok": True, "available": True, "applicable": True},
            ), mock.patch.object(
                boot_apply.v5_drive_bus_action,
                "preload_resident_state",
                return_value={"ok": True},
            ) as preload, mock.patch.object(
                boot_apply.v5_drive_bus_action,
                "run_action",
                return_value=raw,
            ) as run_action, mock.patch.object(
                boot_apply,
                "attest_boot_drive_generation",
                return_value=native_attestation,
            ) as attest, mock.patch.object(
                boot_apply,
                "backend_data_identity",
                return_value=current_backend,
            ):
                first = boot_apply.run_post_restart_drive_apply(5.0)
                second = boot_apply.run_post_restart_drive_apply(5.0)

        self.assertTrue(first["ok"])
        self.assertEqual("DRIVE_BOOT_APPLY_OK", first["code"])
        self.assertEqual("boot-smoke", first["boot_id"])
        self.assertTrue(first["drive_write_executed"])
        self.assertEqual(native_attestation, first["native_attestation"])
        self.assertEqual(first, second)
        preload.assert_called_once_with()
        run_action.assert_called_once()
        attest.assert_called_once_with(raw, 3.0)
        self.assertEqual("boot-apply", run_action.call_args.args[0])
        self.assertEqual("post_restart_boot", run_action.call_args.args[3]["trigger"])

    def test_cached_result_is_rebuilt_for_new_backend_generation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result_path = Path(temporary) / "drive-boot-apply-result.json"
            boot_id_path = Path(temporary) / "boot_id"
            boot_id_path.write_text("boot-smoke\n", encoding="ascii")
            result_path.write_text(json.dumps({
                "schema": boot_apply.BOOT_APPLY_RESULT_SCHEMA,
                "generated_at": "2026-07-23T00:00:00Z",
                "boot_id": "boot-smoke",
                "ok": True,
                "code": "DRIVE_BOOT_APPLY_OK",
                "native_attestation": {
                    "ok": True,
                    "generation": 122,
                    "owner_pid": 455,
                    "owner_start_ticks": 788,
                    "motion_ready": True,
                },
            }), encoding="utf-8")
            current_backend = {
                "generation": 123,
                "owner_pid": 456,
                "owner_start_ticks": 789,
                "fields": {
                    "drive_verified": "0",
                    "motion_ready": "0",
                },
            }
            raw = {
                "ok": True,
                "code": "DRIVE_SET_OK",
                "drive_write_executed": False,
                "motion_executed": False,
            }
            refreshed_attestation = {
                "ok": True,
                "code": "BACKEND_DRIVE_VERIFIED",
                "generation": 123,
                "owner_pid": 456,
                "owner_start_ticks": 789,
                "motion_ready": True,
            }
            with mock.patch.object(
                boot_apply, "BOOT_APPLY_RESULT_PATH", result_path,
            ), mock.patch.object(
                boot_apply, "BOOT_ID_PATH", boot_id_path,
            ), mock.patch.object(
                boot_apply, "backend_data_identity", return_value=current_backend,
            ), mock.patch.object(
                boot_apply, "probe_axis_slave_mapping",
                return_value={"ok": True, "available": True, "applicable": True},
            ), mock.patch.object(
                boot_apply.v5_drive_bus_action, "preload_resident_state",
                return_value={"ok": True},
            ), mock.patch.object(
                boot_apply.v5_drive_bus_action, "run_action", return_value=raw,
            ) as run_action, mock.patch.object(
                boot_apply, "attest_boot_drive_generation",
                return_value=refreshed_attestation,
            ):
                result = boot_apply.run_post_restart_drive_apply(5.0)

        self.assertTrue(result["ok"])
        self.assertEqual(refreshed_attestation, result["native_attestation"])
        run_action.assert_called_once()

    def test_mapping_not_ready_is_transient_and_never_writes(self) -> None:
        with mock.patch.object(
            boot_apply,
            "probe_axis_slave_mapping",
            return_value={"ok": False, "available": False},
        ), mock.patch.object(
            boot_apply.v5_drive_bus_action,
            "run_action",
        ) as run_action:
            result = boot_apply.run_post_restart_drive_apply(5.0)

        self.assertFalse(result["ok"])
        self.assertEqual("DRIVE_BOOT_NATIVE_MAPPING_NOT_READY", result["code"])
        self.assertFalse(result["drive_write_executed"])
        run_action.assert_not_called()

    def test_mapping_socket_startup_race_is_transient_and_never_writes(self) -> None:
        with mock.patch.object(
            boot_apply,
            "probe_axis_slave_mapping",
            side_effect=FileNotFoundError(2, "command gate socket not ready"),
        ), mock.patch.object(
            boot_apply.v5_drive_bus_action,
            "run_action",
        ) as run_action:
            result = boot_apply.run_post_restart_drive_apply(5.0)

        self.assertFalse(result["ok"])
        self.assertEqual("DRIVE_BOOT_NATIVE_MAPPING_NOT_READY", result["code"])
        self.assertIn("FileNotFoundError", result["detail"])
        self.assertFalse(result["drive_write_executed"])
        run_action.assert_not_called()


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
                "preload_resident_state",
                return_value={
                    "ok": True,
                    "settings_runtime_loaded": True,
                    "runtime_ini_loaded": True,
                    "self_slave_bindings_loaded": True,
                    "drive_snapshot_loaded": True,
                },
            ), mock.patch.object(
                action_runtime.v5_drive_bus_action,
                "write_json",
            ), mock.patch.object(
                action_runtime.v5_drive_bus_action,
                "run_action",
                return_value=dict(drive_result),
            ):
                result = action_runtime.execute_action(
                    "settings_axis_zero", {"axis": "B", "slave_position": 3})
            persisted = (
                json.loads(result_path.read_text(encoding="utf-8"))
                if result_path.is_file() else None)
        return result, persisted

    def test_axis_zero_preserves_drive_result_without_publisher_restart(self) -> None:
        drive_result = {
            "ok": True,
            "code": "SETTINGS_AXIS_ZERO_OK",
            "axis": "B",
            "zero_counts": 1234,
            "drive_position": {"readback": {"actual_position_counts": 1234}},
        }
        result, persisted = self.run_axis_zero(drive_result)
        self.assertTrue(result["ok"])
        self.assertEqual(drive_result["code"], result["code"])
        self.assertNotIn("position_publisher_reload", result)
        self.assertIsNotNone(persisted)

    def test_action_runtime_contains_no_publisher_restart_path(self) -> None:
        source = Path(action_runtime.__file__).read_text(encoding="utf-8")
        for token in (
            "reload_position_publisher",
            "position_publisher_reload",
            "/etc/init.d/v5-wcs-status-publisher",
            "subprocess.run",
        ):
            self.assertNotIn(token, source)


if __name__ == "__main__":
    unittest.main()
