from __future__ import annotations

import ctypes
from unittest import mock

import v5_command_gate_zero_client as client
import v5_drive_axis_model as axis_model


class FakeSocket:
    def __init__(self, response: bytes) -> None:
        self.response = response
        self.sent = b""

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def settimeout(self, _timeout):
        pass

    def connect(self, _path):
        pass

    def sendall(self, payload: bytes):
        self.sent = payload

    def recv(self, size: int) -> bytes:
        chunk, self.response = self.response[:size], self.response[size:]
        return chunk


def test_axis_zero_live_client_decodes_verified_commit() -> None:
    response = client.Response()
    response.magic = client.MAGIC
    response.version = client.VERSION
    response.size = ctypes.sizeof(client.Response)
    response.send_status = 1
    response.executed = 1
    response.machine_enable_known = 1
    response.machine_enabled = 0
    response.settings_restart_pending = 1
    response.readback_code = b"SETTINGS_COUNT_DOMAIN_ZERO_LIVE_APPLIED_RESTART_REQUIRED"
    response.zero_commit_seq = 42
    response.zero_display_verified = 1
    response.zero_mcs_position = 0.00005
    response.zero_tolerance_units = 0.0001
    response.zero_previous_mcs_position = 12.5
    fake = FakeSocket(bytes(response))
    with mock.patch.object(client.socket, "AF_UNIX", 1, create=True), \
         mock.patch.object(client.socket, "socket", return_value=fake):
        result = client.apply_axis_zero_live("a", 3, "run-1")
    request = client.Request.from_buffer_copy(fake.sent)
    assert request.op == client.OP_SETTINGS_AXIS_ZERO_LIVE_APPLY
    assert request.index_value == 3
    assert request.axis_value == 0.0
    assert bytes(request.settings_axis).split(b"\0", 1)[0] == b"A"
    assert result["ok"] is True
    assert result["commit_seq"] == 42
    assert result["restart_pending"] is True
    assert result["zero_display_verified"] is True
    assert result["previous_mcs_position"] == 12.5


def test_axis_zero_live_client_rejects_unverified_response() -> None:
    response = client.Response()
    response.magic = client.MAGIC
    response.version = client.VERSION
    response.size = ctypes.sizeof(client.Response)
    response.send_status = -1
    response.readback_code = b"SETTINGS_AXIS_ZERO_FRESH_MCS_NOT_ZERO"
    fake = FakeSocket(bytes(response))
    with mock.patch.object(client.socket, "AF_UNIX", 1, create=True), \
         mock.patch.object(client.socket, "socket", return_value=fake):
        result = client.apply_axis_zero_live("X", 0, "run-2")
    assert result["ok"] is False
    assert result["zero_display_verified"] is False


def _axis_zero_runtime():
    return {"axes": [{"axis": "X", "zero_model": {
        "zero_counts": 10.0, "zero_anchor_counts": 10.0,
        "source": "old", "slave_position": 0}}]}


def _raw_limit_save() -> dict:
    return {
        "new_zero_physical": 0.1,
        "ui_min_limit_distance": -1.1,
        "ui_max_limit_distance": 0.9,
        "min_limit_disabled": False,
        "max_limit_disabled": False,
        "raw_min_limit": -1.0,
        "raw_max_limit": 1.0,
        "updated_sections": ["AXIS_X", "JOINT_0"],
    }


def _raw_limit_sections(max_value: float = 1.0) -> dict:
    return {
        "AXIS_X": {"MIN_LIMIT": "-1", "MAX_LIMIT": str(max_value)},
        "JOINT_0": {"MIN_LIMIT": "-1", "MAX_LIMIT": str(max_value)},
    }


