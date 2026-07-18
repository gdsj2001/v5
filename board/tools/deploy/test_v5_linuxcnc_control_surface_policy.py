#!/usr/bin/env python3
from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
import re
import sys
import tempfile
import types


BOARD_ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATHS = {
    "probe": BOARD_ROOT / "services" / "command_gate" / "v5_linuxcncrsh_probe.c",
    "command_init": BOARD_ROOT / "services" / "command_gate" / "init.d" / "v5-linuxcnc-command-gate",
    "ui_init": BOARD_ROOT / "services" / "ui" / "init.d" / "v5-ui-relay",
    "verifier": BOARD_ROOT / "tools" / "deploy" / "verify_v5_board_runtime.sh",
    "emcrsh": BOARD_ROOT.parent / "linuxcnc" / "src" / "emc" / "usr_intf" / "emcrsh.cc",
    "common_nml": BOARD_ROOT.parent / "linuxcnc" / "configs" / "common" / "linuxcnc.nml",
    "bus_ini": BOARD_ROOT / "linuxcnc" / "ini" / "v5_bus.ini",
    "pulse_ini": BOARD_ROOT / "linuxcnc" / "ini" / "v5_pulse.ini",
    "local_nml": BOARD_ROOT / "linuxcnc" / "ini" / "v5_local_shmem.nml",
    "manifest": BOARD_ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv",
    "board_policy": BOARD_ROOT / "tools" / "deploy" / "check_v5_board_runtime_policy.py",
}
RUNTIME_POLICY = BOARD_ROOT / "tools" / "deploy" / "check_v5_runtime_policy.py"


