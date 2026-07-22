#!/usr/bin/env python3
"""Validate the off-screen page queue and publish the one-shot UI ready gate."""
from __future__ import annotations
import argparse, binascii, ctypes, errno, json, math, os, re, select, struct, subprocess, tempfile, time, urllib.error, urllib.parse, urllib.request, uuid
from pathlib import Path
from v5_remote_ui_local_client import fetch_authenticated_json
from v5_status_shm_reader import status_shm_payload_valid
from v5_ui_main_cache_contract import validate_main_cache_trace
BOOT_CACHE_POLICY = "main_first_navigation_lazy_v1"
REGISTERED_PAGE_COUNT = 9
EXPECTED_CACHE_BUDGET_BYTES = 1024 * 600 * 4 * 13
MAIN_CACHE_LINE_RE = re.compile(
    r"^V5_UI_MAIN_CACHE event=end page=(?P<page>[a-z]+) slot=(?P<slot>[0-9]+) "
    r"create_ok=(?P<create_ok>[01]) apply_ok=(?P<apply_ok>[01]) "
    r"render_ok=(?P<render_ok>[01]) capture_ok=(?P<capture_ok>[01]) "
    r"cache_valid=(?P<cache_valid>[01]) invalidation_clean=(?P<invalidation_clean>[01]) "
    r"elapsed_us=(?P<elapsed>[0-9]+) "
    r"create_us=(?P<create>[0-9]+) prepare_us=(?P<prepare>[0-9]+) "
    r"cpu_pct_x100=(?P<cpu_pct_x100>[0-9]+) "
    r"budget_bytes=(?P<budget>[0-9]+)$"
)
INPUT_BARRIER_SCHEMA = "v5.ui_input_barrier.v1"
FAILURE_SCHEMA = "v5.ui_failure.v1"
from v5_ui_boot_inputs import (
    BUS_INI, BUS_STATUS_PATH, KERNEL_BOOT_ID_PATH, MODAL_FMT, MODAL_PATH,
    POSITION_FMT, POSITION_LOCK, POSITION_PATH, POSITION_PID, POSITION_SEQ_OFFSET,
    PUBLISHER_OWNER_NAMES, ReadyError, STATE_FRAME_SIZE, STATE_LOCK, STATE_PATH,
    STATE_PAYLOAD_SIZE, STATE_PID, STATE_SCENE_MARKER_COUNT_OFFSET,
    STATE_SCENE_POINT_COUNT_OFFSET, STATE_SCENE_SEGMENT_COUNT_OFFSET,
    STATE_SEQ_OFFSET, STATE_TRAJECTORY_COUNT_OFFSET, ShmEventWatcher, WCS_FMT,
    WCS_LOCK, WCS_PATH, WCS_PID, accept_identity, cpu_lists_are_cpu1,
    current_pre_ui_inputs, fnv32, identity_cache_from_tokens, identity_tokens,
    proc_argv, proc_start_ticks, publisher_identities, read_atomic,
    read_kernel_boot_id, read_record, read_state_seqlock, require_cpu1,
    require_lifecycle_lock_held, wait_pre_ui_inputs,
)
class FinalBarrierError(ReadyError): pass


def build_input_barrier_snapshot(result, expected_ini):
    if expected_ini != BUS_INI:
        raise ReadyError("pre-UI snapshot requires the canonical BUS INI")
    if not isinstance(result, dict):
        raise ReadyError("pre-UI barrier result malformed")
    tokens = result.get("owner_identities")
    identity_cache_from_tokens(tokens)
    markers = result.get("markers")
    if not isinstance(markers, dict) or set(markers) != {"position", "state", "wcs", "modal"}:
        raise ReadyError("pre-UI publisher marker set incomplete")
    try:
        normalized_markers = {name: int(value or 0) for name, value in markers.items()}
    except (TypeError, ValueError) as exc:
        raise ReadyError("pre-UI publisher markers malformed") from exc
    if not all(normalized_markers.values()):
        raise ReadyError("pre-UI publisher markers unavailable")
    return {
        "schema": INPUT_BARRIER_SCHEMA,
        "boot_id": read_kernel_boot_id(),
        "expected_ini": expected_ini,
        "owner_identities": tokens,
        "markers": normalized_markers,
        "created_monotonic_ns": time.monotonic_ns(),
    }
