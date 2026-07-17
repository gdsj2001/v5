#!/usr/bin/env python3
from __future__ import annotations

import tempfile
from pathlib import Path
from unittest import mock

import v5_drive_bus_contract as contract
import v5_drive_bus_context as context
from v5_drive_bus_contract import DriveActionError
from v5_drive_runtime_store import update_runtime_ini_raw_limits


def fixture(path: Path, include_crev: bool = True) -> None:
    crev = "WCHECKPOINT_COUNTS_PER_REV = 360000\n" if include_crev else ""
    path.write_text(
        "[TRAJ]\nCOORDINATES = X Y Z A C\n"
        "[AXIS_C]\nMIN_LIMIT = -200\nMAX_LIMIT = 200\n" + crev +
        "[JOINT_4]\nMIN_LIMIT = -200\nMAX_LIMIT = 200\nSCALE = 10000\n",
        encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        ini = Path(temporary) / "v5_bus.ini"
        fixture(ini)
        with mock.patch.object(contract, "RUNTIME_SETTINGS_INI", ini), \
                mock.patch.object(context, "resident_preload_active", True):
            context.runtime_ini_sections_cache.clear()
            result = update_runtime_ini_raw_limits(
                "C", 5, -100.0, -90.0, 10000.0)
        text = ini.read_text(encoding="utf-8")
        assert text.count("WCHECKPOINT_COUNTS_PER_REV = 3600000") == 1
        assert text.count("MIN_LIMIT = -190") == 2
        assert text.count("MAX_LIMIT = 210") == 2
        assert result["wcheckpoint_profile_updated"] is True
        assert result["wcheckpoint_counts_per_rev"] == 3600000

        fixture(ini, include_crev=False)
        with mock.patch.object(contract, "RUNTIME_SETTINGS_INI", ini), \
                mock.patch.object(context, "resident_preload_active", True):
            context.runtime_ini_sections_cache.clear()
            try:
                update_runtime_ini_raw_limits(
                    "C", 5, -100.0, -90.0, 10000.0)
            except DriveActionError as exc:
                assert exc.code == "SETTINGS_AXIS_ZERO_WCHECKPOINT_CREV_WRITE_MISSING"
            else:
                raise AssertionError("missing rotary Crev owner was accepted")
    print("V5_AXIS_ZERO_WCHECKPOINT_PROFILE_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
