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


def c_registry_descriptors(text):
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

    id_values = {
        name: int(value)
        for name, value in re.findall(
            r"^#define\s+(V5_MOTION_MODEL_ID_[A-Z0-9_]+)\s+(\d+)U\s*$",
            text,
            re.MULTILINE,
        )
    }
    descriptor_pattern = re.compile(
        r'''^\{\s*
        (?P<registry_id>V5_MOTION_MODEL_ID_[A-Z0-9_]+|\d+U)\s*,\s*
        "(?P<canonical>[^"]+)"\s*,\s*"(?P<display>[^"]+)"\s*,\s*
        \{(?P<aliases>[^}]*)\}\s*,\s*
        "(?P<kins_module>[^"]+)"\s*,\s*
        "(?P<kins_coordinates>[^"]+)"\s*,\s*
        "(?P<traj_coordinates>[^"]+)"\s*,\s*
        (?P<wrapped_rotary_mask>\d+)U\s*,\s*
        '(?P<first_rotary_axis>[A-Z])'\s*,\s*'(?P<second_rotary_axis>[A-Z])'\s*,\s*
        (?P<first_status_slot>\d+)U\s*,\s*(?P<second_status_slot>\d+)U\s*,\s*
        (?P<first_g53_center>\d+)U\s*,\s*(?P<second_g53_center>\d+)U\s*,\s*
        (?P<first_center_wcs_component>\d+)U\s*,\s*(?P<second_center_wcs_component>\d+)U\s*,\s*
        (?P<active_axis_count>\d+)U\s*,\s*
        \{(?P<active_axes>[^}]*)\}\s*,\s*
        \{(?P<active_status_slots>[^}]*)\}\s*,?\s*\}$''',
        re.DOTALL | re.VERBOSE,
    )
    models = {}
    for entry in entries:
        match = descriptor_pattern.fullmatch(entry.strip())
        if not match:
            raise SystemExit("cannot parse C active-model descriptor")
        raw_id = match.group("registry_id")
        registry_id = id_values.get(raw_id) if not raw_id.endswith("U") else int(raw_id[:-1])
        canonical = match.group("canonical")
        active_axes = tuple(re.findall(r"'([A-Z])'", match.group("active_axes")))
        active_status_slots = tuple(
            int(value) for value in re.findall(r"(\d+)U", match.group("active_status_slots")))
        active_count = int(match.group("active_axis_count"))
        if (registry_id is None or canonical in models or
                len(active_axes) != active_count or len(active_status_slots) != active_count):
            raise SystemExit(f"invalid C active-model descriptor: {canonical}")
        models[canonical] = {
            "registry_id": registry_id,
            "display": match.group("display"),
            "aliases": tuple(re.findall(r'"([^"]+)"', match.group("aliases"))),
            "kins_module": match.group("kins_module"),
            "kins_coordinates": match.group("kins_coordinates"),
            "traj_coordinates": match.group("traj_coordinates"),
            "wrapped_rotary_mask": int(match.group("wrapped_rotary_mask")),
            "first_rotary_axis": match.group("first_rotary_axis"),
            "second_rotary_axis": match.group("second_rotary_axis"),
            "first_status_slot": int(match.group("first_status_slot")),
            "second_status_slot": int(match.group("second_status_slot")),
            "first_g53_center": int(match.group("first_g53_center")),
            "second_g53_center": int(match.group("second_g53_center")),
            "first_center_wcs_component": int(match.group("first_center_wcs_component")),
            "second_center_wcs_component": int(match.group("second_center_wcs_component")),
            "active_axes": active_axes,
            "active_status_slots": active_status_slots,
        }
    return models


def _python_descriptor_literal(node):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Tuple):
        return tuple(_python_descriptor_literal(item) for item in node.elts)
    if isinstance(node, ast.List):
        return [_python_descriptor_literal(item) for item in node.elts]
    if isinstance(node, ast.Dict):
        return {
            _python_descriptor_literal(key): _python_descriptor_literal(value)
            for key, value in zip(node.keys, node.values)
        }
    if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and
            node.func.id == "tuple" and len(node.args) == 1 and not node.keywords):
        value = _python_descriptor_literal(node.args[0])
        if isinstance(value, str):
            return tuple(value)
    raise SystemExit("runtime active-model descriptor must be a literal projection")


def python_runtime_descriptors(text):
    tree = ast.parse(text)
    mapping = None
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(
            isinstance(target, ast.Name)
            and target.id == "ACTIVE_MODEL_REGISTRY_PROJECTION_BY_CANONICAL"
            for target in node.targets
        ):
            continue
        mapping = _python_descriptor_literal(node.value)
        break
    if mapping is None:
        raise SystemExit("missing runtime ACTIVE_MODEL_REGISTRY_PROJECTION_BY_CANONICAL")
    if not isinstance(mapping, dict) or not mapping:
        raise SystemExit("runtime active-model descriptor projection must be a non-empty dict")
    return mapping