def audit_sources(sources: dict[str, str]) -> list[str]:
    failures = []
    probe = sources["probe"]
    command_init = sources["command_init"]
    ui_init = sources["ui_init"]
    verifier = sources["verifier"]

    if "v5_linuxcncrsh_probe_machine(" not in probe:
        failures.append("PROBE_READ_ONLY_GET_MISSING")
    if "--machine-on" in probe:
        failures.append("PROBE_MACHINE_ON_ARGUMENT_SURVIVOR")
    if "v5_linuxcncrsh_send_machine_on_sequence" in probe:
        failures.append("PROBE_MACHINE_ON_SEQUENCE_SURVIVOR")
    for token in ("ensure_machine_on", "poll_sleep_ms", "#include <unistd.h>"):
        if token in probe:
            failures.append(f"PROBE_WRITE_PATH_DEPENDENCY_SURVIVOR:{token}")
    if "unknown argument: %s" not in probe or "return 2;" not in probe:
        failures.append("PROBE_UNKNOWN_ARGUMENT_FAIL_CLOSED_MISSING")

    for token in ("LINUXCNCRSH_PORT", "LINUXCNCRSH_CONNECTPW"):
        if token in ui_init:
            failures.append(f"UI_DEAD_VARIABLE_SURVIVOR:{token}")
    if "LINUXCNCRSH_ENABLEPW" in command_init:
        failures.append("COMMAND_GATE_DEAD_VARIABLE_SURVIVOR:LINUXCNCRSH_ENABLEPW")

    if "/usr/libexec/8ax/v5_linuxcncrsh_probe --host 127.0.0.1" not in verifier:
        failures.append("VERIFIER_READ_ONLY_PROBE_CALL_MISSING")
    if "grep -q 'MACHINE ON'" not in verifier:
        failures.append("VERIFIER_MACHINE_STATE_READBACK_MISSING")
    if "linuxcncrsh read-only machine-state confirmed" not in verifier:
        failures.append("VERIFIER_READ_ONLY_RESULT_TEXT_MISSING")
    if "linuxcncrsh machine auto-on confirmed" in verifier or "--machine-on" in verifier:
        failures.append("VERIFIER_AUTO_ON_SURVIVOR")

    emcrsh = sources["emcrsh"]
    for token in (
        "#include <arpa/inet.h>",
        '{"bind", 1, NULL, \'b\'}',
        'getopt_long(argc, argv, "hb:e:n:p:s:w:d:"',
        "server_address.sin_addr = bind_address",
    ):
        if token not in emcrsh:
            failures.append(f"EMCRSH_BIND_CONTRACT_MISSING:{token}")
    bind_match = re.search(r"case 'b':(?P<body>.*?)\n\s*case 'e':", emcrsh, re.DOTALL)
    if bind_match is None:
        failures.append("EMCRSH_BIND_CASE_MISSING")
    else:
        bind_body = bind_match.group("body")
        for token in (
            "inet_pton(AF_INET, optarg, &bind_address) != 1",
            'fprintf(stderr, "invalid bind address: %s\\n", optarg)',
        ):
            if token not in bind_body:
                failures.append(f"EMCRSH_BIND_CASE_CONTRACT_MISSING:{token}")
        if "return 2;" not in bind_body:
            failures.append("EMCRSH_INVALID_BIND_EXIT_CODE_INVALID")
    if "server_address.sin_addr.s_addr = htonl(INADDR_ANY)" in emcrsh:
        failures.append("EMCRSH_WILDCARD_BIND_SURVIVOR")

    nml_path = "/opt/8ax/v5/linuxcnc/ini/v5_local_shmem.nml"
    for name in ("bus_ini", "pulse_ini"):
        text = sources[name]
        if text.count(f"NML_FILE = {nml_path}") != 1:
            failures.append(f"INI_LOCAL_NML_MISSING:{name}")
        display = next((line.split("=", 1)[1].strip().split() for line in text.splitlines()
                        if line.startswith("DISPLAY =")), [])
        bind_index = display.index("--bind") if "--bind" in display else -1
        if (display.count("--bind") != 1 or bind_index < 0 or
                display[bind_index + 1:bind_index + 2] != ["127.0.0.1"]):
            failures.append(f"INI_LOOPBACK_BIND_MISSING:{name}")

    local_nml = sources["local_nml"]
    if "TCP=" in local_nml or re.search(r"\bREMOTE\b", local_nml):
        failures.append("NML_REMOTE_TRANSPORT_SURVIVOR")
    normalized = lambda text: [" ".join(line.split()) for line in text.splitlines()
                               if line.startswith("B ") or line.startswith("P ")]
    expected_nml = sources["common_nml"].replace(" TCP=5005", "")
    expected_lines = normalized(expected_nml)
    actual_lines = normalized(local_nml)
    for line in expected_lines:
        if line not in actual_lines:
            fields = line.split()
            prefix = "NML_LOCAL_CHANNEL_MISSING" if fields[0] == "P" else "NML_SHMEM_BUFFER_MISSING"
            failures.append(f"{prefix}:{fields[1]}:{fields[2]}")
    if actual_lines != expected_lines:
        failures.append("NML_CANONICAL_BASELINE_MISMATCH")

    manifest_rows = [line.split("\t") for line in sources["manifest"].splitlines() if line.strip()]
    expected_row = ["linuxcnc", "linuxcnc/ini/v5_local_shmem.nml", nml_path, "0644"]
    if [row for row in manifest_rows if len(row) == 4 and row[2] == nml_path] != [expected_row]:
        failures.append("NML_MANIFEST_ROW_INVALID")

    board_policy = sources["board_policy"]
    for token in (
        'Path(PROC_ROOT) / "net" / "tcp"',
        "audit_linuxcnc_control_listeners()",
        "FAIL_LINUXCNC_NML_TCP_5005",
        "FAIL_LINUXCNCRSH_5007_MISSING",
        "FAIL_LINUXCNCRSH_5007_NON_LOOPBACK",
    ):
        if token not in board_policy:
            failures.append(f"LIVE_LISTENER_POLICY_MISSING:{token}")
    return failures


def expect_mutation_failure(
    sources: dict[str, str], source_name: str, injected: str, expected_marker: str
) -> None:
    mutated = dict(sources)
    mutated[source_name] = injected + "\n" + mutated[source_name]
    failures = audit_sources(mutated)
    assert expected_marker in failures, (expected_marker, failures)