def test_axis_zero_verify_saves_zero_and_raw_limits_without_live_apply() -> None:
    runtime = _axis_zero_runtime()

    def persist(owner, *_args, **_kwargs):
        owner["axes"][0]["zero_model"].update(
            {"zero_counts": 1000.0, "zero_anchor_counts": 1000.0,
             "source": "settings_axis_zero"})
        return {"raw_limit_save": _raw_limit_save()}

    patches = [
        mock.patch.object(axis_model, "request_slave_position", return_value=0),
        mock.patch.object(axis_model, "load_settings_runtime", return_value=runtime),
        mock.patch.object(axis_model, "derive_counts_per_unit", return_value=(10000.0, {"source": "test"})),
        mock.patch.object(axis_model, "current_axis_counts", side_effect=[(1000.0, {"position": "0"}), (1050.0, {"position": "0"})]),
        mock.patch.object(axis_model, "persist_axis_zero_model", side_effect=persist),
        mock.patch.object(axis_model, "snapshot_axis_zero_persistence", return_value={"settings_runtime_json": "{}", "runtime_ini": ""}),
        mock.patch.object(axis_model, "read_runtime_ini_sections", return_value=_raw_limit_sections()),
    ]
    with patches[0], patches[1], patches[2], patches[3], patches[4], patches[5], patches[6]:
        result = axis_model.axis_zero_verify({
            "axis": "X", "driver_mode": "bus",
            "target_scope": "bus_count_domain_zero", "apply_mode": "count_domain_zero",
            "_run_id": "run-success"}, 1.0)
    assert result["ok"] is True
    assert result["code"] == "SETTINGS_COUNT_DOMAIN_ZERO_SAVED_RESTART_REQUIRED"
    assert result["settings_mcs_position"] == 0.0
    assert result["settings_mcs_position_valid"] is True
    assert result["settings_zero_display_verified"] is True
    assert result["zero_display_verified"] is False
    assert result["commit_seq"] == 0
    assert result["restart_deferred"] is True
    assert result["raw_limit_live_verified"] is False
    assert result["raw_limit_readback"]["raw_min"] == -1.0
    assert result["raw_limit_readback"]["raw_max"] == 1.0
    assert abs(result["delta_physical"] - 0.005) < 1.0e-12


def test_axis_zero_verify_rolls_back_persistence_after_raw_limit_readback_failure() -> None:
    runtime = _axis_zero_runtime()
    original = _axis_zero_runtime()

    def persist(owner, *_args, **_kwargs):
        owner["axes"][0]["zero_model"].update(
            {"zero_counts": 1000.0, "zero_anchor_counts": 1000.0})
        return {"raw_limit_save": _raw_limit_save()}

    def restore(_snapshot):
        return {"ok": True}

    with mock.patch.object(axis_model, "request_slave_position", return_value=0), \
         mock.patch.object(axis_model, "load_settings_runtime", side_effect=[runtime, original]), \
         mock.patch.object(axis_model, "derive_counts_per_unit", return_value=(10000.0, {})), \
         mock.patch.object(axis_model, "current_axis_counts", side_effect=[(1000.0, {"position": "0"}), (1000.0, {"position": "0"})]), \
         mock.patch.object(axis_model, "persist_axis_zero_model", side_effect=persist), \
         mock.patch.object(axis_model, "snapshot_axis_zero_persistence", return_value={"settings_runtime_json": "{}", "runtime_ini": ""}), \
         mock.patch.object(axis_model, "restore_axis_zero_persistence", side_effect=restore) as restore_mock, \
         mock.patch.object(axis_model, "read_runtime_ini_sections", return_value=_raw_limit_sections(2.0)):
        try:
            axis_model.axis_zero_verify({
                "axis": "X", "driver_mode": "bus",
                "target_scope": "bus_count_domain_zero", "apply_mode": "count_domain_zero",
                "_run_id": "run-fail"}, 1.0)
        except axis_model.DriveActionError as exc:
            assert exc.code == "SETTINGS_AXIS_ZERO_RAW_LIMIT_READBACK_MISMATCH"
        else:
            raise AssertionError("axis-zero raw-limit mismatch was accepted")
    assert restore_mock.call_count == 1
    assert runtime == original


