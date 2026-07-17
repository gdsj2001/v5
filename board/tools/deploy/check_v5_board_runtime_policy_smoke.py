#!/usr/bin/env python3
from pathlib import Path


def main() -> int:
    source = Path(__file__).with_name(
        "check_v5_board_runtime_policy.py").read_text(encoding="utf-8")
    required = (
        "text.split()[0]",
        "position_identity_audit(pid, pidfile)",
        "proc_fields[21] != start_ticks",
        'matches != 1',
        '["flock", "-n", lock_path, "true"]',
        '(0x56504F53, 2, 152, writer_identity)',
        'actual_crc != expected_crc',
    )
    for token in required:
        assert token in source, "position identity gate missing: %s" % token
    assert "pid = int(text)" not in source
    print("V5_BOARD_RUNTIME_POSITION_IDENTITY_POLICY_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
