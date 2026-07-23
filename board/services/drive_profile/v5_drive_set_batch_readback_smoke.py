#!/usr/bin/env python3
from __future__ import annotations

import unittest
from typing import Any, Dict, List, Tuple
from unittest import mock

import v5_drive_bus_action as bus_facade
import v5_drive_bus_apply_action as bus
import v5_drive_health as health

assert bus_facade.run_set_drive is bus.run_set_drive
assert bus_facade.run_boot_drive_apply is bus.run_boot_drive_apply


TARGETS = [{"position": "0"}, {"position": "1"}]
EXPECTATIONS = {
    "0": ((16384, 3125), 8),
    "1": ((16384, 3125), 8),
}


def healthy_readback(position: str) -> Dict[str, Any]:
    egear, mode = EXPECTATIONS[position]
    health_result = {
        "ok": True,
        "failures": [],
        "statusword": 5840,
        "error_code": 0,
        "mode_of_operation": mode,
        "egear_numerator": egear[0],
        "egear_denominator": egear[1],
    }
    reads = {
        command: {"ok": True, "required": True, "code": "OK"}
        for command in (
            "drive.read_statusword",
            "drive.read_error_code",
            "drive.read_mode",
            "drive.read_actual_position",
            "drive.read_egear",
        )
    }
    return {
        "ok": True,
        "attempts": [{
            "attempt": 1,
            "read_ok": True,
            "health": health_result,
            "reads": reads,
        }],
        "health": health_result,
    }


def mismatch_readback(position: str) -> Dict[str, Any]:
    result = healthy_readback(position)
    expected = EXPECTATIONS[position][1]
    failure = {
        "field": "mode",
        "code": "DRIVE_MODE_READBACK_MISMATCH",
        "expected": expected,
        "actual": 7,
    }
    result["ok"] = False
    result["health"] = dict(
        result["health"],
        ok=False,
        failures=[failure],
        mode_of_operation=7,
    )
    result["attempts"][0]["health"] = result["health"]
    return result


def missing_required_readback(position: str) -> Dict[str, Any]:
    result = mismatch_readback(position)
    result["attempts"][0]["read_ok"] = False
    result["attempts"][0]["reads"]["drive.read_statusword"] = {
        "ok": False,
        "required": True,
        "code": "DRIVE_SDO_UPLOAD_FAILED",
    }
    return result


def run_batch(
        scans: List[Dict[str, str]],
        readback_sequences: Dict[str, List[Dict[str, Any]]],
        ) -> Tuple[Dict[str, Any], List[Tuple[str, ...]]]:
    events: List[Tuple[str, ...]] = []
    scan_index = 0
    read_indexes = {position: 0 for position in readback_sequences}

    def fake_scan(_timeout: float) -> Dict[str, Any]:
        nonlocal scan_index
        states = scans[min(scan_index, len(scans) - 1)]
        scan_index += 1
        events.append(("scan",))
        return {
            "ok": True,
            "code": "DRIVE_SCAN_OK",
            "slaves": [
                {"position": position, "state": state}
                for position, state in states.items()
            ],
        }

    def reject_state_switch(argv: List[str], _timeout: float) -> Dict[str, Any]:
        raise AssertionError(
            f"batch readback must not switch EtherCAT AL state: {argv!r}")

    def fake_readback(
            target: Dict[str, Any],
            _timeout: float,
            expected_egear: tuple[int, int] | None = None,
            expected_mode: int | None = None,
            **_kwargs: Any,
            ) -> Dict[str, Any]:
        position = str(target["position"])
        events.append(("read", position))
        assert (expected_egear, expected_mode) == EXPECTATIONS[position]
        sequence = readback_sequences[position]
        index = min(read_indexes[position], len(sequence) - 1)
        read_indexes[position] += 1
        return sequence[index]

    with mock.patch.object(
        health, "run_ethercat_slaves", side_effect=fake_scan,
    ), mock.patch.object(
        health, "run_command", side_effect=reject_state_switch,
    ), mock.patch.object(
        health, "readback_once", side_effect=fake_readback,
    ), mock.patch.object(
        health.time, "sleep",
    ):
        result = health.set_drive_batch_readback(
            TARGETS, 1.0, EXPECTATIONS)
    return result, events


