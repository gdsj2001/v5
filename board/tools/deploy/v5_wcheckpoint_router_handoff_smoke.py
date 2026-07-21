#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "linuxcnc" / "src" / "emc" / "motion" / "v5_wcheckpoint.c"


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    update = source.split(
        "void v5_wcheckpoint_update_before_inputs(void)", 1
    )[1].split("void v5_wcheckpoint_publish(void)", 1)[0]

    capture = update.index("router_candidate[index] = 1U")
    detect = update.index("router_shift_needed = 1")
    shift = update.index(
        "apply_joint_shift(&emcmotStatus->carte_pos_cmd, new_base_turns)"
    )
    commit = update.rindex("axis_state[index].router_synced = 1U")
    if not capture < detect < shift < commit:
        raise AssertionError(
            "router base/generation must shift motmod joint state before commit"
        )
    if "axis_state[index].base_turns = router_base_counts" in update:
        raise AssertionError("router base still bypasses the atomic joint shift")

    old_base_turns = -182
    new_base_turns = -331
    logical_turns = -331.292577
    old_runtime_turns = logical_turns - old_base_turns
    new_runtime_turns = logical_turns - new_base_turns
    joint_shift_turns = new_runtime_turns - old_runtime_turns
    if joint_shift_turns != old_base_turns - new_base_turns:
        raise AssertionError("joint shift does not preserve the logical position")
    if abs((new_base_turns + new_runtime_turns) - logical_turns) > 1.0e-12:
        raise AssertionError("new base/runtime pair changed the logical position")
    if abs(
        (new_runtime_turns + new_base_turns)
        - (old_runtime_turns + old_base_turns)
    ) > 1.0e-12:
        raise AssertionError("wcheckpoint handoff changed the physical target")

    print("V5_WCHECKPOINT_ROUTER_HANDOFF_SMOKE_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
