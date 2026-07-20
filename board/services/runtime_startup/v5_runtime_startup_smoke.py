#!/usr/bin/env python3
from pathlib import Path


BOARD = Path(__file__).resolve().parents[2]
ORCHESTRATOR = BOARD / "services/runtime_startup/init.d/v5-runtime-startup"
UI_INIT = BOARD / "services/ui/init.d/v5-ui-relay"
UI_READY = BOARD / "services/ui/v5_ui_boot_ready.py"
ACTIOND_INIT = BOARD / "services/drive_profile/init.d/v5-settings-actiond"
MANIFEST = BOARD / "config/deploy/v5_runtime_deploy_manifest.tsv"
SD_WRITER = BOARD / "tools/petalinux/write_v5_sd_card.sh"
INSTALLER = BOARD / "tools/deploy/install_v5_runtime.sh"


def section(text: str, begin: str, end: str) -> str:
    start = text.index(begin)
    stop = text.index(end, start)
    return text[start:stop]


def main() -> int:
    orchestrator = ORCHESTRATOR.read_text(encoding="utf-8")
    ui_init = UI_INIT.read_text(encoding="utf-8")
    ui_ready = UI_READY.read_text(encoding="utf-8")
    actiond = ACTIOND_INIT.read_text(encoding="utf-8")
    manifest = MANIFEST.read_text(encoding="utf-8")
    sd_writer = SD_WRITER.read_text(encoding="utf-8")
    installer = INSTALLER.read_text(encoding="utf-8")

    for token in (
        '"$COMMAND_GATE" start >>"$LOGFILE" 2>&1 &',
        '"$ACTIOND" start >>"$LOGFILE" 2>&1 &',
        'start_publishers_after_data_ready >>"$LOGFILE" 2>&1 &',
        '"$UI" start >>"$LOGFILE" 2>&1 &',
        "rollback_start",
        "status_runtime",
    ):
        assert token in orchestrator, token
    assert orchestrator.index('"$COMMAND_GATE" start') < orchestrator.index(
        'wait "$actiond_job"'
    )
    assert orchestrator.index('"$ACTIOND" start') < orchestrator.index(
        'wait "$actiond_job"'
    )
    assert orchestrator.index('"$UI" start') < orchestrator.index(
        'wait "$actiond_job"'
    )
    publishers = section(
        orchestrator,
        "start_publishers_after_data_ready() {",
        "\nstatus_runtime() {",
    )
    assert '"$BACKEND_READINESS_PROBE" --wait data --timeout-ms 120000' in publishers
    for service in ("POSITION", "WCS", "STATE"):
        assert f'"${service}" start &' in publishers
    assert publishers.index('"$STATE" start &') < publishers.index(
        'wait "$position_job"'
    )

    wait_inputs = section(ui_init, "wait_boot_inputs_ready() {", "\nstale_service_pids() {")
    assert '"$BACKEND_READINESS_PROBE" --wait data --timeout-ms 120000' in wait_inputs
    assert "/proc/[0-9]*/cmdline" not in wait_inputs
    assert "PROFILE_SNAPSHOT" not in ui_init
    assert "build_profile_snapshot" not in ui_init
    relay_spawn = ui_init.index('setsid taskset -c 1 nice -n "$UI_INPUT_NICE" "$RELAY_DAEMON"')
    ui_spawn = ui_init.index('setsid taskset -c 1 nice -n "$UI_INPUT_NICE" "$UI_DAEMON"')
    input_barrier = ui_init.index("if ! wait_boot_inputs_ready; then")
    final_barrier = ui_init.index('if ! "$BOOT_READY"')
    assert relay_spawn < input_barrier < ui_spawn < final_barrier
    assert '--backend-readiness-probe "$BACKEND_READINESS_PROBE"' in ui_init
    assert '"backend_ready":{' in ui_init

    assert actiond.count("v5_drive_profile_resident_snapshot.py") == 1
    assert 'rm -f "$PROFILE_SNAPSHOT"' in actiond
    snapshot_block = section(actiond, "start_service() {", "\nstop_service() {")
    assert (
        '"$PROFILE_SNAPSHOT_BUILDER" --profile-root /opt/8ax/drive-profiles \\\n'
        '    --out "$PROFILE_SNAPSHOT" >/run/8ax/v5_drive_profile_resident_snapshot.log 2>&1\n'
    ) in snapshot_block
    assert "def require_backend_motion_ready" in ui_ready
    final_ready = section(ui_ready, "def wait_and_publish", "\ndef main()")
    assert final_ready.index("validate_final_publisher_barrier") < final_ready.index(
        "require_backend_motion_ready"
    ) < final_ready.index("atomic_write_json(args.ready_path, payload)")
    assert 'payload["backend_ready"] = backend_ready' in final_ready

    assert (
        "init\tservices/runtime_startup/init.d/v5-runtime-startup\t"
        "/etc/init.d/v5-runtime-startup\t0755"
    ) in manifest
    for retired in (
        "enable_service v5-linuxcnc-command-gate",
        "enable_service v5-position-status-publisher",
        "enable_service v5-wcs-status-publisher",
        "enable_service v5-state-publisher",
        "enable_service v5-ui-relay",
        "enable_service v5-settings-actiond",
    ):
        assert retired not in sd_writer, retired
    assert "enable_service v5-runtime-startup 05 14" in sd_writer
    assert "enable_runtime_startup_boot_graph()" in installer
    assert "disable_boot_service \"$service\"" in installer
    assert "enable_auxiliary_boot_service v5-runtime-startup 05 14" in installer
    for retired in (
        "enable_boot_service()",
        "enable_boot_service_raw()",
        "enable_boot_service v5-linuxcnc-command-gate",
        "enable_boot_service v5-position-status-publisher",
        "enable_boot_service v5-wcs-status-publisher",
        "enable_boot_service v5-state-publisher",
        "enable_boot_service v5-ui-relay",
        "enable_boot_service v5-settings-actiond",
    ):
        assert retired not in installer, retired

    print("V5_RUNTIME_STARTUP_DAG_SMOKE_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
