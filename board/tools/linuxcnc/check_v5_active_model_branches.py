#!/usr/bin/env python3
import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text, token, owner):
    if token not in text:
        raise SystemExit(f"missing active-model branch token {token!r}: {owner}")


def forbid(text, token, owner):
    if token in text:
        raise SystemExit(f"cross-model active-model token {token!r}: {owner}")


def make_block(makefile, name):
    rows = []
    prefix = f"{name}-objs"
    for row in makefile.splitlines():
        if row.startswith(prefix):
            rows.append(row)
    if not rows:
        raise SystemExit(f"missing kinematics object block: {name}")
    return "\n".join(rows)


def c_registry_axes(text):
    marker = "static const V5MotionModelDescriptor v5_motion_model_registry[] ="
    begin = text.find(marker)
    if begin < 0:
        raise SystemExit("missing C active-model registry")
    outer = text.find("{", begin + len(marker))
    if outer < 0:
        raise SystemExit("missing C active-model registry initializer")
    depth = 1
    entry_begin = None
    entries = []
    index = outer + 1
    while index < len(text) and depth:
        ch = text[index]
        if ch == "{":
            depth += 1
            if depth == 2:
                entry_begin = index
        elif ch == "}":
            if depth == 2 and entry_begin is not None:
                entries.append(text[entry_begin:index + 1])
                entry_begin = None
            depth -= 1
        index += 1

    models = {}
    axes_pattern = re.compile(
        r"(\d+)U,\s*\n\s*\{((?:\s*'[A-Z]'\s*,?)+)\},\s*\n\s*\{"
    )
    for entry in entries:
        strings = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', entry)
        axes_match = axes_pattern.search(entry)
        if not strings or not axes_match:
            raise SystemExit("cannot parse C active-model descriptor")
        canonical = strings[0]
        active_count = int(axes_match.group(1))
        axes = "".join(re.findall(r"'([A-Z])'", axes_match.group(2)))
        if len(axes) != active_count or canonical in models:
            raise SystemExit(f"invalid C active-model axes: {canonical}={axes}")
        models[canonical] = axes
    return models


def python_runtime_axes(text):
    tree = ast.parse(text)
    mapping = None
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(
            isinstance(target, ast.Name)
            and target.id == "ACTIVE_MODEL_AXES_BY_CANONICAL"
            for target in node.targets
        ):
            continue
        if not isinstance(node.value, ast.Dict):
            raise SystemExit("runtime active-model axes owner must be a dict")
        mapping = {}
        for key_node, value_node in zip(node.value.keys, node.value.values):
            if not isinstance(key_node, ast.Constant) or not isinstance(key_node.value, str):
                raise SystemExit("runtime active-model key must be a string")
            if not (
                isinstance(value_node, ast.Call)
                and isinstance(value_node.func, ast.Name)
                and value_node.func.id == "tuple"
                and len(value_node.args) == 1
                and isinstance(value_node.args[0], ast.Constant)
                and isinstance(value_node.args[0].value, str)
                and not value_node.keywords
            ):
                raise SystemExit(
                    f"runtime active-model axes must use tuple(string): {key_node.value}"
                )
            mapping[key_node.value] = value_node.args[0].value
        break
    if mapping is None:
        raise SystemExit("missing runtime ACTIVE_MODEL_AXES_BY_CANONICAL")
    return mapping