def expect_replacement_failure(
    sources: dict[str, str], source_name: str, old: str, new: str, expected_marker: str
) -> None:
    assert old in sources[source_name], (source_name, old)
    mutated = dict(sources)
    mutated[source_name] = mutated[source_name].replace(old, new, 1)
    failures = audit_sources(mutated)
    assert expected_marker in failures, (expected_marker, failures)


def listener_policy_namespace():
    spec = importlib.util.spec_from_file_location("v5_board_policy", SOURCE_PATHS["board_policy"])
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    definitions = module.REMOTE_CHECK.rsplit("\nrc = 0\n", 1)[0]
    namespace: dict[str, object] = {}
    original_pwd = sys.modules.get("pwd")
    if original_pwd is None:
        sys.modules["pwd"] = types.ModuleType("pwd")
    try:
        exec(definitions, namespace)
    finally:
        if original_pwd is None:
            sys.modules.pop("pwd", None)
    return namespace


def write_proc_listener(path: Path, address: str, port: int) -> None:
    path.write_text(
        "  sl  local_address rem_address st\n"
        f"   0: {address}:{port:04X} 00000000:0000 0A\n",
        encoding="ascii",
    )


def run_listener_policy_case(tcp_lines: list[tuple[str, int]], expected_marker: str | None) -> None:
    namespace = listener_policy_namespace()
    with tempfile.TemporaryDirectory(prefix="v5-listener-policy-") as tmp:
        net = Path(tmp) / "net"
        net.mkdir()
        (net / "tcp").write_text("  sl  local_address rem_address st\n", encoding="ascii")
        (net / "tcp6").write_text("  sl  local_address rem_address st\n", encoding="ascii")
        rows = {"tcp": [], "tcp6": []}
        for table, port in tcp_lines:
            address = "0100007F" if table == "tcp" else "00000000000000000000000001000000"
            rows[table].append(f"   0: {address}:{port:04X} 00000000:0000 0A")
        for table, lines in rows.items():
            (net / table).write_text(
                "  sl  local_address rem_address st\n" + "\n".join(lines) + ("\n" if lines else ""),
                encoding="ascii",
            )
        namespace["PROC_ROOT"] = tmp
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            rc = namespace["audit_linuxcnc_control_listeners"]()
        if expected_marker is None:
            assert rc == 0, stderr.getvalue()
        else:
            assert rc != 0 and expected_marker in stderr.getvalue(), (expected_marker, stderr.getvalue())


