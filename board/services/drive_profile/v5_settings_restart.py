from __future__ import annotations

import subprocess
import time
from pathlib import Path
from typing import Any, Dict

from v5_settings_action_contract import CANONICAL_CLEAN_RESTART_SERVICES, RUN_DIR

def service_status(name: str, timeout_s: float = 4.0) -> Dict[str, Any]:
    script = Path('/etc/init.d') / name
    if not script.exists():
        return {"ok": False, "code": "SERVICE_SCRIPT_MISSING", "script": str(script)}
    try:
        proc = subprocess.run([str(script), 'status'], text=True, capture_output=True, timeout=timeout_s, check=False)
    except Exception as exc:
        return {"ok": False, "code": "SERVICE_STATUS_EXCEPTION", "detail": "%s: %s" % (type(exc).__name__, exc), "script": str(script)}
    return {
        "ok": proc.returncode == 0,
        "code": "SERVICE_RUNNING" if proc.returncode == 0 else "SERVICE_NOT_RUNNING",
        "returncode": proc.returncode,
        "stdout": proc.stdout[-1000:],
        "stderr": proc.stderr[-1000:],
        "script": str(script),
    }


def restart_service(name: str, timeout_s: float = 8.0) -> Dict[str, Any]:
    script = Path('/etc/init.d') / name
    if not script.exists():
        return {"service": name, "ok": False, "code": "SERVICE_SCRIPT_MISSING", "script": str(script)}
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    safe_name = ''.join(ch if ch.isalnum() or ch in ('-', '_') else '_' for ch in name)
    log_path = RUN_DIR / ("settings_restart_%s.log" % safe_name)
    try:
        with log_path.open('w', encoding='utf-8') as fp:
            proc = subprocess.run([str(script), 'restart'], text=True, stdout=fp, stderr=subprocess.STDOUT, timeout=timeout_s, check=False)
    except subprocess.TimeoutExpired as exc:
        status = service_status(name)
        detail = str(exc)
        try:
            output = log_path.read_text(encoding='utf-8', errors='replace')[-2000:]
        except Exception:
            output = ''
        return {"service": name, "ok": False, "code": "SERVICE_RESTART_TIMEOUT", "script": str(script), "timeout_s": timeout_s, "detail": detail, "stdout": output, "log_path": str(log_path), "status": status}
    except Exception as exc:
        status = service_status(name)
        return {"service": name, "ok": False, "code": "SERVICE_RESTART_EXCEPTION", "script": str(script), "detail": "%s: %s" % (type(exc).__name__, exc), "log_path": str(log_path), "status": status}
    try:
        output = log_path.read_text(encoding='utf-8', errors='replace')[-2000:]
    except Exception:
        output = ''
    status = service_status(name)
    ok = proc.returncode == 0 and bool(status.get('ok'))
    return {
        "service": name,
        "ok": ok,
        "code": "SERVICE_RESTARTED" if ok else "SERVICE_RESTART_FAILED",
        "returncode": proc.returncode,
        "stdout": output,
        "stderr": "",
        "script": str(script),
        "timeout_s": timeout_s,
        "log_path": str(log_path),
        "status": status,
    }


def run_restart_handoff(action: str, spec: Dict[str, Any]) -> Dict[str, Any]:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    handoff_script = RUN_DIR / "settings_clean_restart_handoff.sh"
    handoff_log = RUN_DIR / "settings_clean_restart_handoff.log"
    service_list = " ".join(CANONICAL_CLEAN_RESTART_SERVICES)
    handoff_script.write_text("""#!/bin/sh
set -u
LOG="%s"
exec >>"$LOG" 2>&1
echo "clean_restart_handoff begin $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
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
""" % (str(handoff_log), service_list), encoding="utf-8")
    handoff_script.chmod(0o755)
    try:
        # This is the cancellable pre-commit window. After Popen succeeds the
        # detached handoff owns the reboot and the action becomes terminal.
        time.sleep(1.0)
        subprocess.Popen(
            ["/bin/sh", str(handoff_script)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        ok = True
        code = "SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED"
    except Exception as exc:
        ok = False
        code = "SETTINGS_SAVE_RESTART_HANDOFF_SPAWN_FAILED"
        append_event({"event": "clean_restart_spawn_failed", "action": action, "ok": False, "detail": "%s: %s" % (type(exc).__name__, exc)})
    return {
        "schema": "v5.settings_action_result.v1",
        "generated_at": now_utc(),
        "action": action,
        "owner": spec.get("owner", ""),
        "ok": ok,
        "code": code,
        "message_cn": "保存并重启已进入开发板干净重启流程。" if ok else "保存并重启 handoff 启动失败。",
        "display_message_cn": "保存并重启已进入开发板干净重启流程。" if ok else "保存并重启 handoff 启动失败。",
        "write_executed": False,
        "motion_executed": False,
        "restart_executed": True,
        "clean_restart_equivalent": "board_reboot",
        "handoff_script": str(handoff_script),
        "handoff_log": str(handoff_log),
        "stop_order": CANONICAL_CLEAN_RESTART_SERVICES,
    }
