#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import os
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
import subprocess
import sys
import tempfile
import types


BOARD_ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATHS = {
    "init": BOARD_ROOT / "services" / "command_gate" / "init.d" / "v5-linuxcnc-command-gate",
    "rules": BOARD_ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" / "v5-base-overlay" / "files" / "udev" / "99-uio.rules",
    "stepgen": BOARD_ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" / "v5-stepgen-module" / "files" / "zynq_stepgen_hw.c",
    "dna": BOARD_ROOT / "services" / "auth_download" / "device_dna_register_hardware.py",
    "runtime_policy": BOARD_ROOT / "tools" / "deploy" / "check_v5_runtime_policy.py",
    "board_policy": BOARD_ROOT / "tools" / "deploy" / "check_v5_board_runtime_policy.py",
    "ethercat_recipe": BOARD_ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-kernel" / "ethercat-master" / "ethercat-master_git.bb",
}
STEPGEN_RULE = 'SUBSYSTEM=="uio", KERNEL=="uio*", ATTR{name}=="*stepgen*", SYMLINK+="v5-stepgen-uio", OWNER="root", GROUP="petalinux", MODE="0660"'
DNA_RULE = 'SUBSYSTEM=="uio", KERNEL=="uio*", ATTR{name}=="*dna*", SYMLINK+="v5-dna-uio", OWNER="root", GROUP="root", MODE="0600"'
_RUNTIME_POLICY_MODULE = None


def load_runtime_policy_module():
    global _RUNTIME_POLICY_MODULE
    if _RUNTIME_POLICY_MODULE is None:
        spec = importlib.util.spec_from_file_location("v5_runtime_policy", SOURCE_PATHS["runtime_policy"])
        assert spec and spec.loader
        _RUNTIME_POLICY_MODULE = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_RUNTIME_POLICY_MODULE)
    return _RUNTIME_POLICY_MODULE


def audit_static(sources: dict[str, str]) -> list[str]:
    policy = load_runtime_policy_module()
    failures = list(policy.audit_uio_device_permission_sources(sources))
    failures.extend(policy.audit_ethercat_device_permission_sources({
        "recipe": sources["ethercat_recipe"],
        "rules": sources["rules"],
        "init": sources["init"],
        "board_policy": sources["board_policy"],
    }))
    return failures


def mutate(sources: dict[str, str], name: str, old: str, new: str, marker: str) -> None:
    assert old in sources[name], (name, old)
    changed = dict(sources)
    changed[name] = changed[name].replace(old, new, 1)
    failures = audit_static(changed)
    assert marker in failures, (marker, failures)


def load_live_namespace():
    spec = importlib.util.spec_from_file_location("v5_board_policy", SOURCE_PATHS["board_policy"])
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    definitions = module.REMOTE_CHECK.rsplit("\nrc = 0\n", 1)[0]
    namespace: dict[str, object] = {}
    original = {name: sys.modules.get(name) for name in ("pwd", "grp")}
    for name, value in original.items():
        if value is None:
            sys.modules[name] = types.ModuleType(name)
    try:
        exec(definitions, namespace)
    finally:
        for name, value in original.items():
            if value is None:
                sys.modules.pop(name, None)
    return namespace


def valid_records(petalinux_gid: int):
    return [
        {"role": "stepgen", "target": "/dev/uio1", "is_symlink": True,
         "is_char": True, "name": "zynq_stepgen", "mode": 0o660, "uid": 0,
         "gid": petalinux_gid},
        {"role": "dna", "target": "/dev/uio2", "is_symlink": True,
         "is_char": True, "name": "z20_dna_reader", "mode": 0o600, "uid": 0,
         "gid": 0},
    ]


def record_mutation(namespace, key: str, value, marker: str, role: str = "stepgen") -> None:
    records = valid_records(1000)
    record = next(item for item in records if item["role"] == role)
    record[key] = value
    failures = namespace["validate_uio_records"](records, 1000)
    assert any(item.startswith(marker) for item in failures), (marker, failures)


def extract_ethercat_transform(recipe: str) -> str:
    start = recipe.index("    v5_ethercat_count_exact() {\n")
    anchor = recipe.index("    v5_ethercat_call_number=$(awk", start)
    end = recipe.index("\n}", anchor)
    return recipe[start:end]


def write_executable(path: Path, text: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)
    path.chmod(0o755)