def test_rotary_axis_zero_accepts_explicit_disabled_raw_limits() -> None:
    runtime = {"axes": [{"axis": "B", "zero_model": {
        "zero_counts": 10.0, "zero_anchor_counts": 10.0,
        "source": "old", "slave_position": 4}}]}

    def persist(owner, *_args, **_kwargs):
        owner["axes"][0]["zero_model"].update(
            {"zero_counts": -248668.0, "zero_anchor_counts": -248668.0})
        return {"raw_limit_save": {
            "new_zero_physical": -24.8668,
            "ui_min_limit_distance": 0.0,
            "ui_max_limit_distance": 0.0,
            "min_limit_disabled": True,
            "max_limit_disabled": True,
            "raw_min_limit": 0.0,
            "raw_max_limit": 0.0,
            "updated_sections": ["AXIS_B", "JOINT_3"],
        }}

    with mock.patch.object(axis_model, "request_slave_position", return_value=4), \
         mock.patch.object(axis_model, "load_settings_runtime", return_value=runtime), \
         mock.patch.object(axis_model, "derive_counts_per_unit", return_value=(10000.0, {})), \
         mock.patch.object(axis_model, "current_axis_counts", side_effect=[
             (-248668.0, {"position": "4"}), (-248668.0, {"position": "4"})]), \
         mock.patch.object(axis_model, "persist_axis_zero_model", side_effect=persist), \
         mock.patch.object(axis_model, "snapshot_axis_zero_persistence", return_value={
             "settings_runtime_json": "{}", "runtime_ini": ""}), \
         mock.patch.object(axis_model, "read_runtime_ini_sections", return_value={
             "AXIS_B": {"MIN_LIMIT": "0", "MAX_LIMIT": "0"},
             "JOINT_3": {"MIN_LIMIT": "0", "MAX_LIMIT": "0"},
         }):
        result = axis_model.axis_zero_verify({
            "axis": "B", "driver_mode": "bus",
            "target_scope": "bus_count_domain_zero", "apply_mode": "count_domain_zero",
            "_run_id": "run-rotary-disabled-limits"}, 1.0)
    assert result["ok"] is True
    assert result["raw_limit_readback"]["min_limit_disabled"] is True
    assert result["raw_limit_readback"]["max_limit_disabled"] is True
    assert result["raw_limit_readback"]["raw_min"] == 0.0
    assert result["raw_limit_readback"]["raw_max"] == 0.0


def test_axis_zero_verify_rolls_back_when_raw_limit_formula_is_wrong() -> None:
    runtime = _axis_zero_runtime()
    original = _axis_zero_runtime()

    def persist(owner, *_args, **_kwargs):
        owner["axes"][0]["zero_model"].update(
            {"zero_counts": 1000.0, "zero_anchor_counts": 1000.0})
        bad = _raw_limit_save()
        bad["raw_min_limit"] = -0.5
        return {"raw_limit_save": bad}

    with mock.patch.object(axis_model, "request_slave_position", return_value=0), \
         mock.patch.object(axis_model, "load_settings_runtime", side_effect=[runtime, original]), \
         mock.patch.object(axis_model, "derive_counts_per_unit", return_value=(10000.0, {})), \
         mock.patch.object(axis_model, "current_axis_counts", side_effect=[(1000.0, {"position": "0"}), (1000.0, {"position": "0"})]), \
         mock.patch.object(axis_model, "persist_axis_zero_model", side_effect=persist), \
         mock.patch.object(axis_model, "snapshot_axis_zero_persistence", return_value={"settings_runtime_json": "{}", "runtime_ini": ""}), \
         mock.patch.object(axis_model, "restore_axis_zero_persistence", return_value={"ok": True}) as restore_mock:
        try:
            axis_model.axis_zero_verify({
                "axis": "X", "driver_mode": "bus",
                "target_scope": "bus_count_domain_zero", "apply_mode": "count_domain_zero",
                "_run_id": "run-rejected"}, 1.0)
        except axis_model.DriveActionError as exc:
            assert exc.code == "SETTINGS_AXIS_ZERO_RAW_LIMIT_FORMULA_MISMATCH"
            assert exc.detail["rollback_error"] == ""
        else:
            raise AssertionError("axis-zero bad raw-limit formula was accepted")
    assert restore_mock.call_count == 1
    assert runtime == original
