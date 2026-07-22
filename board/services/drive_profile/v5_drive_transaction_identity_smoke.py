#!/usr/bin/env python3
from __future__ import annotations

import copy
import tempfile
from pathlib import Path

import v5_drive_bus_contract as contract
import v5_drive_transaction_identity as identity


def write_owner(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main() -> int:
    original_probe = identity.probe_axis_slave_mapping
    original_parse = identity.parse_slave_identity
    with tempfile.TemporaryDirectory(prefix="v5_drive_identity_smoke_") as temp:
        root = Path(temp)
        contract.RUNTIME_SETTINGS_INI = root / "linuxcnc/ini/v5_bus.ini"
        contract.SELF_PARAMETER_TABLE = root / "config/settings/self_parameter_table.tsv"
        contract.DRIVE_PARAMETER_TABLE = root / "config/settings/drive_parameter_table.tsv"
        contract.SETTINGS_RUNTIME_JSON = root / "settings_runtime.json"
        contract.RESIDENT_SNAPSHOT = root / "run/drive_profile_resident_snapshot.json"
        write_owner(contract.RUNTIME_SETTINGS_INI, "[RTCP]\nMODEL=XYZAC_TRT\n")
        write_owner(contract.SELF_PARAMETER_TABLE, "".join(
            "%s\tslave\t%d\n" % (axis, slot)
            for slot, axis in enumerate("XYZAC")))
        write_owner(contract.DRIVE_PARAMETER_TABLE, "".join(
            "%s\tencoder_bits\t18\n" % axis for axis in "XYZAC"))
        write_owner(contract.SETTINGS_RUNTIME_JSON, '{"schema":"re.v3.settings_runtime.drive_only.v1","axes":[]}\n')
        write_owner(contract.RESIDENT_SNAPSHOT, '{"generated_at":"g1","profiles":[]}\n')

        targets = []
        planned = {}
        for slot, axis_name in enumerate("XYZAC"):
            physical = {
                "identity_ok": True,
                "vendor_id": "0x000001dd",
                "product_code": "0x10305070",
                "revision": "0x00010000",
            }
            targets.append({
                "axis": axis_name,
                "status_slot": slot,
                "position": str(slot),
                "identity": physical,
                "profile": {
                    "profile_id": "sv630n",
                    "profile_map_sha256": "a" * 64,
                },
            })
            planned[str(slot)] = ((4096, 1125), {
                "scale": 10000.0,
                "target_precision": 0.0001,
                "pitch_units_per_load_rev": 360.0 if axis_name in "AC" else 5.0,
                "motor_rev": 50.0 if axis_name in "AC" else 1.0,
                "load_rev": 1.0,
                "motor_revs_per_load_rev": 50.0 if axis_name in "AC" else 1.0,
                "target_command_counts_per_motor_rev": 72000 if axis_name in "AC" else 50000,
                "encoder_bits": 18,
                "encoder_bits_source": "drive_parameter_table.encoder_bits",
            })
        mapping_generation = identity.persistent_mapping_generation(targets)
        mapping_probe = {
            "ok": True,
            "available": True,
            "applicable": True,
            "valid": True,
            "generation": mapping_generation,
            "code": "BUS_HOME_MAPPING_VALID",
        }
        identity.probe_axis_slave_mapping = lambda _timeout: dict(mapping_probe)
        physical_by_position = {
            str(target["position"]): dict(target["identity"])
            for target in targets
        }
        identity.parse_slave_identity = lambda position, _timeout: dict(physical_by_position[str(position)])
        scan = {
            "active_model": "XYZAC_TRT",
            "active_model_axes": list("XYZAC"),
            "active_model_status_slots": [
                {"axis": axis_name, "status_slot": slot}
                for slot, axis_name in enumerate("XYZAC")],
        }
        try:
            frozen = identity.capture_drive_transaction_identity(
                targets, planned, scan, 1.0)
            current = identity.capture_drive_transaction_identity(
                targets, planned, scan, 1.0)
            verified = identity.verify_drive_transaction_identity(
                frozen, current, "before_first_write_sdo")
            if (not verified.get("ok") or
                    frozen.get("persistent_mapping_generation") != mapping_generation or
                    not frozen.get("native_mapping_matches_persistent")):
                print("nominal transaction identity did not close", frozen, verified)
                return 1

            drift_targets = copy.deepcopy(targets)
            drift_targets[0]["position"], drift_targets[1]["position"] = (
                drift_targets[1]["position"], drift_targets[0]["position"])
            drift_current = identity.capture_drive_transaction_identity(
                drift_targets, planned, scan, 1.0)
            try:
                identity.verify_drive_transaction_identity(
                    frozen, drift_current, "after_batch_fresh_readback")
            except contract.DriveActionError as exc:
                if exc.code != "DRIVE_TRANSACTION_IDENTITY_CHANGED":
                    print("owner drift returned wrong code", exc.code)
                    return 2
            else:
                print("owner drift was accepted")
                return 3

            frozen = identity.capture_drive_transaction_identity(
                targets, planned, scan, 1.0)
            mapping_probe["generation"] = mapping_generation ^ 0x1
            current = identity.capture_drive_transaction_identity(
                targets, planned, scan, 1.0)
            try:
                identity.verify_drive_transaction_identity(
                    frozen, current, "after_batch_fresh_readback")
            except contract.DriveActionError as exc:
                if exc.code != "DRIVE_TRANSACTION_IDENTITY_CHANGED":
                    print("native mapping drift returned wrong code", exc.code)
                    return 4
            else:
                print("native mapping drift was accepted")
                return 5

            mapping_probe["generation"] = mapping_generation
        finally:
            identity.probe_axis_slave_mapping = original_probe
            identity.parse_slave_identity = original_parse
    print("drive transaction identity smoke ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