def read_input_barrier_snapshot(path, expected_ini):
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError) as exc:
        raise ReadyError(f"cannot read pre-UI publisher snapshot path={path}") from exc
    if (not isinstance(payload, dict) or payload.get("schema") != INPUT_BARRIER_SCHEMA or
            payload.get("boot_id") != read_kernel_boot_id() or
            payload.get("expected_ini") != expected_ini):
        raise ReadyError("pre-UI publisher snapshot identity mismatch")
    identity_cache_from_tokens(payload.get("owner_identities"))
    markers = payload.get("markers")
    if not isinstance(markers, dict) or set(markers) != {"position", "state", "wcs", "modal"}:
        raise ReadyError("pre-UI publisher snapshot marker set incomplete")
    return payload
def validate_final_publisher_barrier(snapshot_path, expected_ini,
                                     checker=current_pre_ui_inputs):
    try:
        snapshot = read_input_barrier_snapshot(snapshot_path, expected_ini)
        identities = identity_cache_from_tokens(snapshot["owner_identities"])
    except ReadyError as exc:
        raise FinalBarrierError(str(exc)) from exc
    try:
        markers, owners = (checker(identities, True, expected_ini)
                           if checker is current_pre_ui_inputs else checker(identities))
    except ReadyError as exc:
        if any(token in str(exc) for token in (
                "owner changed", "PID/start mismatch", "owner lock is not held",
                "owner argv changed", "owner record mismatch")):
            raise FinalBarrierError(str(exc)) from exc
        raise
    if set(owners) != set(PUBLISHER_OWNER_NAMES):
        raise FinalBarrierError("final publisher owner set incomplete")
    if not isinstance(markers, dict) or set(markers) != {"position", "state", "wcs", "modal"}:
        raise FinalBarrierError("final publisher marker set incomplete")
    try:
        final_markers = {name: int(value or 0) for name, value in markers.items()}
        baseline_markers = {name: int(value or 0) for name, value in snapshot["markers"].items()}
    except (TypeError, ValueError) as exc:
        raise ReadyError("final publisher markers malformed") from exc
    if (not all(final_markers.values()) or
            any(final_markers[name] < baseline_markers[name] for name in final_markers)):
        raise FinalBarrierError("final publisher generation/freshness regressed")
    return {
        "owner_identities": identity_tokens(identities),
        "baseline_markers": baseline_markers,
        "final_markers": final_markers,
    }
def parse_main_cache(text: str) -> dict | None:
    rows: list[dict] = []
    for line in text.splitlines():
        match = MAIN_CACHE_LINE_RE.match(line.strip())
        if not match:
            continue
        rows.append({
            "page": match.group("page"),
            "slot": int(match.group("slot")),
            "create_ok": int(match.group("create_ok")),
            "apply_ok": int(match.group("apply_ok")),
            "render_ok": int(match.group("render_ok")),
            "capture_ok": int(match.group("capture_ok")),
            "elapsed_us": int(match.group("elapsed")),
            "create_us": int(match.group("create")),
            "prepare_us": int(match.group("prepare")),
            "cpu_pct_x100": int(match.group("cpu_pct_x100")),
            "cache_valid": int(match.group("cache_valid")),
            "invalidation_clean": int(match.group("invalidation_clean")),
            "budget_bytes": int(match.group("budget")),
        })
    if len(rows) > 1:
        raise ReadyError(f"multiple Main cache completion rows found count={len(rows)}")
    return rows[0] if rows else None
def validate_main_cache(row: dict | None) -> None:
    if not isinstance(row, dict):
        raise ReadyError("Main cache completion row unavailable")
    if row["page"] != "main" or row["slot"] != 0:
        raise ReadyError(f"Main cache identity invalid row={row!r}")
    for key in (
        "create_ok", "apply_ok", "render_ok", "capture_ok",
        "cache_valid", "invalidation_clean",
    ):
        if row[key] != 1:
            raise ReadyError(f"Main cache incomplete key={key} row={row!r}")
    if row["elapsed_us"] <= 0 or row["create_us"] <= 0 or row["prepare_us"] <= 0:
        raise ReadyError(f"Main cache timing unavailable row={row!r}")
    if row["create_us"] + row["prepare_us"] > row["elapsed_us"]:
        raise ReadyError(f"Main cache timing counters exceed elapsed row={row!r}")
    if row["budget_bytes"] != EXPECTED_CACHE_BUDGET_BYTES:
        raise ReadyError(
            f"cache budget mismatch actual={row['budget_bytes']} "
            f"expected={EXPECTED_CACHE_BUDGET_BYTES}"
        )
def read_status_field(pid: int, field: str) -> str:
    try:
        lines = Path(f"/proc/{pid}/status").read_text(encoding="ascii", errors="replace").splitlines()
    except OSError:
        return ""
    prefix = field + ":"
    for line in lines:
        if line.startswith(prefix):
            return line.split(":", 1)[1].strip()
    return ""
