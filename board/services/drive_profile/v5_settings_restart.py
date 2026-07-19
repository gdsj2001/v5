from __future__ import annotations

import subprocess
import time
from pathlib import Path
from typing import Any, Dict

from v5_settings_action_contract import CANONICAL_CLEAN_RESTART_SERVICES, RUN_DIR


RESTART_HANDOFF_DELAY_S = 1.0
RESTART_COMMIT_POLL_S = 0.1
RESTART_COMMIT_TIMEOUT_POLLS = 100


def restart_handoff_paths() -> tuple[Path, Path, Path, Path]:
    return (
        RUN_DIR / "settings_clean_restart_handoff.sh",
        RUN_DIR / "settings_clean_restart_handoff.log",
        RUN_DIR / "settings_clean_restart_commit.armed",
        RUN_DIR / "settings_clean_restart_commit.go",
    )


def remove_runtime_marker(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def run_restart_handoff(action: str, spec: Dict[str, Any]) -> Dict[str, Any]:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    handoff_script, handoff_log, armed_marker, go_marker = restart_handoff_paths()
    remove_runtime_marker(armed_marker)
    remove_runtime_marker(go_marker)
    service_list = " ".join(CANONICAL_CLEAN_RESTART_SERVICES)
    handoff_script.write_text("""#!/bin/sh
set -u
LOG="%s"
ARMED="%s"
GO="%s"
exec >>"$LOG" 2>&1
echo "clean_restart_handoff armed $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
polls=0
while [ ! -f "$GO" ]; do
  if [ "$polls" -ge %d ]; then
    echo "clean_restart_handoff commit_timeout $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
    rm -f "$ARMED"
    exit 111
  fi
  sleep %.1f
  polls=$((polls + 1))
done
rm -f "$GO" "$ARMED"
echo "clean_restart_handoff acknowledged $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
sleep %.1f
sync || true
for svc in %s; do
  if [ -x "/etc/init.d/$svc" ]; then
    echo "stop $svc"
    "/etc/init.d/$svc" stop || true
  fi
done
for pidfile in /run/8ax/*.pid; do
  [ -f "$pidfile" ] || continue
  pid=$(cat "$pidfile" 2>/dev/null || true)
  case "$pid" in
    ''|*[!0-9]*) continue ;;
  esac
  kill "$pid" 2>/dev/null || true
done
sleep 0.3
for pidfile in /run/8ax/*.pid; do
  [ -f "$pidfile" ] || continue
  pid=$(cat "$pidfile" 2>/dev/null || true)
  case "$pid" in
    ''|*[!0-9]*) continue ;;
  esac
  kill -KILL "$pid" 2>/dev/null || true
done
rm -f /run/8ax/*.pid
rm -f /run/8ax_v5_product_ui/*.sock
rm -f /dev/shm/v3_status_shm
rm -f /dev/shm/v5_native_*.bin
rm -f /run/8ax_v5_drive/drive_profile_resident_snapshot.json
rm -f /run/8ax_v5_product_ui/settings_actiond_events.jsonl
rm -f /run/8ax_v5_product_ui/touch_events.jsonl
sync || true
echo "clean_restart_handoff reboot $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
if command -v reboot >/dev/null 2>&1; then
  reboot -f
elif [ -x /sbin/reboot ]; then
  /sbin/reboot -f
else
  echo b >/proc/sysrq-trigger
fi
""" % (
        str(handoff_log),
        str(armed_marker),
        str(go_marker),
        RESTART_COMMIT_TIMEOUT_POLLS,
        RESTART_COMMIT_POLL_S,
        RESTART_HANDOFF_DELAY_S,
        service_list,
    ), encoding="utf-8")
    handoff_script.chmod(0o755)
    return {
        "schema": "v5.settings_action_result.v1",
        "generated_at": now_utc(),
        "action": action,
        "owner": spec.get("owner", ""),
        "ok": True,
        "code": "SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED",
        "message_cn": "系统级重启已准备，等待关闭结果窗后提交。",
        "display_message_cn": "系统级重启已准备，点击关闭后黑屏并重启。",
        "write_executed": False,
        "motion_executed": False,
        "restart_executed": False,
        "restart_commit_required": True,
        "clean_restart_equivalent": "board_reboot_after_ui_ack",
        "handoff_script": str(handoff_script),
        "handoff_log": str(handoff_log),
        "stop_order": CANONICAL_CLEAN_RESTART_SERVICES,
    }


def commit_restart_handoff() -> Dict[str, Any]:
    handoff_script, _handoff_log, armed_marker, go_marker = restart_handoff_paths()
    if not handoff_script.is_file():
        return {
            "ok": False,
            "accepted": False,
            "code": "SETTINGS_SAVE_RESTART_HANDOFF_NOT_PREPARED",
            "detail": str(handoff_script),
        }
    remove_runtime_marker(go_marker)
    try:
        armed_marker.write_text("armed\n", encoding="utf-8")
    except Exception as exc:
        return {
            "ok": False,
            "accepted": False,
            "code": "SETTINGS_SAVE_RESTART_COMMIT_ARM_FAILED",
            "detail": "%s: %s" % (type(exc).__name__, exc),
        }
    try:
        subprocess.Popen(
            ["/bin/sh", str(handoff_script)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except Exception as exc:
        remove_runtime_marker(armed_marker)
        return {
            "ok": False,
            "accepted": False,
            "code": "SETTINGS_SAVE_RESTART_COMMIT_SPAWN_FAILED",
            "detail": "%s: %s" % (type(exc).__name__, exc),
        }
    return {
        "ok": True,
        "accepted": True,
        "code": "SETTINGS_SAVE_RESTART_COMMIT_ACK",
        "handoff_script": str(handoff_script),
        "handoff_delay_s": RESTART_HANDOFF_DELAY_S,
        "commit_armed": True,
    }


def release_restart_handoff() -> Dict[str, Any]:
    _handoff_script, _handoff_log, armed_marker, go_marker = restart_handoff_paths()
    if not armed_marker.is_file():
        return {
            "ok": False,
            "code": "SETTINGS_SAVE_RESTART_COMMIT_NOT_ARMED",
        }
    try:
        armed_marker.replace(go_marker)
    except Exception as exc:
        return {
            "ok": False,
            "code": "SETTINGS_SAVE_RESTART_COMMIT_RELEASE_FAILED",
            "detail": "%s: %s" % (type(exc).__name__, exc),
        }
    return {
        "ok": True,
        "code": "SETTINGS_SAVE_RESTART_COMMIT_RELEASED",
    }


def abort_restart_handoff() -> None:
    _handoff_script, _handoff_log, armed_marker, go_marker = restart_handoff_paths()
    remove_runtime_marker(armed_marker)
    remove_runtime_marker(go_marker)
