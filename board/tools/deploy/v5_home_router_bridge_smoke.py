#!/usr/bin/env python3
"""Focused contract smoke for the V5 atomic BUS router/Home bridge."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
ROUTER = ROOT / "linuxcnc/src/hal/components/v5_bus_axis_router.comp"
OWNER = ROOT / "linuxcnc/src/hal/user_comps/v5_native_hal_owner.comp"
PROTOCOL = ROOT / "linuxcnc/src/hal/user_comps/v5_native_hal_owner_protocol.h"
HAL = ROOT / "board/linuxcnc/hal/v5_bus_2ms.hal"
COMMAND_IPC = ROOT / "board/services/command_gate/v5_command_gate_ipc.h"
COMMAND_SERVER = ROOT / "board/services/command_gate/v5_command_gate_server.c"
HOME_ADAPTER = ROOT / "board/services/command_gate/v5_native_home.c"
HOME_COMPONENT = ROOT / "linuxcnc/src/hal/components/v5_bus_homecomp.comp"
HOME_MOTION = ROOT / "linuxcnc/src/hal/components/v5_bus_home_motion.h"
HOME_MATH = ROOT / "linuxcnc/src/hal/components/v5_bus_home_math.h"
HOME_API = ROOT / "linuxcnc/src/hal/components/v5_bus_home_api.h"
HOME_PINS = ROOT / "linuxcnc/src/hal/components/v5_bus_home_pins.h"
MOTION_CONTROL = ROOT / "linuxcnc/src/emc/motion/control.c"
MOTION_WCHECKPOINT = ROOT / "linuxcnc/src/emc/motion/v5_wcheckpoint.c"
DEFAULT_HOMING = ROOT / "linuxcnc/src/emc/motion/homing.c"
SAFETY = ROOT / "linuxcnc/src/hal/components/v5_safety_latch.comp"
UI_ERROR = ROOT / "board/app/src/v5_ui_shell_operator_error.c"
OPERATOR_ERROR_MAP = ROOT / "board/services/state_publisher/v5_native_operator_error_map.py"
NATIVE_MOTION_PARAMETERS = ROOT / "board/services/command_gate/v5_native_motion_parameters.c"
NATIVE_MOTION_PARAMETERS_H = ROOT / "board/services/command_gate/v5_native_motion_parameters.h"
SETTINGS_APPLY_VALUES = ROOT / "board/services/command_gate/v5_settings_apply_values.c"
SETTINGS_AXIS_RUNTIME = ROOT / "board/app/src/v5_settings_axis_table_runtime.c"

HAL_NAME_LEN = 47
PIN_DECLARATION = re.compile(
    r"^\s*(?:pin|param)\s+(?:in|out|io|rw|ro)\s+\w+\s+([^\s;=]+)",
    re.MULTILINE,
)


@dataclass
class RouterModel:
    table_valid: bool = False
    mapping: list[int] = field(default_factory=lambda: [0, 1, 2, 3, 4])
    zeros: list[int] = field(default_factory=lambda: [0, 0, 0, 0, 0])
    generation: int = 0
    commit: int = 0
    last_seen: int = 0
    rejected: int = 0

    def revoke(self) -> None:
        self.table_valid = False
        self.generation = self.commit = self.last_seen = self.rejected = 0

    def publish(
        self,
        commit: int,
        generation: int,
        mapping: list[int],
        zeros: list[int],
        mapping_records_valid: list[bool],
    ) -> bool:
        if not self.table_valid:
            self.last_seen = 0
        self.table_valid = True
        if commit == 0 or commit == self.last_seen:
            return False
        self.last_seen = commit
        if (
            generation == 0
            or not all(mapping_records_valid)
            or sorted(mapping) != list(range(5))
            or len(zeros) != 5
            or any(value < -(1 << 31) or value >= (1 << 31) for value in zeros)
        ):
            self.rejected = commit
            return False
        self.mapping = list(mapping)
        self.zeros = list(zeros)
        self.generation = generation
        self.commit = commit
        self.rejected = 0
        return True

    def route(self, joint_targets: list[int], slave_actuals: list[int]) -> tuple[list[int], list[int]]:
        if not self.table_valid or not self.commit:
            return [0] * 5, [0] * 5
        slave_targets = [0] * 5
        joint_actuals = [0] * 5
        for joint, slave in enumerate(self.mapping):
            slave_targets[slave] = self.wrap_s32(joint_targets[joint] + self.zeros[joint])
            joint_actuals[joint] = self.wrap_s32(slave_actuals[slave] - self.zeros[joint])
        return slave_targets, joint_actuals

    @staticmethod
    def wrap_s32(value: int) -> int:
        value &= 0xFFFFFFFF
        return value if value <= 0x7FFFFFFF else value - 0x100000000


def require(source: str, token: str) -> None:
    if token not in source:
        raise AssertionError(f"missing contract token: {token}")


def nearest_quantized(value: int, quantum: int) -> int:
    quotient = abs(value) // quantum
    if value < 0:
        quotient = -quotient
    remainder = value - quotient * quantum
    twice = abs(remainder) * 2
    if twice > quantum or (twice == quantum and value < 0):
        quotient += -1 if value < 0 else 1
    return quotient * quantum


def generated_halcompile_names(source: str, component: str) -> list[str]:
    names = []
    for declared in PIN_DECLARATION.findall(source):
        pin = re.sub(r"##\[\d+\]", "00", declared).replace("_", "-")
        names.append(f"{component}.{pin}")
    return names


def require_hal_name_lengths(
    owner: str,
    router: str,
    safety: str,
    home_pins: str,
) -> None:
    names = generated_halcompile_names(owner, "v5-native-hal-owner")
    names.extend(generated_halcompile_names(router, "v5-bus-axis-router"))
    names.extend(generated_halcompile_names(safety, "v5-safety-latch.0"))
    names.extend(
        value.replace("%d", "0").replace("%c", "A")
        for value in re.findall(r'"([^"]*v5-bus-home[^"]*)"', home_pins)
    )
    overlong = sorted(
        ((len(name), name) for name in names if len(name) > HAL_NAME_LEN),
        reverse=True,
    )
    if overlong:
        details = ", ".join(f"{length}:{name}" for length, name in overlong)
        raise AssertionError(f"HAL name exceeds HAL_NAME_LEN={HAL_NAME_LEN}: {details}")


def main() -> int:
    router = ROUTER.read_text(encoding="utf-8")
    owner = OWNER.read_text(encoding="utf-8")
    protocol = PROTOCOL.read_text(encoding="utf-8")
    hal = HAL.read_text(encoding="utf-8")
    command_ipc = COMMAND_IPC.read_text(encoding="utf-8")
    command_server = COMMAND_SERVER.read_text(encoding="utf-8")
    home_adapter = HOME_ADAPTER.read_text(encoding="utf-8")
    home_component = HOME_COMPONENT.read_text(encoding="utf-8")
    home_motion = HOME_MOTION.read_text(encoding="utf-8")
    home_math = HOME_MATH.read_text(encoding="utf-8")
    home_api = HOME_API.read_text(encoding="utf-8")
    home_pins = HOME_PINS.read_text(encoding="utf-8")
    motion_control = MOTION_CONTROL.read_text(encoding="utf-8")
    motion_wcheckpoint = MOTION_WCHECKPOINT.read_text(encoding="utf-8")
    default_homing = DEFAULT_HOMING.read_text(encoding="utf-8")
    safety = SAFETY.read_text(encoding="utf-8")
    ui_error = UI_ERROR.read_text(encoding="utf-8")
    operator_error_map = OPERATOR_ERROR_MAP.read_text(encoding="utf-8")
    native_motion_parameters = NATIVE_MOTION_PARAMETERS.read_text(encoding="utf-8")
    native_motion_parameters_h = NATIVE_MOTION_PARAMETERS_H.read_text(encoding="utf-8")
    settings_apply_values = SETTINGS_APPLY_VALUES.read_text(encoding="utf-8")
    settings_axis_runtime = SETTINGS_AXIS_RUNTIME.read_text(encoding="utf-8")

    require_hal_name_lengths(owner, router, safety, home_pins)

    for token in (
        "pin in u32 joint-axis-code-##[5]",
        "pin out float wcp-base-##[3]",
        "pin out s32 wcp-runtime-##[3]",
        "pin out u32 wcp-gen-##[3]",
        "v5_bus_router_wcheckpoint_quantum",
        "v5_bus_router_nearest_quantized",
        "physical_target = v5_bus_router_add_offset",
    ):
        require(router, token)
    for token in (
        "v5-bus-axis-router.wcp-base-00 => motion.v5-wcheckpoint-a-router-base-counts",
        "v5-bus-axis-router.wcp-runtime-00 => motion.v5-wcheckpoint-a-router-runtime-counts",
        "v5-bus-axis-router.wcp-base-02 => motion.v5-wcheckpoint-c-router-base-counts",
        "v5-bus-axis-router.wcp-runtime-02 => motion.v5-wcheckpoint-c-router-runtime-counts",
        "v5-native-hal-owner.home-axis-code-04 => v5-bus-axis-router.joint-axis-code-04",
    ):
        require(hal, token)
    for token in (
        "motion.v5-wcheckpoint-%c-router-base-counts",
        "motion.v5-wcheckpoint-%c-router-runtime-counts",
        "motion.v5-wcheckpoint-%c-router-generation",
        "motion.v5-wcheckpoint-%c-router-valid",
        "fmod(fabs(router_base_counts), quantum_counts) == 0.0",
    ):
        require(motion_wcheckpoint, token)

    current_counts = -1072799791
    output_crev = 3600000
    reducer_num, reducer_den = 20, 1
    turn_quantum = reducer_den
    base_counts = nearest_quantized(current_counts, output_crev * turn_quantum)
    if base_counts != -1072800000 or current_counts - base_counts != 209:
        raise AssertionError("boot wcheckpoint did not center the observed C-axis count")
    if base_counts + (current_counts - base_counts) != current_counts:
        raise AssertionError("router base/runtime readback did not preserve logical counts")
    five_turn_target = current_counts - 5 * output_crev
    if five_turn_target - base_counts != -17999791:
        raise AssertionError("cc-ac five-turn target did not consume the boot base")
    if nearest_quantized(3500000, output_crev * 2) != 0:
        raise AssertionError("fractional reducer must use an integer motor-turn quantum")

    for dead in (
        "slave-fault-##", "slave-positive-limit-##", "slave-negative-limit-##",
        "slave-home-switch-##", "joint-fault-##", "joint-positive-limit-##",
        "joint-negative-limit-##", "joint-home-switch-##", "slave-enable-##",
    ):
        if dead in router:
            raise AssertionError(f"retired router pin survived: {dead}")
    require(router, "function read fp;")
    require(router, "function write nofp;")
    require(router, "option singleton yes;")
    require(router, "rejected-commit-seq")
    require(router, "last_seen_commit_seq")
    if "joint-config-valid" in router:
        raise AssertionError("BUS router still depends on Home readiness")
    require(router, "joint-zero-counts-##[5]")
    require(router, "v5_bus_router_sub_offset")
    require(router, "v5_bus_router_add_offset")
    require(owner, "__sync_synchronize();\n    home_table_commit_seq = g_home_stage_commit_seq;")
    require(owner, "wait_home_router_ack")
    require(owner, "NATIVE_HOME_ROUTER_REJECTED")
    require(owner, "home_transaction(joint) != transaction")
    require(owner, "home_status_consistent")
    require(owner, "selected = home_failed_joint;")
    require(owner, "response->home_current_mask = home_failure_current_mask")
    require(protocol, "#define V5_NATIVE_HAL_OWNER_VERSION 7u")
    require(protocol, "V5_NATIVE_HOME_CONFIG_HOME_READY")
    require(command_ipc, "#define V5_COMMAND_GATE_IPC_VERSION 5u")
    require(command_ipc, "V5_COMMAND_GATE_IPC_OP_PROBE_AXIS_SLAVE_MAPPING = 6")
    require(command_server, "load_axis_slave_mapping_status();")
    require(command_server, "g_axis_slave_mapping_applicable &&")
    require(command_server, '"ALL_HOME_AXIS_SLAVE_MAPPING_INVALID"')
    require(protocol, "status_home_router_rejected_commit_seq")
    require(protocol, "rtcp_home_ack_transaction")
    require(hal, "v5-native-hal-owner.home-router-commit-seq")
    require(hal, "v5-bus-axis-router.table-mapping-valid")
    require(hal, "v5-bus-axis-router.joint-zero-counts-00")
    require(hal, "v5-native-hal-owner.home-rtcp-ack-transaction")
    require(hal, "motion.v5-bus-home-failed-joint")
    require(owner, "home_config_valid(joint) = g_home_stage[joint].home_ready ? 1 : 0;")
    require(command_server, "project_native_bus_mapping(")
    require(command_server, "records[axis->status_slot].home_ready = axis->bus_zero_evidence_known ? 1 : 0;")
    projection_failure = command_server.split(
        "if (g_axis_slave_mapping_applicable && g_axis_slave_mapping_valid &&", 1
    )[1].split("if (!v5_linuxcncrsh_gate_preconnect", 1)[0]
    for token in (
        "g_axis_slave_mapping_status_available = 1;",
        "g_axis_slave_mapping_valid = 0;",
        "g_axis_slave_mapping_generation = 0U;",
        '"NATIVE_BUS_MAPPING_PROJECTION_FAILED"',
    ):
        require(projection_failure, token)
    if projection_failure.index("g_axis_slave_mapping_valid = 0;") > projection_failure.index(
        "BUS motion remains fail-closed"
    ):
        raise AssertionError("BUS mapping projection failure publishes stale valid state")
    require(home_adapter, "progress.current_axis_mask = status->home_current_mask;")
    require(home_adapter, "status->home_axis_code_by_joint[joint]")
    require(home_adapter, 'case 21U: return "ALL_HOME_PLAN_STALE";')
    home_wait = home_adapter.split(
        "static int wait_native_all_homed", 1
    )[1].split("V5LinuxcncrshSendStatus v5_native_home_send", 1)[0]
    fatal_status = home_wait.index(
        "(native_status.home_failed_mask || native_status.home_cancelled_mask)"
    )
    aggregate_consistency = home_wait.index("!native_status.home_status_consistent")
    if fatal_status >= aggregate_consistency:
        raise AssertionError(
            "matching Home failure/cancellation must terminate before aggregate consistency wait"
        )
    require(home_wait, "native_status.home_transaction == transaction")
    require(home_wait, "return cancelled ? -1 : -2;")
    require(home_component, "native_home_run_mask(mapping_mask)")
    require(home_component, "H[j].home_sequence")
    require(home_component, "resume_next_native_sequence()")
    require(home_motion, "v5_home_sequence_stage_mask")
    require(home_api, "v5_home_sequence_valid(H[joint].home_sequence)")
    if "home_sequence" in native_motion_parameters or "home_sequence" in native_motion_parameters_h:
        raise AssertionError("Command Gate HOME_SEQUENCE shadow survived")
    require(settings_apply_values, 'snprintf(ini_value, ini_cap, "999")')
    require(settings_apply_values, 'strcmp(raw, "999") == 0')
    require(settings_axis_runtime, 'strcmp(row->home_sequence, "999") == 0')
    home_order_block = settings_apply_values.split(
        'if (strcmp(field_key, "home_order") == 0)', 1
    )[1].split('if (strcmp(field_key, "precision") == 0)', 1)[0]
    if 'snprintf(ini_value, ini_cap, "-1")' in home_order_block:
        raise AssertionError("disabled HOME_SEQUENCE is still encoded as synchronized -1")

    retired_home_tolerance_tokens = (
        "home-arrival-tol",
        "home-readback-tol",
        "home-failure-tol",
        "arrival_tolerance_counts",
        "home_tolerance_counts",
        "home_tolerance",
        "V5_ARRIVAL",
        "ALL_HOME_TARGET_OUT_OF_TOLERANCE",
        "V5_HOME_ROTARY_CORRECTION_MAX",
        "V5_HOME_ROTARY_CORRECTION_REQUIRED",
        "rotary_correction_attempts",
        "v5_home_rotary_correction_state",
    )
    retired_scope = "\n".join(
        (owner, protocol, hal, command_ipc, home_adapter, home_component,
         home_motion, home_math, home_pins, ui_error, operator_error_map)
    )
    for token in retired_home_tolerance_tokens:
        if token in retired_scope:
            raise AssertionError(f"retired Home tolerance token survived: {token}")

    # The execution point is owned by the fresh post-RTCP snapshot.  Freezing
    # an axis actual before force-off would reject a legitimate coordinate
    # window update as V5_PLAN_STALE before any Home motion can start.
    rtcp_ack = home_component.index("txn_phase = V5_TXN_PLAN;")
    plan_phase = home_component.index("if (txn_phase == V5_TXN_PLAN)")
    freeze_call = home_component.index("freeze_execution_plan(")
    if not rtcp_ack < plan_phase < freeze_call:
        raise AssertionError("Home execution plan is not frozen after fresh RTCP OFF")
    if home_component.count("freeze_execution_plan(") != 1:
        raise AssertionError("Home execution plan must have one post-RTCP freeze owner")

    if home_component.count("*P->rtcp_force_off = 1;") != 1:
        raise AssertionError("Home must assert one transaction-scoped RTCP force-off request")
    finish_block = home_motion.split("static void finish_transaction(void)", 1)[1].split(
        "static void fail_transaction", 1)[0]
    require(finish_block, "*P->rtcp_force_off = 0;")
    if "*P->rtcp_force_off = 1;" in finish_block:
        raise AssertionError("Home terminal must release, not reassert, RTCP force-off")

    for token in (
        "failure = update_stage_start_barrier(&failed_joint);",
        "physical_complete_mask != run_mask",
        "v5_terminal_readback_ready(&failed_joint, &failure)",
        "v5_commit_all_homed()",
        "v5_home_joint_count_supported(njoints, V5_HOME_JOINTS)",
    ):
        require(home_component, token)
    sequence_step = home_component.index("do_homing_sequence();")
    barrier_step = home_component.index("failure = update_stage_start_barrier(&failed_joint);", sequence_step)
    joint_loop = home_component.index("for (j = 0; j < all_joints; ++j)", barrier_step)
    terminal_step = home_component.index("v5_terminal_readback_ready", joint_loop)
    atomic_gate = home_component.index("v5_home_atomic_commit_ready", terminal_step)
    atomic_commit = home_component.index("v5_commit_all_homed", atomic_gate)
    if not sequence_step < barrier_step < joint_loop < terminal_step < atomic_gate < atomic_commit:
        raise AssertionError("Home sequence/barrier/terminal/atomic-commit order regressed")
    for token in (
        "runtime != s->transaction_start_runtime_counts",
        "v5_home_runtime_target_position(",
        "resume_target = s->zero_cycle_return_pending",
        "s->motion_target_counts : s->target_counts",
        "start_ready_mask = ready",
        "start_release_mask = v5_home_stage_release_ready",
        "start_release_mask & (1u << j)",
        "v5_mark_joint_physical_complete",
        "v5_commit_all_homed",
    ):
        require(home_motion, token)
    mark_begin = home_motion.index("static void v5_mark_joint_physical_complete")
    commit_begin = home_motion.index("static void v5_commit_all_homed")
    mark_body = home_motion[mark_begin:commit_begin]
    for forbidden in ("joint->pos_cmd =", "joint->pos_fb =", "joint->motor_offset =", "H[j].homed = 1"):
        if forbidden in mark_body:
            raise AssertionError(f"per-axis Home path commits terminal state early: {forbidden}")
    for forbidden in (
        "coordinate_sync", "post_sync", "v5_home_prepare_coordinate_sync",
        "joint->pos_cmd =", "joint->pos_fb =", "joint->motor_offset =",
    ):
        if forbidden in home_motion or forbidden in home_math or forbidden in home_component:
            raise AssertionError(f"retired Home coordinate rewrite survived: {forbidden}")
    if home_motion.count("joint->free_tp.curr_pos = joint->pos_fb;") != 1:
        raise AssertionError("free_tp current position may only be initialized for a real motion leg")
    if home_motion.count("H[j].homed = 1") != 1:
        raise AssertionError("homed must have one atomic terminal owner")
    if "v5_home_target_reached" in home_motion or "v5_home_target_reached" in home_math:
        raise AssertionError("retired Home exact target terminal gate survived")
    require(home_math, "static int v5_home_runtime_target_position")
    if "joint->pos_fb + (double)delta / cfg->counts_per_unit" in home_motion:
        raise AssertionError("Home motion target regressed to relative joint feedback arithmetic")
    require(home_math, "static int v5_home_terminal_sample_stable")
    require(home_motion, "v5_home_terminal_sample_stable(")
    ready = home_motion.index("start_ready_mask = ready")
    release = home_motion.index("start_release_mask = v5_home_stage_release_ready", ready)
    release_gate = home_motion.index("start_release_mask & (1u << j)", release)
    start_joint = home_motion.index("v5_start_joint(j, current, runtime, generation)", release_gate)
    if not ready < release < release_gate < start_joint:
        raise AssertionError("same-sequence stable barrier does not guard motion start")
    pending_target = home_motion.index("resume_target = s->zero_cycle_return_pending")
    resume_leg = home_motion.index(
        "v5_start_motion_leg(j, logical_current, runtime, resume_target)", pending_target
    )
    outbound_complete = home_motion.index("s->zero_cycle_return_pending = 0", resume_leg)
    return_leg = home_motion.index("j, current, runtime, s->target_counts", outbound_complete)
    physical_complete = home_motion.index("v5_mark_joint_physical_complete", return_leg)
    if not pending_target < resume_leg < outbound_complete < return_leg < physical_complete:
        raise AssertionError("zero-cycle fixed outbound/return/physical-complete order regressed")
    require(home_math, "phase <= counts_per_rev - phase ? -phase")
    require(home_math, "v5_home_atomic_commit_ready")
    require(home_api, "bool homing_wcheckpoint_reset_required(void) { return 0; }")
    require(home_api, "V5_HOME_EXPORT(homing_wcheckpoint_reset_required);")
    require(default_homing, "bool homing_wcheckpoint_reset_required(void) { return 1; }")
    require(default_homing, "EXPORT_SYMBOL(homing_wcheckpoint_reset_required);")
    require(motion_control, "if (homing_wcheckpoint_reset_required())")
    if "v5_reset_wrapped_rotary_turn_offsets();" not in motion_control:
        raise AssertionError("successful Home must still clear the G-code wrapped turn offset")

    model = RouterModel()
    # Missing Home readiness is represented outside the router.  The mapping
    # record still commits with a neutral offset so BUS semantic actual remains
    # available for the repair UI.
    mapping_only = RouterModel()
    assert mapping_only.publish(
        1, 76, [0, 1, 2, 3, 4], [0, 0, 0, 0, 0], [True] * 5
    )
    assert mapping_only.generation == 76
    zeros = [1000, 2000, 3000, 4000, 5000]
    assert model.publish(1, 77, [0, 1, 2, 3, 4], zeros, [True] * 5)
    targets, actuals = model.route(
        [10, 20, 30, 40, 50], [1100, 2200, 3300, 4400, 5500]
    )
    assert targets == [1010, 2020, 3030, 4040, 5050]
    assert actuals == [100, 200, 300, 400, 500]

    # A same-generation, newer commit must atomically swap X and Z in both directions.
    assert model.publish(2, 77, [2, 1, 0, 3, 4], zeros, [True] * 5)
    targets, actuals = model.route(
        [10, 20, 30, 40, 50], [3100, 2200, 1300, 4400, 5500]
    )
    assert targets == [3030, 2020, 1010, 4040, 5050]
    assert actuals == [300, 200, 100, 400, 500]

    # Signed-32 PDO wrap must be inverse in both directions.
    assert RouterModel.wrap_s32(0x7FFFFFFF + 1) == -0x80000000
    assert RouterModel.wrap_s32(-0x80000000 - 1) == 0x7FFFFFFF

    # A commit edge with a half table is rejected once; later pin writes cannot heal it.
    assert not model.publish(
        3, 77, [2, 1, 0, 3, 4], zeros, [True, True, False, True, True]
    )
    assert model.rejected == 3
    assert not model.publish(3, 77, [2, 1, 0, 3, 4], zeros, [True] * 5)
    model.revoke()  # owner consumer-ACK failure path explicitly fails closed
    assert model.route([1] * 5, [2] * 5) == ([0] * 5, [0] * 5)

    # An old per-joint transaction must never be folded into a newer global run.
    global_txn = 12
    per_joint_txn = [12, 12, 11, 12, 12]
    exact = [index for index, txn in enumerate(per_joint_txn) if txn == global_txn]
    assert exact == [0, 1, 3, 4]

    print("V5_HOME_ROUTER_BRIDGE_SMOKE PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
