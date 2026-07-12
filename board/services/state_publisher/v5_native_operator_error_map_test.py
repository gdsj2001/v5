#!/usr/bin/env python3

from pathlib import Path
import re

from v5_native_operator_error_map import (
    DISPLAY_LOG_ONLY,
    DISPLAY_POPUP,
    DISPLAY_TOP_STATUS,
    EXPECTED_SOURCE_COUNT,
    NativeOperatorErrorMap,
    _PRINTF_TOKEN,
    translate_internal_alias,
)


ROOT = Path(__file__).resolve().parents[2]
MAP_PATH = ROOT / "config/ui/v5_native_operator_error_map.tsv"
FORBIDDEN = re.compile(r"linuxcnc|linuxcncrsh|\bemc\b|\bhal\b|\bnml\b|motmod|rtapi|gdb|python", re.I)
FORBIDDEN_CN = re.compile(r"底层控制|内部进程|回溯路径")


def sample_for_pattern(pattern: str) -> str:
    output = []
    index = 0
    while index < len(pattern):
        if pattern[index] != "%":
            output.append(pattern[index])
            index += 1
            continue
        if index + 1 < len(pattern) and pattern[index + 1] == "%":
            output.append("%")
            index += 2
            continue
        token = _PRINTF_TOKEN.match(pattern, index)
        if not token:
            output.append("%")
            index += 1
            continue
        kind = token.group("kind")
        if kind == "c":
            output.append("X")
        elif kind in "diu":
            output.append("7")
        elif kind in "oxX":
            output.append("1A")
        elif kind in "fFeEgGaA":
            output.append("1.25")
        elif kind == "p":
            output.append("0x1A")
        else:
            output.append("VALUE")
        index = token.end()
    return "".join(output)


def assert_safe(message) -> None:
    assert message.title_cn
    assert message.reason_cn
    assert message.next_cn
    assert not FORBIDDEN.search(message.title_cn)
    assert not FORBIDDEN.search(message.reason_cn)
    assert not FORBIDDEN.search(message.next_cn)
    assert not FORBIDDEN_CN.search(message.title_cn)
    assert not FORBIDDEN_CN.search(message.reason_cn)
    assert not FORBIDDEN_CN.search(message.next_cn)
    assert message.display_mode in {DISPLAY_LOG_ONLY, DISPLAY_TOP_STATUS, DISPLAY_POPUP}


def main() -> int:
    error_map = NativeOperatorErrorMap.from_tsv(str(MAP_PATH))
    assert error_map.source_count == EXPECTED_SOURCE_COUNT
    checked = 0
    for entry in error_map._entries:
        sample = sample_for_pattern(entry.native_pattern)
        message = error_map.translate(sample)
        assert_safe(message)
        if entry.handling_group == "PASSTHROUGH":
            assert not message.matched
        else:
            assert message.matched, entry.source_id
            assert message.source_id == entry.source_id, (entry.source_id, message.source_id, sample)
        checked += 1
    home = error_map.translate("Can't run a program when not homed")
    assert home.matched and home.title_cn == "需要回零"
    assert home.display_mode == DISPLAY_POPUP
    assert "原点" in home.reason_cn
    jog = error_map.translate("Can't jog joint when not enabled.")
    assert jog.matched and jog.display_mode == DISPLAY_TOP_STATUS
    jog_stopped = error_map.translate("Jog aborted by jog-stop-immediate")
    assert jog_stopped.matched and jog_stopped.display_mode == DISPLAY_LOG_ONLY
    mcode = error_map.translate("Unknown m code used: M123")
    assert mcode.matched and "M123" in mcode.reason_cn
    internal = error_map.translate("invalid V5 wrapped rotary target state")
    assert internal.matched and "旋转轴" in internal.reason_cn
    native_parameter = error_map.translate("Named hal parameter #<axis> not found")
    assert native_parameter.matched and "底层控制" not in native_parameter.reason_cn
    unknown = error_map.translate("LinuxCNC private process /internal/path failed")
    assert not unknown.matched
    assert unknown.display_mode == DISPLAY_POPUP
    assert_safe(unknown)
    alias = translate_internal_alias("POWER_ON_HOME_REQUIRED")
    assert alias.matched and alias.title_cn == "需要回零"
    assert alias.display_mode == DISPLAY_POPUP
    assert "POWER_ON_HOME" not in alias.reason_cn
    print(f"v5_native_operator_error_map_test PASS count={checked}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
