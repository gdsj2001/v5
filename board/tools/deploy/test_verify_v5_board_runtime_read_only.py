#!/usr/bin/env python3
"""Contract: the board verifier must not become a canonical state writer."""

from pathlib import Path


SCRIPT = Path(__file__).with_name("verify_v5_board_runtime.sh")
STATE_START = 'if remote "/usr/libexec/8ax/v5_state_publisher --path /dev/shm/v5_verify_state_publisher --once'
STATE_END = '\nif remote "test -s \'$state_path\'"'
STATE_CLEANUP = "remote 'rm -f /dev/shm/v5_verify_state_publisher /tmp/v5_verify_state_publisher.out' >/dev/null 2>&1 || true"
TCF_LABEL = "production TCF files, services, processes, and port 1534 absent"


def audit_state_cleanup(text: str) -> None:
    begin = text.index(STATE_START); end = text.index(STATE_END, begin)
    block = text[begin:end]
    condition_end = block.index(' >/dev/null 2>&1; then')
    fi_end = block.rfind('\nfi') + len('\nfi')
    assert 'rm -f /dev/shm/v5_verify_state_publisher' not in block[:condition_end], \
        'STATE_TEMP_CLEANUP_IN_SUCCESS_CHAIN'
    assert block.count(STATE_CLEANUP) == 1 and block.index(STATE_CLEANUP) > fi_end, \
        'STATE_TEMP_CLEANUP_NOT_INDEPENDENT_POSTCONDITION'


def main() -> int:
    text = SCRIPT.read_text(encoding="utf-8")
    audit_state_cleanup(text)
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
        STATE_CLEANUP,
    )
    for token in required:
        assert token in text, f"verifier read-only or temporary-path contract missing: {token}"
    assert "tcf_absence_check=" in text and TCF_LABEL in text, "TCF_ABSENCE_GATE_MISSING"
    tcf_begin = text.index("tcf_absence_check=")
    tcf_end = text.index("\n\n", tcf_begin)
    tcf_block = text[tcf_begin:tcf_end]
    assert "rm -f" not in tcf_block and "unlink" not in tcf_block, "TCF_GATE_MUTATES_BOARD"
    begin = text.index(STATE_START); end = text.index(STATE_END, begin)
    block = text[begin:end]
    condition_end = block.index(' >/dev/null 2>&1; then')
    mutated_block = (block[:condition_end] +
                     ' && rm -f /dev/shm/v5_verify_state_publisher' +
                     block[condition_end:]).replace('\n' + STATE_CLEANUP, '', 1)
    mutated = text[:begin] + mutated_block + text[end:]
    try:
        audit_state_cleanup(mutated)
    except AssertionError as exc:
        assert 'STATE_TEMP_CLEANUP_' in str(exc)
    else:
        raise AssertionError('STATE_TEMP_CLEANUP_SUCCESS_CHAIN_MUTATION_SURVIVED')
    print("V5_BOARD_VERIFIER_CANONICAL_READ_ONLY_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