class BatchReadbackSmoke(unittest.TestCase):
    def test_nominal_and_transient_readback_never_switch_al_state(self) -> None:
        result, events = run_batch(
            [{"0": "OP", "1": "OP"}],
            {
                "0": [healthy_readback("0")],
                "1": [mismatch_readback("1"), healthy_readback("1")],
            },
        )
        self.assertTrue(result["ok"])
        self.assertFalse(any(event[0] == "state" for event in events))

    def test_persistent_failure_is_preserved_without_state_switch(self) -> None:
        result, events = run_batch(
            [{"0": "OP", "1": "OP"}] * 4,
            {
                "0": [healthy_readback("0")],
                "1": [missing_required_readback("1")] * 4,
            },
        )
        self.assertFalse(result["ok"])
        self.assertEqual(["1"], result["failed_positions"])
        self.assertFalse(any(event[0] == "state" for event in events))


def drive_target() -> Dict[str, Any]:
    return {
        "axis": "C",
        "position": "4",
        "status_slot": 4,
        "axis_cfg": {"axis": "C"},
        "profile": {"profile_id": "sv630n"},
        "commands": {
            "drive.set_egear": {
                "supported": True,
                "access": "write",
                "requires_save_parameters": False,
            },
            "drive.write_mode": {
                "supported": True,
                "access": "write",
                "requires_save_parameters": False,
            },
        },
    }


class SetDrivePreflightSmoke(unittest.TestCase):
    def test_settings_button_validates_only_and_requires_restart(self) -> None:
        target = drive_target()
        identity = {"transaction_generation": "a" * 64}
        with mock.patch.object(
            bus,
            "configured_drive_targets",
            return_value=([target], {"axes": []}, {"active_model": "XYZAC_TRT"}),
        ), mock.patch.object(
            bus,
            "_planned_drive_transaction",
            return_value={"4": ((4096, 1125), {"source": "ini"})},
        ), mock.patch.object(
            bus, "capture_drive_transaction_identity", return_value=identity,
        ), mock.patch.object(
            bus, "_reload_drive_transaction_identity", return_value=identity,
        ), mock.patch.object(
            bus,
            "verify_drive_transaction_identity",
            return_value={"ok": True, "stage": "settings_drive_preflight"},
        ), mock.patch.object(
            bus, "invalidate_stale_drive_transaction_evidence", return_value=[],
        ), mock.patch.object(
            bus,
            "invalidate_drive_generation",
            return_value={
                "ok": True,
                "code": "BACKEND_DRIVE_INVALIDATED_MACHINE_OFF",
                "generation": 123,
                "motion_ready": False,
                "drive_verified": False,
            },
        ) as invalidate_native, mock.patch.object(
            bus, "write_command",
        ) as write_command, mock.patch.object(
            bus.v5_drive_enable_window, "begin",
        ) as window_begin:
            result = bus.run_set_drive(
                5.0, {"trigger": "settings_set_drive"})

        self.assertTrue(result["ok"])
        self.assertEqual("DRIVE_SET_RESTART_REQUIRED", result["code"])
        self.assertTrue(result["restart_required"])
        self.assertTrue(result["restart_deferred"])
        self.assertFalse(result["write_executed"])
        self.assertFalse(result["drive_write_executed"])
        self.assertEqual(4096, result["targets"][0]["target_egear"]["numerator"])
        self.assertEqual(1125, result["targets"][0]["target_egear"]["denominator"])
        self.assertFalse(result["native_invalidation"]["motion_ready"])
        invalidate_native.assert_called_once_with(3.0)
        write_command.assert_not_called()
        window_begin.assert_not_called()


