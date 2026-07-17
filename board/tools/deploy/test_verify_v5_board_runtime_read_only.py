#!/usr/bin/env python3
"""Contract: the board verifier must not become a canonical state writer."""

from pathlib import Path


SCRIPT = Path(__file__).with_name("verify_v5_board_runtime.sh")


def main() -> int:
    text = SCRIPT.read_text(encoding="utf-8")
    forbidden = (
        "v5_wcs_status_publisher.py --once --path /dev/shm/v5_native_wcs_status.bin",
        "--operator-error-path /dev/shm/v5_native_operator_error_status.bin",
        "v5_state_publisher --path /dev/shm/v3_status_shm",
    )
    for token in forbidden:
        assert token not in text, f"verifier canonical writer survived: {token}"
    required = (
        "test -s /dev/shm/v5_native_wcs_status.bin",
        "/usr/bin/python3 -c '$wcs_block_check'",
        "--operator-error-path /dev/shm/v5_verify_native_operator_error.bin",
        "v5_state_publisher --path /dev/shm/v5_verify_state_publisher --once",
        "rm -f /dev/shm/v5_verify_native_operator_error.bin",
        "rm -f /dev/shm/v5_verify_state_publisher",
    )
    for token in required:
        assert token in text, f"verifier read-only or temporary-path contract missing: {token}"
    print("V5_BOARD_VERIFIER_CANONICAL_READ_ONLY_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
