#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MOTION_COMMAND = ROOT / "linuxcnc/src/emc/motion/command.c"
BUS_HAL = ROOT / "board/linuxcnc/hal/v5_bus_2ms.hal"


def abort_block(text: str) -> str:
    start = text.find("case EMCMOT_ABORT:")
    end = text.find("\n\t    break;", start)
    if start < 0 or end <= start:
        raise AssertionError("EMCMOT_ABORT block not found")
    return text[start:end]


def main() -> None:
    motion_text = MOTION_COMMAND.read_text(encoding="utf-8")
    hal_text = BUS_HAL.read_text(encoding="utf-8")
    block = abort_block(motion_text)
    required = (
        "v5_reset_wrapped_rotary_turn_offsets();",
        "if (emcmotConfig->numDIO > 0)",
        "emcmotDioWrite(0, 0);",
    )
    for token in required:
        if token not in block:
            raise AssertionError(f"abort RTCP cleanup missing: {token}")
    if block.index("emcmotDioWrite(0, 0);") > block.index("tpAbort("):
        raise AssertionError("RTCP program request must clear before trajectory abort")
    if (
        "v5-rtcp-gcode-request motion.digital-out-00 => "
        "v5-native-hal-owner.rtcp-gcode-request or2.0.in1"
    ) not in hal_text:
        raise AssertionError("motion.digital-out-00 is not the registered RTCP request")
    print("V5_ESTOP_ABORT_RTCP_OUTPUT_POLICY_OK")


if __name__ == "__main__":
    main()