class BootDriveApplySmoke(unittest.TestCase):
    def run_apply(
            self,
            actual_egear: tuple[int, int],
            actual_mode: int,
            ) -> tuple[Dict[str, Any], mock.Mock]:
        target = drive_target()
        identity = {
            "transaction_generation": "b" * 64,
            "native_mapping_generation": 1234,
            "persistent_mapping_generation": 1234,
            "native_mapping_matches_persistent": True,
        }
        events: List[str] = []
        pre_read = {
            "ok": True,
            "reads": {
                "drive.read_egear": {
                    "numerator": {"value": actual_egear[0]},
                    "denominator": {"value": actual_egear[1]},
                },
                "drive.read_mode": {"upload": {"value": actual_mode}},
            },
        }
        batch = {
            "ok": True,
            "code": "DRIVE_SET_BATCH_READBACK_OK",
            "cycles": [{"attempt": 1}],
            "readbacks": {"4": healthy_readback("0")},
            "failed_positions": [],
        }
        batch["readbacks"]["4"]["health"].update({
            "egear_numerator": 4096,
            "egear_denominator": 1125,
            "mode_of_operation": 8,
        })

        def readback_batch(*_args: Any, **_kwargs: Any) -> Dict[str, Any]:
            events.append("batch_readback")
            return batch

        def readback_start_time() -> int:
            events.append("batch_start")
            return 987654321
        with mock.patch.object(
            bus,
            "configured_drive_targets",
            return_value=([target], {"axes": []}, {"active_model": "XYZAC_TRT"}),
        ), mock.patch.object(
            bus,
            "_planned_drive_transaction",
            return_value={"4": ((4096, 1125), {"source": "ini"})},
        ), mock.patch.object(
            bus, "capture_drive_transaction_identity", return_value=identity,
        ), mock.patch.object(
            bus, "_reload_drive_transaction_identity", return_value=identity,
        ), mock.patch.object(
            bus,
            "verify_drive_transaction_identity",
            side_effect=lambda frozen, current, stage: {
                "ok": frozen == current,
                "stage": stage,
            },
        ), mock.patch.object(
            bus, "invalidate_stale_drive_transaction_evidence", return_value=[],
        ), mock.patch.object(
            bus.v5_drive_enable_window,
            "begin",
            return_value={
                "ok": True,
                "run_id": "boot-smoke",
                "initial_machine_enabled": False,
                "final_machine_enabled": False,
            },
        ), mock.patch.object(
            bus.v5_drive_enable_window,
            "finish_safely",
            return_value={
                "ok": True,
                "run_id": "boot-smoke",
                "initial_machine_enabled": False,
                "final_machine_enabled": False,
            },
        ), mock.patch.object(
            bus, "precheck_targets_for_write", return_value=[],
        ), mock.patch.object(
            bus, "read_required_state", return_value=pre_read,
        ), mock.patch.object(
            bus, "write_command", return_value={"ok": True, "code": "OK"},
        ) as write_command, mock.patch.object(
            bus, "set_drive_batch_readback", side_effect=readback_batch,
        ), mock.patch.object(
            bus.time, "monotonic_ns", side_effect=readback_start_time,
        ), mock.patch.object(
            bus, "update_axis_drive_set_evidence",
        ), mock.patch.object(
            bus, "persist_settings_runtime", return_value={"ok": True},
        ), mock.patch.object(
            bus, "write_drive_parameter_display_rows", return_value={"ok": True},
        ), mock.patch.object(
            bus,
            "drive_display_update_from_health",
            return_value={"axis": "C", "write_status": "applied"},
        ):
            result = bus.run_boot_drive_apply(
                5.0,
                {"_run_id": "boot-smoke", "trigger": "post_restart_boot"},
            )
        self.assertEqual(["batch_start", "batch_readback"], events)
        self.assertEqual(
            987654321, result["batch_readback_started_monotonic_ns"])
        self.assertEqual(4, result["targets"][0]["status_slot"])
        return result, write_command

    def test_matching_actual_skips_sdo_writes_but_still_fresh_reads(self) -> None:
        result, write_command = self.run_apply((4096, 1125), 8)
        self.assertTrue(result["ok"])
        self.assertEqual("DRIVE_SET_OK", result["code"])
        self.assertFalse(result["drive_write_executed"])
        write_command.assert_not_called()

    def test_mismatch_writes_only_required_fields(self) -> None:
        result, write_command = self.run_apply((1, 1), 8)
        self.assertTrue(result["ok"])
        self.assertTrue(result["drive_write_executed"])
        self.assertEqual(1, write_command.call_count)
        self.assertEqual("drive.set_egear", write_command.call_args.args[1])


if __name__ == "__main__":
    unittest.main()
