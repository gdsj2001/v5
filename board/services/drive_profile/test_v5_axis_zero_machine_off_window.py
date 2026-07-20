from __future__ import annotations

from unittest import mock

import v5_drive_bus_action as action


def _success_result() -> dict:
    return {
        "ok": True,
        "code": "SETTINGS_COUNT_DOMAIN_ZERO_SAVED_RESTART_REQUIRED",
        "message_cn": "Y轴零位与 raw 软限位已保存。",
        "display_message_cn": "Y轴零位与 raw 软限位已保存。",
        "settings_mcs_position": 0.0,
        "settings_mcs_position_valid": True,
        "write_executed": True,
        "drive_write_executed": False,
        "motion_executed": False,
    }


def test_axis_zero_saves_without_machine_off_window() -> None:
    with mock.patch.object(action.v5_drive_enable_window, "begin") as begin, \
         mock.patch.object(action.v5_drive_enable_window, "finish_safely") as finish, \
         mock.patch.object(action, "axis_zero_verify", return_value=_success_result()) as verify:
        result = action.run_axis_zero(1.0, {"axis": "Y", "_run_id": "zero-save"})
    assert result["ok"] is True
    assert result["code"] == "SETTINGS_COUNT_DOMAIN_ZERO_SAVED_RESTART_REQUIRED"
    assert verify.call_args.args[0]["_run_id"] == "zero-save"
    begin.assert_not_called()
    finish.assert_not_called()


def test_axis_zero_generates_run_id_without_opening_window() -> None:
    with mock.patch.object(action.v5_drive_enable_window, "begin") as begin, \
         mock.patch.object(action.v5_drive_enable_window, "finish_safely") as finish, \
         mock.patch.object(action, "axis_zero_verify", return_value=_success_result()) as verify:
        result = action.run_axis_zero(1.0, {"axis": "Y"})
    assert result["ok"] is True
    assert verify.call_args.args[0]["_run_id"].startswith("direct-")
    begin.assert_not_called()
    finish.assert_not_called()


def test_axis_zero_preserves_structured_save_failure() -> None:
    failure = action.DriveActionError(
        "SETTINGS_AXIS_ZERO_RAW_LIMIT_READBACK_MISMATCH",
        "Y轴 raw 软限位硬盘回读不一致。",
        {"section": "AXIS_Y"},
    )
    with mock.patch.object(action.v5_drive_enable_window, "begin") as begin, \
         mock.patch.object(action.v5_drive_enable_window, "finish_safely") as finish, \
         mock.patch.object(action, "axis_zero_verify", side_effect=failure):
        result = action.run_axis_zero(1.0, {"axis": "Y", "_run_id": "zero-fail"})
    assert result["ok"] is False
    assert result["code"] == "SETTINGS_AXIS_ZERO_RAW_LIMIT_READBACK_MISMATCH"
    assert result["failed_stage"] == "axis_zero"
    assert result["write_executed"] is False
    begin.assert_not_called()
    finish.assert_not_called()