def read_diagnostics(url: str, ca_cert=None, clients_file=None, client_id=None) -> dict:
    if all((ca_cert, clients_file, client_id)):
        parsed = urllib.parse.urlsplit(url)
        payload = fetch_authenticated_json(
            f"{parsed.scheme}://{parsed.netloc}/",
            Path(ca_cert),
            Path(clients_file),
            str(client_id),
            ["diagnostics"],
            parsed.path,
        )
    else:
        with urllib.request.urlopen(url, timeout=1.0) as response:
            payload = json.load(response)
    if not isinstance(payload, dict):
        raise ReadyError("relay diagnostics is not an object")
    return payload
def validate_first_event(diagnostics: dict, width: int, height: int) -> dict:
    event = diagnostics.get("first_dirty_event")
    if not isinstance(event, dict):
        raise ReadyError("relay has not captured the formal first frame")
    expected_rect = (0, 0, width, height)
    actual_rect = tuple(int(event.get(key, -1)) for key in ("x", "y", "w", "h"))
    if actual_rect != expected_rect:
        raise ReadyError(f"first frame is not full main blit actual={actual_rect} expected={expected_rect}")
    frame_id = int(event.get("frame_id") or 0)
    base_frame_id = int(event.get("base_frame_id") or -1)
    if frame_id <= 1 or base_frame_id != 1:
        raise ReadyError(f"first frame identity invalid event={event!r}")
    return dict(event)
def atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temp_name, 0o600)
        os.replace(temp_name, path)
    finally:
        try:
            os.unlink(temp_name)
        except FileNotFoundError:
            pass
def build_failure_payload(owner_pid, owner_start_ticks, owner_role,
                          startup_instance_id, stage, error):
    try:
        owner_pid = int(owner_pid)
        owner_start_ticks = int(owner_start_ticks)
    except (TypeError, ValueError) as exc:
        raise ReadyError("failure owner identity malformed") from exc
    if owner_pid <= 0 or owner_start_ticks <= 0:
        raise ReadyError("failure owner identity invalid")
    try:
        actual_start_ticks = int(proc_start_ticks(owner_pid))
    except (OSError, ValueError, IndexError) as exc:
        raise ReadyError(f"failure owner unavailable pid={owner_pid}") from exc
    if actual_start_ticks != owner_start_ticks:
        raise ReadyError(f"failure owner PID/start mismatch pid={owner_pid}")
    try:
        startup_instance_id = str(uuid.UUID(str(startup_instance_id))).lower()
    except (ValueError, AttributeError) as exc:
        raise ReadyError("failure startup instance identity invalid") from exc
    if owner_role not in ("relay", "supervisor"):
        raise ReadyError(f"failure owner role invalid role={owner_role!r}")
    stage = str(stage or "").strip()
    error = str(error or "").strip()
    if not re.fullmatch(r"[a-z0-9_]{1,64}", stage):
        raise ReadyError(f"failure stage invalid stage={stage!r}")
    if not error:
        raise ReadyError("failure error text invalid")
    error = error[:512]
    return {
        "schema": FAILURE_SCHEMA,
        "ready": False,
        "failed": True,
        "boot_id": read_kernel_boot_id(),
        "startup_instance_id": startup_instance_id,
        "owner_role": owner_role,
        "owner_pid": owner_pid,
        "owner_start_ticks": owner_start_ticks,
        "stage": stage,
        "error": error,
        "created_realtime_ns": time.time_ns(),
        "created_monotonic_ns": time.monotonic_ns(),
    }
def publish_failure(path, owner_pid, owner_start_ticks, owner_role,
                    startup_instance_id, stage, error):
    payload = build_failure_payload(
        owner_pid,
        owner_start_ticks,
        owner_role,
        startup_instance_id,
        stage,
        error,
    )
    atomic_write_json(path, payload)
    return payload