def main():
    makefile = read("linuxcnc/src/Makefile")
    registry = read("board/services/command_gate/v5_motion_model_registry.h")
    runtime_descriptor = read("board/services/drive_profile/v5_active_model_descriptor.py")
    common = read("linuxcnc/src/emc/kinematics/trtfuncs.c")
    switchkins = read("linuxcnc/src/emc/kinematics/switchkins.c")
    ac_wrapper = read("linuxcnc/src/emc/kinematics/xyzac-trt-kins.c")
    ac_branch = read("linuxcnc/src/emc/kinematics/xyzac-trt-funcs.c")
    bc_wrapper = read("linuxcnc/src/emc/kinematics/xyzbc-trt-kins.c")
    bc_branch = read("linuxcnc/src/emc/kinematics/xyzbc-trt-funcs.c")
    native_owner = read("linuxcnc/src/hal/user_comps/v5_native_hal_owner.comp")
    native_dispatch = read("linuxcnc/src/hal/user_comps/v5_native_g53_model.h")
    native_ac = read("linuxcnc/src/hal/user_comps/v5_native_g53_model_ac.h")
    native_bc = read("linuxcnc/src/hal/user_comps/v5_native_g53_model_bc.h")
    scene_dispatch = read("board/services/state_publisher/v5_program_scene_model.c")
    scene_ac = read("board/services/state_publisher/v5_program_scene_model_ac.c")
    scene_bc = read("board/services/state_publisher/v5_program_scene_model_bc.c")

    ac_objects = make_block(makefile, "xyzac-trt-kins")
    bc_objects = make_block(makefile, "xyzbc-trt-kins")
    require(ac_objects, "xyzac-trt-funcs.o", "linuxcnc/src/Makefile:xyzac")
    forbid(ac_objects, "xyzbc-trt-funcs.o", "linuxcnc/src/Makefile:xyzac")
    require(bc_objects, "xyzbc-trt-funcs.o", "linuxcnc/src/Makefile:xyzbc")
    forbid(bc_objects, "xyzac-trt-funcs.o", "linuxcnc/src/Makefile:xyzbc")

    registry_descriptors = c_registry_descriptors(registry)
    runtime_descriptors = python_runtime_descriptors(runtime_descriptor)
    if runtime_descriptors != registry_descriptors:
        raise SystemExit(
            "runtime/C active-model descriptor registry mismatch: "
            f"runtime={runtime_descriptors!r} C={registry_descriptors!r}"
        )

    for token in ("xyzacKinematicsForward", "xyzacKinematicsInverse",
                  "xyzbcKinematicsForward", "xyzbcKinematicsInverse"):
        forbid(common, token, "linuxcnc/src/emc/kinematics/trtfuncs.c")
    require(
        common,
        "strcmp(coordinates, kp->required_coordinates) != 0",
        "linuxcnc/src/emc/kinematics/trtfuncs.c",
    )
    require(common, "static TrtKinematicsContext *trt_context;", "TRT HAL context")
    require(common, "trt_context = hal_malloc(sizeof(*trt_context));", "TRT HAL context")
    require(common, "&(trt_context->x_rot_point)", "TRT HAL context")
    require(common, "&(trt_context->tool_offset)", "TRT HAL context")
    forbid(common, "static TrtKinematicsContext trt_context", "TRT HAL context")
    for setup_index in range(3):
        require(
            switchkins,
            f"if (ksetup{setup_index}(comp_id,coordinates,&kp))",
            f"switchkins setup type {setup_index}",
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

    require(scene_dispatch, "v5_program_scene_model_ac_ops", "scene model dispatcher")
    require(scene_dispatch, "v5_program_scene_model_bc_ops", "scene model dispatcher")
    forbid(scene_dispatch, "V5_MOTION_MODEL_ID_XYZAC_TRT", "scene shared dispatcher")
    forbid(scene_dispatch, "V5_MOTION_MODEL_ID_XYZBC_TRT", "scene shared dispatcher")
    require(scene_ac, "V5_MOTION_MODEL_ID_XYZAC_TRT", "scene AC branch")
    require(scene_ac, "model->primary_axis = 'A'", "scene AC branch")
    require(scene_ac, "model->child_axis = 'C'", "scene AC branch")
    forbid(scene_ac, "V5_MOTION_MODEL_ID_XYZBC_TRT", "scene AC branch")
    forbid(scene_ac, "model->primary_axis = 'B'", "scene AC branch")
    require(scene_bc, "V5_MOTION_MODEL_ID_XYZBC_TRT", "scene BC branch")
    require(scene_bc, "model->primary_axis = 'B'", "scene BC branch")
    require(scene_bc, "model->child_axis = 'C'", "scene BC branch")
    forbid(scene_bc, "V5_MOTION_MODEL_ID_XYZAC_TRT", "scene BC branch")
    forbid(scene_bc, "model->primary_axis = 'A'", "scene BC branch")

    print(
        "V5_ACTIVE_MODEL_BRANCHES_OK "
        f"kinematics=2 native_g53=2 scene=2 runtime_descriptors={len(runtime_descriptors)}"
    )


if __name__ == "__main__":
    main()