def main() -> int:
    sources = {
        name: path.read_text(encoding="utf-8", errors="strict")
        for name, path in SOURCE_PATHS.items()
    }
    failures = audit_sources(sources)
    assert not failures, failures

    expect_mutation_failure(
        sources, "probe", "/* --machine-on */", "PROBE_MACHINE_ON_ARGUMENT_SURVIVOR")
    expect_mutation_failure(
        sources,
        "probe",
        "v5_linuxcncrsh_send_machine_on_sequence(&config);",
        "PROBE_MACHINE_ON_SEQUENCE_SURVIVOR",
    )
    expect_mutation_failure(
        sources,
        "ui_init",
        "LINUXCNCRSH_PORT=${V5_LINUXCNCRSH_PORT:-5007}",
        "UI_DEAD_VARIABLE_SURVIVOR:LINUXCNCRSH_PORT",
    )
    expect_mutation_failure(
        sources,
        "ui_init",
        "LINUXCNCRSH_CONNECTPW=${V5_LINUXCNCRSH_CONNECTPW:-EMC}",
        "UI_DEAD_VARIABLE_SURVIVOR:LINUXCNCRSH_CONNECTPW",
    )
    expect_mutation_failure(
        sources,
        "command_init",
        "LINUXCNCRSH_ENABLEPW=${V5_LINUXCNCRSH_ENABLEPW:-EMCTOO}",
        "COMMAND_GATE_DEAD_VARIABLE_SURVIVOR:LINUXCNCRSH_ENABLEPW",
    )
    expect_replacement_failure(
        sources, "emcrsh", "server_address.sin_addr = bind_address;",
        "server_address.sin_addr.s_addr = htonl(INADDR_ANY);",
        "EMCRSH_WILDCARD_BIND_SURVIVOR")
    expect_replacement_failure(
        sources, "bus_ini", "--bind 127.0.0.1", "--bind 0.0.0.0",
        "INI_LOOPBACK_BIND_MISSING:bus_ini")
    expect_replacement_failure(
        sources, "bus_ini", "127.0.0.1", "127.0.0.10",
        "INI_LOOPBACK_BIND_MISSING:bus_ini")
    bind_case = re.search(r"case 'b':.*?\n\s*case 'e':", sources["emcrsh"], re.DOTALL)
    assert bind_case and "return 2;" in bind_case.group(0)
    expect_replacement_failure(
        sources, "emcrsh", bind_case.group(0),
        bind_case.group(0).replace("return 2;", "return 1;"),
        "EMCRSH_INVALID_BIND_EXIT_CODE_INVALID")
    expect_mutation_failure(
        sources, "local_nml", "B injected SHMEM localhost 1 TCP=5005",
        "NML_REMOTE_TRANSPORT_SURVIVOR")
    local_row = next(
        line for line in sources["local_nml"].splitlines()
        if " ".join(line.split()) == "P emc emcCommand LOCAL localhost RW 0 1.0 0 0")
    expect_replacement_failure(
        sources, "local_nml", local_row, "",
        "NML_LOCAL_CHANNEL_MISSING:emc:emcCommand")

    run_listener_policy_case([("tcp", 5007)], None)
    run_listener_policy_case([("tcp6", 5007)], None)
    run_listener_policy_case([("tcp", 5005), ("tcp", 5007)], "FAIL_LINUXCNC_NML_TCP_5005")
    run_listener_policy_case([], "FAIL_LINUXCNCRSH_5007_MISSING")
    namespace = listener_policy_namespace()
    with tempfile.TemporaryDirectory(prefix="v5-listener-policy-nonloop-") as tmp:
        net = Path(tmp) / "net"
        net.mkdir()
        write_proc_listener(net / "tcp", "00000000", 5007)
        (net / "tcp6").write_text("  sl  local_address rem_address st\n", encoding="ascii")
        namespace["PROC_ROOT"] = tmp
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            assert namespace["audit_linuxcnc_control_listeners"]() != 0
        assert "FAIL_LINUXCNCRSH_5007_NON_LOOPBACK" in stderr.getvalue()
    namespace = listener_policy_namespace()
    with tempfile.TemporaryDirectory(prefix="v5-listener-policy-tcp6-nonloop-") as tmp:
        net = Path(tmp) / "net"
        net.mkdir()
        (net / "tcp").write_text("  sl  local_address rem_address st\n", encoding="ascii")
        write_proc_listener(net / "tcp6", "0" * 32, 5007)
        namespace["PROC_ROOT"] = tmp
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            assert namespace["audit_linuxcnc_control_listeners"]() != 0
        assert "FAIL_LINUXCNCRSH_5007_NON_LOOPBACK" in stderr.getvalue()

    policy = RUNTIME_POLICY.read_text(encoding="utf-8", errors="strict")
    for marker in (
        "LINUXCNC_READ_ONLY_PROBE_MISSING",
        "LINUXCNC_READ_ONLY_UI_INIT_MISSING",
        "LINUXCNC_READ_ONLY_PROBE_CONTRACT_MISSING",
        "LINUXCNC_PROBE_CONTROL_SURFACE_RESURRECTED",
        "LINUXCNC_UI_DEAD_PROBE_VARIABLE_RESURRECTED",
        "LINUXCNC_GATE_DEAD_PROBE_VARIABLE_RESURRECTED",
        "LINUXCNC_AUTO_ON_VERIFY_TEXT_RESURRECTED",
    ):
        assert marker in policy, f"runtime anti-resurrection marker missing: {marker}"
    for retired_marker in (
        "LINUXCNC_MACHINE_ON_PROBE_MISSING",
        "LINUXCNC_MACHINE_ON_UI_INIT_MISSING",
    ):
        assert retired_marker not in policy, f"retired marker survived: {retired_marker}"
    print("V5_LINUXCNC_CONTROL_SURFACE_READ_ONLY_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