def upstream_ethercat_init(target_count: int = 1, existing_calls: int = 0) -> str:
    target = "    if $ETHERCATCTL start; then\n        :\n    fi\n"
    return (
        '#!/bin/sh\nPATH="$PWD/fakebin:$PATH"\nexport PATH\n'
        'ETHERCATCTL="$PWD/fakebin/ethercatctl"\nV5_FIXTURE_DEV="$PWD/dev"\n' +
        ("        v5_ethercat_apply_permissions\n" * existing_calls) +
        (target * target_count)
    )


def transform_ethercat_init(recipe: str, root: Path, target_count: int = 1,
                            existing_calls: int = 0, create_init: bool = True):
    init_dir = root / "root" / "etc" / "init.d"
    init_dir.mkdir(parents=True)
    init_path = init_dir / "ethercat"
    if create_init:
        write_executable(init_path, upstream_ethercat_init(target_count, existing_calls))
    wrapper = root / "transform.sh"
    write_executable(
        wrapper,
        '#!/bin/sh\nset -eu\nD="$PWD/root"\nsysconfdir=/etc\n'
        'bbfatal() { printf "BBFATAL:%s\\n" "$*" >&2; exit 1; }\n' +
        extract_ethercat_transform(recipe) + "\n",
    )
    proc = subprocess.run(["sh", "transform.sh"], cwd=root, text=True,
                          capture_output=True, check=False)
    return proc, init_path


def run_ethercat_hook_case(recipe: str, create_node: bool, failure: str = "") -> tuple[int, str]:
    with tempfile.TemporaryDirectory(prefix="v5-ethercat-hook-") as tmp:
        root = Path(tmp)
        transform, init_path = transform_ethercat_init(recipe, root)
        assert transform.returncode == 0, transform.stdout + transform.stderr
        transformed = init_path.read_text(encoding="utf-8")
        target = "    if $ETHERCATCTL start; then"
        call = "        v5_ethercat_apply_permissions"
        lines = transformed.splitlines()
        assert lines.count(target) == 1 and lines.count(call) == 1
        assert lines.index(call) == lines.index(target) + 1
        transformed = transformed.replace('/dev/EtherCAT*', '"$V5_FIXTURE_DEV"/EtherCAT*')
        transformed = transformed.replace('[ -c "$v5_ethercat_node" ]', '[ -e "$v5_ethercat_node" ]')
        write_executable(init_path, transformed)
        fakebin = root / "fakebin"
        dev = root / "dev"
        fakebin.mkdir()
        dev.mkdir()
        if create_node:
            (dev / "EtherCAT0").write_bytes(b"")
        write_executable(fakebin / "ethercatctl", '#!/bin/sh\nprintf "%s\\n" "$1" >>"$PWD/control.log"\nexit 0\n')
        write_executable(fakebin / "id", '#!/bin/sh\n[ "$V5_FAIL" = id ] && exit 1\necho 1000\n')
        for command in ("chown", "chmod"):
            write_executable(fakebin / command, f'#!/bin/sh\n[ "$V5_FAIL" = {command} ] && exit 1\nexit 0\n')
        write_executable(
            fakebin / "stat",
            '#!/bin/sh\n[ "$V5_FAIL" = stat ] && exit 1\n'
            '[ "$V5_FAIL" = readback ] && { echo 0:1000:666; exit 0; }\n'
            'echo 0:1000:660\n',
        )
        init_relative = init_path.relative_to(root).as_posix()
        syntax = subprocess.run(["sh", "-n", init_relative], cwd=root, check=False)
        assert syntax.returncode == 0
        env = dict(os.environ, V5_FAIL=failure)
        proc = subprocess.run(["sh", init_relative], cwd=root, env=env, check=False)
        log = (root / "control.log").read_text(encoding="utf-8")
        return proc.returncode, log