def main():
    makefile = read("linuxcnc/src/Makefile")
    registry = read("board/services/command_gate/v5_motion_model_registry.h")
    runtime_store = read("board/services/drive_profile/v5_drive_runtime_store.py")
    common = read("linuxcnc/src/emc/kinematics/trtfuncs.c")
    ac_wrapper = read("linuxcnc/src/emc/kinematics/xyzac-trt-kins.c")
    ac_branch = read("linuxcnc/src/emc/kinematics/xyzac-trt-funcs.c")
    bc_wrapper = read("linuxcnc/src/emc/kinematics/xyzbc-trt-kins.c")
    bc_branch = read("linuxcnc/src/emc/kinematics/xyzbc-trt-funcs.c")
    native_owner = read("linuxcnc/src/hal/user_comps/v5_native_hal_owner.comp")
    native_dispatch = read("linuxcnc/src/hal/user_comps/v5_native_g53_model.h")
    native_ac = read("linuxcnc/src/hal/user_comps/v5_native_g53_model_ac.h")
    native_bc = read("linuxcnc/src/hal/user_comps/v5_native_g53_model_bc.h")
    ui_dispatch = read("board/app/src/v5_main_page_model_projector.c")
    ui_ac = read("board/app/src/v5_main_page_model_projector_ac.c")
    ui_bc = read("board/app/src/v5_main_page_model_projector_bc.c")

    ac_objects = make_block(makefile, "xyzac-trt-kins")
    bc_objects = make_block(makefile, "xyzbc-trt-kins")
    require(ac_objects, "xyzac-trt-funcs.o", "linuxcnc/src/Makefile:xyzac")
    forbid(ac_objects, "xyzbc-trt-funcs.o", "linuxcnc/src/Makefile:xyzac")
    require(bc_objects, "xyzbc-trt-funcs.o", "linuxcnc/src/Makefile:xyzbc")
    forbid(bc_objects, "xyzac-trt-funcs.o", "linuxcnc/src/Makefile:xyzbc")

    expected_axes = {"XYZAC_TRT": "XYZAC", "XYZBC_TRT": "XYZBC"}
    registry_axes = c_registry_axes(registry)
    runtime_axes = python_runtime_axes(runtime_store)
    if registry_axes != expected_axes:
        raise SystemExit(f"unexpected C active-model registry axes: {registry_axes!r}")
    if runtime_axes != registry_axes:
        raise SystemExit(
            "runtime/C active-model axes registry mismatch: "
            f"runtime={runtime_axes!r} C={registry_axes!r}"
        )

    for token in ("xyzacKinematicsForward", "xyzacKinematicsInverse",
                  "xyzbcKinematicsForward", "xyzbcKinematicsInverse"):
        forbid(common, token, "linuxcnc/src/emc/kinematics/trtfuncs.c")
    require(
        common,
        "strcmp(coordinates, kp->required_coordinates) != 0",
        "linuxcnc/src/emc/kinematics/trtfuncs.c",
    )
    for token in ("TO_RAD", "cos(", "sin("):
        forbid(common, token, "linuxcnc/src/emc/kinematics/trtfuncs.c")
    require(ac_wrapper, 'kp->required_coordinates = "XYZAC"', "xyzac wrapper")
    require(ac_wrapper, "kp->allow_duplicates     = 0", "xyzac wrapper")
    require(ac_wrapper, "xyzacKinematicsForward", "xyzac wrapper")
    require(ac_branch, "xyzacKinematicsForward", "xyzac branch")
    forbid(ac_wrapper, "xyzbcKinematics", "xyzac wrapper")
    forbid(ac_branch, "xyzbcKinematics", "xyzac branch")
    for token in ("ctx->joint_b", "ctx->joint_u", "ctx->joint_v", "ctx->joint_w"):
        forbid(ac_branch, token, "xyzac branch")
    for token in ("pos->b = 0;", "pos->u = 0;", "pos->v = 0;", "pos->w = 0;",
                  "computed.b = 0;", "computed.u = 0;", "computed.v = 0;",
                  "computed.w = 0;"):
        require(ac_branch, token, "xyzac branch")
    require(bc_wrapper, 'kp->required_coordinates = "XYZBC"', "xyzbc wrapper")
    require(bc_wrapper, "kp->allow_duplicates     = 0", "xyzbc wrapper")
    require(bc_wrapper, "xyzbcKinematicsForward", "xyzbc wrapper")
    require(bc_branch, "xyzbcKinematicsForward", "xyzbc branch")
    forbid(bc_wrapper, "xyzacKinematics", "xyzbc wrapper")
    forbid(bc_branch, "xyzacKinematics", "xyzbc branch")
    for token in ("ctx->joint_a", "ctx->joint_u", "ctx->joint_v", "ctx->joint_w"):
        forbid(bc_branch, token, "xyzbc branch")
    for token in ("pos->a = 0;", "pos->u = 0;", "pos->v = 0;", "pos->w = 0;",
                  "computed.a = 0;", "computed.u = 0;", "computed.v = 0;",
                  "computed.w = 0;"):
        require(bc_branch, token, "xyzbc branch")

    require(native_owner, "v5_native_g53_model_resolve", "native G53 owner")
    require(native_owner, "strcmp(kins_module, branch->kinematics_module)", "native G53 owner")
    forbid(native_owner, "strcmp(g_config.model", "native G53 owner")
    require(native_dispatch, "v5_native_g53_model_ac_project", "native G53 dispatcher")
    require(native_dispatch, "v5_native_g53_model_bc_project", "native G53 dispatcher")
    require(native_ac, "input->a_y - input->c_y", "native G53 AC branch")
    forbid(native_ac, "input->b_", "native G53 AC branch")
    require(native_bc, "input->b_x - input->c_x", "native G53 BC branch")
    forbid(native_bc, "input->a_", "native G53 BC branch")

    require(ui_dispatch, "v5_main_page_model_projector_ac_ops", "UI model dispatcher")
    require(ui_dispatch, "v5_main_page_model_projector_bc_ops", "UI model dispatcher")
    forbid(ui_dispatch, "V5_MOTION_MODEL_ID_XYZAC_TRT", "UI shared dispatcher")
    forbid(ui_dispatch, "V5_MOTION_MODEL_ID_XYZBC_TRT", "UI shared dispatcher")
    require(ui_ac, "V5_MOTION_MODEL_ID_XYZAC_TRT", "UI AC branch")
    require(ui_ac, "model->first_rotary_axis == 'A'", "UI AC branch")
    require(ui_ac, "model->second_rotary_axis == 'C'", "UI AC branch")
    forbid(ui_ac, "V5_MOTION_MODEL_ID_XYZBC_TRT", "UI AC branch")
    forbid(ui_ac, "model->first_rotary_axis == 'B'", "UI AC branch")
    require(ui_bc, "V5_MOTION_MODEL_ID_XYZBC_TRT", "UI BC branch")
    require(ui_bc, "model->first_rotary_axis == 'B'", "UI BC branch")
    require(ui_bc, "model->second_rotary_axis == 'C'", "UI BC branch")
    forbid(ui_bc, "V5_MOTION_MODEL_ID_XYZAC_TRT", "UI BC branch")
    forbid(ui_bc, "model->first_rotary_axis == 'A'", "UI BC branch")

    print(
        "V5_ACTIVE_MODEL_BRANCHES_OK "
        "kinematics=2 native_g53=2 ui=2 runtime_axes=2"
    )


if __name__ == "__main__":
    main()