def require_backend_motion_ready(probe: Path) -> dict:
    try:
        completed = subprocess.run(
            [str(probe), "--require", "motion"],
            text=True,
            capture_output=True,
            timeout=1.0,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise ReadyError(f"backend readiness query failed: {exc}") from exc
    if completed.returncode != 0:
        detail = (completed.stdout or completed.stderr).strip()
        raise ReadyError(
            f"motion backend readiness unavailable rc={completed.returncode} detail={detail}"
        )
    fields = {}
    for item in completed.stdout.split():
        if "=" in item:
            key, value = item.split("=", 1)
            fields[key] = value
    try:
        result = {
            "generation": int(fields["generation"]),
            "owner_pid": int(fields["owner_pid"]),
            "owner_start_ticks": int(fields["owner_start_ticks"]),
            "backend_ready_published_ns": int(fields["backend_ready_published"]),
        }
        motion_ready = int(fields["motion_ready"])
        revoked = int(fields["revoked"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ReadyError("backend readiness response malformed") from exc
    if (not all(result.values()) or motion_ready != 1 or revoked != 0):
        raise ReadyError(f"motion backend readiness response invalid fields={fields!r}")
    return result
def build_ready_payload(
    ui_pid: int,
    ui_log: Path,
    diagnostics: dict,
    width: int,
    height: int,
    required_cpu_list: str,
) -> dict:
    try:
        text = ui_log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise ReadyError(f"cannot read UI boot log: {exc}") from exc
    try:
        validate_main_cache_trace(text.splitlines())
    except ValueError as exc:
        raise ReadyError(f"Main cache evidence invalid: {exc}") from exc
    main_cache = parse_main_cache(text)
    validate_main_cache(main_cache)
    if "v5 UI remote framebuffer IPC ready:" not in text:
        raise ReadyError("UI process has not crossed the post-cache IPC-ready boundary")
    cpus_allowed = read_status_field(ui_pid, "Cpus_allowed_list")
    if cpus_allowed != required_cpu_list:
        raise ReadyError(
            f"UI pre-render affinity invalid pid={ui_pid} Cpus_allowed_list={cpus_allowed!r} "
            f"expected={required_cpu_list!r}"
        )
    first_event = validate_first_event(diagnostics, width, height)
    current_frame_id = int(diagnostics.get("frame_id") or 0)
    if current_frame_id < int(first_event["frame_id"]):
        raise ReadyError(
            f"relay frame identity regressed first={first_event['frame_id']} current={current_frame_id}"
        )
    if diagnostics.get("ui_ready"):
        raise ReadyError("stale ready metadata was visible before this boot completed")
    ui_start_ticks = int(proc_start_ticks(ui_pid))
    if ui_start_ticks <= 0:
        raise ReadyError(f"UI process start ticks invalid pid={ui_pid} value={ui_start_ticks}")
    return {
        "schema": "v5.ui_ready.v1",
        "ready": True,
        "boot_id": read_kernel_boot_id(),
        "ui_instance_id": str(uuid.uuid4()),
        "ui_pid": ui_pid,
        "ui_start_ticks": ui_start_ticks,
        "cpus_allowed_list": cpus_allowed,
        "width": width,
        "height": height,
        "cache_policy": BOOT_CACHE_POLICY,
        "cache_page_count": 1,
        "cache_registered_page_count": REGISTERED_PAGE_COUNT,
        "cache_slot_count": 13,
        "cache_budget_bytes": EXPECTED_CACHE_BUDGET_BYTES,
        "main_cache": main_cache,
        "cache_peak_cpu_pct_x100": main_cache["cpu_pct_x100"],
        "first_frame": first_event,
        "current_frame_id": current_frame_id,
        "created_realtime_ns": time.time_ns(),
        "created_monotonic_ns": time.monotonic_ns(),
    }
def wait_and_publish(args: argparse.Namespace) -> dict:
    deadline = time.monotonic() + args.timeout
    last_error = "not_checked"
    while time.monotonic() < deadline:
        if not Path(f"/proc/{args.ui_pid}/status").exists():
            raise ReadyError(f"UI process exited before ready pid={args.ui_pid}")
        try:
            diagnostics = read_diagnostics(
                args.diagnostics_url,
                args.diagnostics_ca_cert,
                args.diagnostics_clients_file,
                args.diagnostics_client_id,
            )
            payload = build_ready_payload(
                args.ui_pid,
                args.ui_log,
                diagnostics,
                args.width,
                args.height,
                args.require_cpu_list,
            )
            publisher_barrier = validate_final_publisher_barrier(
                args.publisher_snapshot_path,
                args.expected_ini,
            )
            backend_ready = require_backend_motion_ready(args.backend_readiness_probe)
            if int(proc_start_ticks(args.ui_pid)) != int(payload["ui_start_ticks"]):
                raise ReadyError(f"UI process identity changed before ready commit pid={args.ui_pid}")
            payload["publisher_barrier"] = publisher_barrier
            payload["backend_ready"] = backend_ready
            atomic_write_json(args.ready_path, payload)
            return payload
        except FinalBarrierError:
            raise
        except (OSError, ReadyError, urllib.error.URLError) as exc:
            last_error = str(exc)
            time.sleep(0.05)
    main_cache = None
    try:
        main_cache = parse_main_cache(args.ui_log.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        pass
    failure_stage = "main_cache" if main_cache is None else "post_main_cache_ready"
    raise ReadyError(
        f"UI ready timeout seconds={args.timeout:.1f} failure_stage={failure_stage} "
        f"last_error={last_error}"
    )
def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the v5 boot Main cache and publish ui_ready metadata."); parser.add_argument("--pre-ui-inputs", action="store_true"); parser.add_argument("--publish-failure", action="store_true"); parser.add_argument("--expected-ini")
    parser.add_argument("--ui-pid", type=int); parser.add_argument("--ui-log", type=Path)
    parser.add_argument("--ready-path", type=Path); parser.add_argument("--diagnostics-url")
    parser.add_argument("--diagnostics-ca-cert", type=Path)
    parser.add_argument("--diagnostics-clients-file", type=Path)
    parser.add_argument("--diagnostics-client-id", default="local-bootstrap")
    parser.add_argument("--publisher-snapshot-path", type=Path)
    parser.add_argument("--backend-readiness-probe", type=Path)
    parser.add_argument("--startup-instance-id"); parser.add_argument("--failure-owner-pid", type=int)
    parser.add_argument("--failure-owner-start-ticks", type=int); parser.add_argument("--failure-owner-role")
    parser.add_argument("--failure-stage"); parser.add_argument("--failure-error")
    parser.add_argument("--timeout", type=float, default=30.0); parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=600); parser.add_argument("--require-cpu-list", default="1"); args = parser.parse_args()
    try:
        if args.publish_failure:
            if not all((args.ready_path, args.startup_instance_id,
                        args.failure_owner_pid, args.failure_owner_start_ticks,
                        args.failure_owner_role, args.failure_stage, args.failure_error)):
                raise ReadyError("failure mode requires ready path, startup and owner identity, stage and error")
            failure = publish_failure(
                args.ready_path,
                args.failure_owner_pid,
                args.failure_owner_start_ticks,
                args.failure_owner_role,
                args.startup_instance_id,
                args.failure_stage,
                args.failure_error,
            )
            print("v5_ui_boot_failure PASS " + json.dumps(failure, sort_keys=True, separators=(",", ":")))
            return 0
        if args.pre_ui_inputs:
            if args.expected_ini != BUS_INI: raise ReadyError("pre-UI inputs require the canonical BUS INI")
            result = wait_pre_ui_inputs(args.timeout, expected_ini=args.expected_ini)
            if args.publisher_snapshot_path is None:
                raise ReadyError("pre-UI inputs require publisher snapshot path")
            atomic_write_json(
                args.publisher_snapshot_path,
                build_input_barrier_snapshot(result, args.expected_ini),
            )
            print("v5_ui_boot_inputs PASS " + json.dumps(result, sort_keys=True, separators=(",", ":")))
            return 0
        if not all((args.ui_pid, args.ui_log, args.ready_path, args.diagnostics_url,
                    args.diagnostics_ca_cert, args.diagnostics_clients_file,
                    args.diagnostics_client_id,
                    args.publisher_snapshot_path, args.expected_ini,
                    args.backend_readiness_probe)):
            raise ReadyError("UI ready mode requires UI, diagnostics and publisher snapshot identity")
        payload = wait_and_publish(args)
    except ReadyError as exc:
        if all((args.ready_path, args.startup_instance_id,
                args.failure_owner_pid, args.failure_owner_start_ticks,
                args.failure_owner_role, args.failure_stage)):
            try:
                publish_failure(
                    args.ready_path,
                    args.failure_owner_pid,
                    args.failure_owner_start_ticks,
                    args.failure_owner_role,
                    args.startup_instance_id,
                    args.failure_stage,
                    str(exc),
                )
            except ReadyError as failure_exc:
                print(f"v5_ui_boot_failure FAIL {failure_exc}", file=os.sys.stderr)
        print(f"v5_ui_boot_ready FAIL {exc}", file=os.sys.stderr)
        return 1
    print(
        "v5_ui_boot_ready PASS "
        f"boot_id={payload['boot_id']} ui_instance_id={payload['ui_instance_id']} "
        f"ui_pid={payload['ui_pid']} ui_start_ticks={payload['ui_start_ticks']} "
        f"frame_id={payload['current_frame_id']} cache_budget_bytes={payload['cache_budget_bytes']}",
        flush=True,
    )
    return 0
if __name__ == "__main__": raise SystemExit(main())
