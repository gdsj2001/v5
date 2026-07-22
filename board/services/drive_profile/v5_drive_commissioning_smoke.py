#!/usr/bin/env python3
from __future__ import print_function

import json
from pathlib import Path

import v5_drive_bus_action as action
import v5_drive_bus_contract as contract
from v5_drive_bus_contract import DriveActionError
from v5_drive_parameter_table import drive_commissioning_plan


ROOT = Path(__file__).resolve().parents[2]
PROFILE_MAP = ROOT / "config/drive-profiles/public/driver_profile_map.json"


def profile():
    with PROFILE_MAP.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    return [item for item in payload["profiles"] if item.get("profile_id") == "inovance_sv630n_18bit"][0]


def target():
    return {"axis": "X", "position": "0", "profile": profile()}


def fake_result(value, ok=True):
    return {"ok": ok, "value": value, "stdout": str(value), "stderr": "", "returncode": 0 if ok else 1}


def test_apply_and_readback():
    plan = drive_commissioning_plan(target())
    registers = {(item["object"]["index"], item["object"]["subindex"]): item["expected_raw"] for item in plan}
    gain = [item for item in plan if item["field"] == "csp_velocity_feedforward_gain_0p1_percent"][0]
    registers[(gain["object"]["index"], gain["object"]["subindex"])] = 0
    writes = []

    def upload(_position, obj, _data_type, _timeout_s):
        return fake_result(registers[(obj["index"], obj["subindex"])])

    def download(_position, obj, _data_type, value, _timeout_s):
        registers[(obj["index"], obj["subindex"])] = int(value)
        writes.append((obj["index"], obj["subindex"], int(value)))
        return fake_result(value)

    old_upload, old_download = action.ethercat_upload, action.ethercat_download
    action.ethercat_upload, action.ethercat_download = upload, download
    try:
        result = action.apply_target_commissioning(target(), 1.0)
    finally:
        action.ethercat_upload, action.ethercat_download = old_upload, old_download
    assert result["ok"] and result["write_executed"], result
    assert writes == [("0x2008", "0x14", 1000)], writes


def test_failure_rolls_back_prior_write():
    plan = drive_commissioning_plan(target())
    registers = {(item["object"]["index"], item["object"]["subindex"]): item["expected_raw"] for item in plan}
    filter_item = [item for item in plan if item["field"] == "csp_velocity_feedforward_filter_0p01_ms"][0]
    gain_item = [item for item in plan if item["field"] == "csp_velocity_feedforward_gain_0p1_percent"][0]
    filter_key = (filter_item["object"]["index"], filter_item["object"]["subindex"])
    gain_key = (gain_item["object"]["index"], gain_item["object"]["subindex"])
    registers[filter_key] = 0
    registers[gain_key] = 0
    failed_once = [False]

    def upload(_position, obj, _data_type, _timeout_s):
        return fake_result(registers[(obj["index"], obj["subindex"])])

    def download(_position, obj, _data_type, value, _timeout_s):
        key = (obj["index"], obj["subindex"])
        if key == gain_key and int(value) == 1000 and not failed_once[0]:
            failed_once[0] = True
            return fake_result(registers[key], ok=False)
        registers[key] = int(value)
        return fake_result(value)

    old_upload, old_download = action.ethercat_upload, action.ethercat_download
    action.ethercat_upload, action.ethercat_download = upload, download
    try:
        try:
            action.apply_target_commissioning(target(), 1.0)
            raise AssertionError("expected commissioning failure")
        except DriveActionError as exc:
            assert exc.code == "DRIVE_COMMISSIONING_WRITE_FAILED", exc.code
            assert exc.detail["rollback_ok"], exc.detail
    finally:
        action.ethercat_upload, action.ethercat_download = old_upload, old_download
    assert registers[filter_key] == 0, registers


test_apply_and_readback()
test_failure_rolls_back_prior_write()
print("v5 drive commissioning smoke ok")
