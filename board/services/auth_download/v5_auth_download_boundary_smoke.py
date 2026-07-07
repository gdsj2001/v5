#!/usr/bin/env python3
"""Check v5 auth/download boundaries without contacting VPS."""

from __future__ import annotations

from pathlib import Path

import device_auth_latch
import v5_device_authorization_download
import drive_profile_download_flow

FORBIDDEN_PRODUCT_MARKERS = (
    "AX" + "8_",
    "RE_V5_DRIVE_PROFILE_" + "PRIVATE\"",
    "legacy_" + "effective",
    "linuxcnc" + ".command",
    "motion" + "_stillness_gate",
    "program" + "_open_cc",
    "start" + "_run",
    "pause" + "_resume_program",
    "re_" + "v" + "3_json",
)

AUTH_DOWNLOAD_FILES = tuple(sorted(Path("services/auth_download").glob("*.py")))


def main() -> int:
    latch = device_auth_latch.build_device_auth_latch({"ok": False})
    if latch.get("ok") is not False or latch.get("schema") != device_auth_latch.LATCH_SCHEMA:
        return 1

    result_path = str(v5_device_authorization_download.DEFAULT_RESULT_PATH)
    expected_auth_result = "device_authorization" + "_download_result" + ".json"
    if expected_auth_result not in result_path:
        return 2
    expected_progress = "drive_profile_server" + "_download_progress" + ".json"
    if drive_profile_download_flow.server_download_progress_path().name != expected_progress:
        return 3

    for path in AUTH_DOWNLOAD_FILES:
        text = path.read_text(encoding="utf-8", errors="replace")
        for marker in FORBIDDEN_PRODUCT_MARKERS:
            if marker in text:
                print(f"forbidden marker {marker} in {path}")
                return 4

    print("v5 auth/download boundary smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
