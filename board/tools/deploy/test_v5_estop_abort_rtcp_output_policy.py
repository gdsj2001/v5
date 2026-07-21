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
    if "loadrt and2 count=6" not in hal_text:
        raise AssertionError("five physical drive safety gates are not allocated")
    safety_index = hal_text.index("addf v5-safety-latch.0 servo-thread")
    motion_index = hal_text.index("addf motion-controller servo-thread")
    cia402_index = hal_text.index("addf cia402.0.write-all servo-thread")
    for joint in range(5):
        gate = joint + 1
        addf_token = f"addf and2.{gate} servo-thread"
        amp_token = f"joint.{joint}.amp-enable-out => and2.{gate}.in0"
        enable_token = f"and2.{gate}.out => cia402.{joint}.enable"
        for token in (addf_token, amp_token, enable_token):
            if token not in hal_text:
                raise AssertionError(f"axis {joint} realtime safety gate missing: {token}")
        gate_index = hal_text.index(addf_token)
        if not safety_index < motion_index < gate_index < cia402_index:
            raise AssertionError(
                f"axis {joint} safety gate must run after motion and before cia402 write"
            )
    estop_net = hal_text[
        hal_text.index("net estop-loop ") : hal_text.index("\nnet v5-machine-enabled")
    ]
    for gate in range(1, 6):
        if f"and2.{gate}.in1" not in estop_net:
            raise AssertionError(f"axis safety gate {gate} lacks realtime estop permit")
    for joint in range(5):
        direct_route = (
            f"joint.{joint}.amp-enable-out => cia402.{joint}.enable"
        )
        if direct_route in hal_text:
            raise AssertionError(f"axis {joint} bypasses the realtime estop gate")
    print("V5_ESTOP_ABORT_RTCP_OUTPUT_POLICY_OK")


if __name__ == "__main__":
    main()