def main() -> int:
    sources = {name: path.read_text(encoding="utf-8", errors="strict")
               for name, path in SOURCE_PATHS.items()}
    assert not audit_static(sources), audit_static(sources)
    changed = dict(sources)
    changed["init"] += "\nchmod 666 /dev/uio7 2>/dev/null || true\n"
    failures = audit_static(changed)
    assert "UIO_FIXED_DEVICE_PATH_RESURRECTED:init:/dev/uio7" in failures
    assert "UIO_WORLD_CHMOD_RESURRECTED:init" in failures
    changed = dict(sources)
    changed["init"] += "\nchmod 0666 /dev/v5-stepgen-uio\n"
    assert "UIO_WORLD_CHMOD_RESURRECTED:init" in audit_static(changed)
    changed = dict(sources)
    changed["rules"] += '\nKERNEL=="uio*", MODE="0660", GROUP="dialout"\n'
    failures = audit_static(changed)
    assert "UIO_UDEV_RULESET_INVALID" in failures
    assert "UIO_UDEV_BROAD_PERMISSION_RESURRECTED:dialout" in failures
    mutate(sources, "rules", 'MODE="0660"', 'MODE="0666"',
           'UIO_UDEV_BROAD_PERMISSION_RESURRECTED:MODE="0666"')
    mutate(sources, "rules", STEPGEN_RULE + "\n", "", "UIO_UDEV_RULESET_INVALID")
    changed = dict(sources)
    changed["rules"] += STEPGEN_RULE + "\n"
    assert "UIO_UDEV_RULESET_INVALID" in audit_static(changed)
    mutate(sources, "rules", "v5-dna-uio", "v5-dna-uio-wrong", "UIO_UDEV_RULESET_INVALID")
    changed = dict(sources)
    changed["stepgen"] += '\nstatic const char *fallback = "/dev/uio9";\n'
    assert "UIO_FIXED_DEVICE_PATH_RESURRECTED:stepgen:/dev/uio9" in audit_static(changed)
    changed = dict(sources)
    changed["stepgen"] += '\ngetenv   ("V5_STEPGEN_UIO_DEVICE");\n'
    failures = audit_static(changed)
    assert "UIO_STEPGEN_PATH_OVERRIDE_RESURRECTED:getenv" in failures
    changed = dict(sources)
    changed["dna"] = changed["dna"].replace("def read_live_dna()", "def read_live_dna(device_path)", 1)
    assert "UIO_DNA_PATH_OVERRIDE_RESURRECTED" in audit_static(changed)
    mutate(sources, "stepgen", "validate_stepgen_uio_fd(fd, &uio_identity)", "/* fd validation removed */",
           "UIO_STEPGEN_STABLE_CONSUMER_MISSING")
    mutate(sources, "dna", "_validate_dna_uio_fd(fd, identity)", "# fd validation removed",
           "UIO_DNA_STABLE_CONSUMER_MISSING")
    changed = dict(sources)
    changed["init"] += '\nnode=/dev/v5-stepgen-uio; chmod 0666 "$node"\n'
    assert "UIO_WORLD_CHMOD_RESURRECTED:init" in audit_static(changed)
    changed = dict(sources)
    changed["dna"] += '\npath = os.getenv("V5_DNA_UIO")\n'
    assert "UIO_DNA_PATH_OVERRIDE_RESURRECTED" in audit_static(changed)

    namespace = load_live_namespace()
    assert namespace["validate_uio_records"](valid_records(1000), 1000) == []
    record_mutation(namespace, "is_symlink", False, "FAIL_UIO_STEPGEN_SYMLINK")
    record_mutation(namespace, "name", "wrong", "FAIL_UIO_STEPGEN_NAME")
    record_mutation(namespace, "uid", 1, "FAIL_UIO_STEPGEN_UID")
    record_mutation(namespace, "gid", 1, "FAIL_UIO_STEPGEN_GID")
    record_mutation(namespace, "mode", 0o666, "FAIL_UIO_STEPGEN_MODE")
    record_mutation(namespace, "target", "/dev/uio1", "FAIL_UIO_TARGET_ALIAS", role="dna")
    records = valid_records(1000) + [{"role": "unexpected", "target": "/dev/uio9", "mode": 0o620}]
    assert "FAIL_UIO_UNEXPECTED_WRITABLE:/dev/uio9" in namespace["validate_uio_records"](records, 1000)
    assert namespace["validate_uio_consumer_fsids"](
        {"stepgen": (1000, 1000), "dna": (0, 0)}, 1000, 1000) == []
    failures = namespace["validate_uio_consumer_fsids"]({"dna": (0, 0)}, 1000, 1000)
    assert failures == ["FAIL_UIO_STEPGEN_CONSUMER_MISSING"]
    failures = namespace["validate_uio_consumer_fsids"]({"stepgen": (1000, 1000)}, 1000, 1000)
    assert failures == ["FAIL_UIO_DNA_CONSUMER_MISSING"]
    failures = namespace["validate_uio_consumer_fsids"](
        {"stepgen": (0, 0), "dna": (0, 0)}, 1000, 1000)
    assert failures == ["FAIL_UIO_STEPGEN_CONSUMER_FSIDS:(0, 0)"]
    rtapi_token = 'rtapi_pids = pids_by_executable_name("rtapi_app")'
    assert sources["board_policy"].count(rtapi_token) == 1
    changed = dict(sources)
    changed["board_policy"] = changed["board_policy"].replace(rtapi_token, "rtapi_pids = []", 1)
    assert f"UIO_LIVE_POLICY_MISSING:{rtapi_token}" in audit_static(changed)
    changed = dict(sources)
    changed["board_policy"] = changed["board_policy"].replace(
        "failures.extend(validate_uio_consumer_fsids(", "failures.extend((", 1)
    assert "UIO_LIVE_POLICY_MISSING:combined_consumer_validator_call" in audit_static(changed)
    changed = dict(sources)
    changed["board_policy"] = changed["board_policy"].replace(
        "rc |= audit_uio_devices()", "# UIO audit result dropped", 1)
    assert "UIO_LIVE_POLICY_MISSING:rc |= audit_uio_devices()" in audit_static(changed)

    original = {name: namespace[name] for name in (
        "pwd", "grp", "DEV_ROOT", "collect_uio_record", "pids_by_executable_name",
        "read_pid", "read_fs_ids", "validate_uio_consumer_fsids")}
    try:
        namespace["pwd"] = types.SimpleNamespace(
            getpwnam=lambda _name: types.SimpleNamespace(pw_uid=1000))
        namespace["grp"] = types.SimpleNamespace(
            getgrnam=lambda _name: types.SimpleNamespace(gr_gid=1000))
        with tempfile.TemporaryDirectory(prefix="v5-uio-live-") as raw_root:
            namespace["DEV_ROOT"] = raw_root
            records_by_role = {item["role"]: item for item in valid_records(1000)}
            namespace["collect_uio_record"] = lambda role, _path: dict(records_by_role[role])
            namespace["pids_by_executable_name"] = lambda name: [101] if name == "rtapi_app" else []
            namespace["read_pid"] = lambda _path: 202
            namespace["read_fs_ids"] = lambda pid: (1000, 1000) if pid == 101 else (0, 0)
            namespace["validate_uio_consumer_fsids"] = (
                lambda records, uid, gid: ["FAIL_UIO_CONSUMER_VALIDATOR_SENTINEL"])
            stdout = StringIO(); stderr = StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                rc = namespace["audit_uio_devices"]()
            output = stdout.getvalue() + stderr.getvalue()
            assert rc != 0 and "FAIL_UIO_CONSUMER_VALIDATOR_SENTINEL" in output, output
    finally:
        namespace.update(original)

    changed = dict(sources)
    changed["ethercat_recipe"] += "\nchmod 666 /dev/EtherCAT* 2>/dev/null || true\n"
    assert "ETHERCAT_WORLD_WRITABLE_RESURRECTED:recipe" in audit_static(changed)
    changed = dict(sources)
    changed["ethercat_recipe"] += "\nchmod 0666 /dev/EtherCAT7\n"
    failures = audit_static(changed)
    assert "ETHERCAT_WORLD_WRITABLE_RESURRECTED:recipe" in failures
    assert "ETHERCAT_FIXED_DEVICE_PATH_RESURRECTED:recipe:/dev/EtherCAT7" in failures
    changed = dict(sources)
    changed["rules"] += '\nKERNEL=="EtherCAT*", MODE="0660"\n'
    assert "ETHERCAT_SECOND_PERMISSION_OWNER:rules" in audit_static(changed)
    mutate(sources, "ethercat_recipe", "v5_ethercat_start_count=$(v5_ethercat_count_exact",
           "v5_ethercat_start_count=1 # guard removed",
           "ETHERCAT_PERMISSION_HOOK_MISSING:v5_ethercat_start_count=$(v5_ethercat_count_exact")
    changed = dict(sources)
    changed["ethercat_recipe"] = changed["ethercat_recipe"].replace(
        "v5_ethercat_start_count=$(v5_ethercat_count_exact",
        "v5_ethercat_start_count=$(v5_ethercat_count_exact", 1).replace(
            "    v5_ethercat_init=${D}${sysconfdir}/init.d/ethercat\n",
            "    v5_ethercat_init=${D}${sysconfdir}/init.d/ethercat\n    : || true\n", 1)
    assert "ETHERCAT_PERMISSION_TRANSFORM_LENIENT_FALLBACK" in audit_static(changed)
    changed = dict(sources)
    changed["board_policy"] = changed["board_policy"].replace(
        "rc |= audit_ethercat_devices()", "# EtherCAT audit dropped", 1)
    assert "ETHERCAT_LIVE_POLICY_MISSING:rc |= audit_ethercat_devices()" in audit_static(changed)
    records = [{"path": "/dev/EtherCAT0", "is_char": True, "uid": 0,
                "gid": 1000, "mode": 0o660}]
    assert namespace["validate_ethercat_records"](records, 1000) == []
    assert namespace["validate_ethercat_records"]([], 1000) == ["FAIL_ETHERCAT_DEVICE_MISSING"]
    for key, value, marker in (
        ("is_char", False, "FAIL_ETHERCAT_DEVICE_CHAR"),
        ("uid", 1, "FAIL_ETHERCAT_DEVICE_UID"),
        ("gid", 1, "FAIL_ETHERCAT_DEVICE_GID"),
        ("mode", 0o666, "FAIL_ETHERCAT_DEVICE_MODE"),
    ):
        changed_record = [dict(records[0], **{key: value})]
        assert any(item.startswith(marker) for item in namespace["validate_ethercat_records"](changed_record, 1000))
    rc, log = run_ethercat_hook_case(sources["ethercat_recipe"], True)
    assert rc == 0 and log.splitlines() == ["start"], (rc, log)
    rc, log = run_ethercat_hook_case(sources["ethercat_recipe"], False)
    assert rc != 0 and log.splitlines() == ["start", "stop"], (rc, log)
    for failure in ("id", "chown", "chmod", "stat", "readback"):
        rc, log = run_ethercat_hook_case(sources["ethercat_recipe"], True, failure)
        assert rc != 0 and log.splitlines() == ["start", "stop"], (failure, rc, log)
    with tempfile.TemporaryDirectory(prefix="v5-ethercat-zero-") as tmp:
        proc, _path = transform_ethercat_init(sources["ethercat_recipe"], Path(tmp), 0)
        assert proc.returncode != 0 and "expected 1" in proc.stderr
    with tempfile.TemporaryDirectory(prefix="v5-ethercat-multi-") as tmp:
        proc, _path = transform_ethercat_init(sources["ethercat_recipe"], Path(tmp), 2)
        assert proc.returncode != 0 and "expected 1" in proc.stderr
    with tempfile.TemporaryDirectory(prefix="v5-ethercat-old-call-") as tmp:
        proc, _path = transform_ethercat_init(
            sources["ethercat_recipe"], Path(tmp), 1, existing_calls=1)
        assert proc.returncode != 0 and "call already exists" in proc.stderr
    with tempfile.TemporaryDirectory(prefix="v5-ethercat-missing-init-") as tmp:
        proc, _path = transform_ethercat_init(
            sources["ethercat_recipe"], Path(tmp), create_init=False)
        assert proc.returncode != 0 and "EtherCAT init is missing" in proc.stderr
    insertion = "    sed -i '/^    if \\$ETHERCATCTL start; then$/a\\        v5_ethercat_apply_permissions' \"$v5_ethercat_init\""
    assert insertion in sources["ethercat_recipe"]
    for label, mutated in (
        ("missing-call", sources["ethercat_recipe"].replace(insertion, ": # insertion removed", 1)),
        ("duplicate-call", sources["ethercat_recipe"].replace(insertion, insertion + "\n" + insertion, 1)),
        ("non-adjacent-call", sources["ethercat_recipe"].replace(
            insertion,
            insertion + "\n    sed -i '/^    if \\$ETHERCATCTL start; then$/a\\        :' \"$v5_ethercat_init\"",
            1,
        )),
    ):
        with tempfile.TemporaryDirectory(prefix="v5-ethercat-%s-" % label) as tmp:
            proc, _path = transform_ethercat_init(mutated, Path(tmp), 1)
            assert proc.returncode != 0, (label, proc.stdout, proc.stderr)
    print("V5_DEVICE_PERMISSION_UIO_ETHERCAT_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
