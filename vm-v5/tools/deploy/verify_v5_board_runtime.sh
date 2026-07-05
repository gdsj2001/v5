#!/bin/sh
set -eu

board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
state_path="${V5_STATUS_SHM_PATH:-/dev/shm/v3_status_shm}"
linuxcncrsh_port="${V5_LINUXCNCRSH_PORT:-5007}"
fail=0
warn=0

say() { printf '%s\n' "$*"; }
ok() { say "OK $*"; }
warn_msg() { say "WARN $*"; warn=1; }
fail_msg() { say "FAIL $*"; fail=1; }

if [ -z "$board_ssh" ]; then
  fail_msg "V5_BOARD_SSH is required"
  exit 2
fi

remote() {
  ssh -o BatchMode=yes -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "$@"
}

if ! remote 'true' >/dev/null 2>&1; then
  fail_msg "cannot connect to board via ssh: $board_ssh port=$board_ssh_port"
  exit 1
fi
ok "ssh reachable: $board_ssh port=$board_ssh_port"

check_remote_test() {
  label="$1"
  command="$2"
  if remote "$command" >/dev/null 2>&1; then
    ok "$label"
  else
    fail_msg "$label"
  fi
}
check_remote_test "v5_lvgl_shell installed executable" 'test -x /usr/libexec/8ax/v5_lvgl_shell'
check_remote_test "v5_state_publisher installed executable" 'test -x /usr/libexec/8ax/v5_state_publisher'
check_remote_test "v5_linuxcncrsh_probe installed executable" 'test -x /usr/libexec/8ax/v5_linuxcncrsh_probe'
check_remote_test "v5_linuxcncrsh_golden_run installed executable" 'test -x /usr/libexec/8ax/v5_linuxcncrsh_golden_run'
check_remote_test "state publisher init installed" 'test -x /etc/init.d/v5-state-publisher'
check_remote_test "linuxcnc command gate init installed" 'test -x /etc/init.d/v5-linuxcnc-command-gate'
check_remote_test "v5 linuxcnc ini installed" 'test -r /opt/8ax/v5/linuxcnc/ini/v5_pulse.ini'
check_remote_test "v5 linuxcnc hal installed" 'test -r /opt/8ax/v5/linuxcnc/hal/v5_pulse.hal'
check_remote_test "v5 deploy config installed" 'test -r /opt/8ax/v5/config/hardware_profile.json'
check_remote_test "v5 auth dna register installed executable" 'test -x /usr/libexec/8ax/auth_download/v5_device_dna_register.py'
check_remote_test "v5 auth authorization download installed executable" 'test -x /usr/libexec/8ax/auth_download/v5_device_authorization_download.py'
check_remote_test "v5 drive profile download installed executable" 'test -x /usr/libexec/8ax/auth_download/v5_drive_profile_download.py'
check_remote_test "v5 auth support modules installed" 'test -r /usr/libexec/8ax/auth_download/drive_profile_download_flow.py && test -r /usr/libexec/8ax/auth_download/drive_profile_download_transport.py && test -r /usr/libexec/8ax/auth_download/device_vps_identity.py'

if remote '/etc/init.d/v5-state-publisher status' >/tmp/v5_verify_init_status.out 2>&1; then
  ok "v5-state-publisher init status running"
  sed 's/^/INFO state publisher init: /' /tmp/v5_verify_init_status.out
else
  fail_msg "v5-state-publisher init status running"
  sed 's/^/INFO state publisher init: /' /tmp/v5_verify_init_status.out
fi
rm -f /tmp/v5_verify_init_status.out

if remote "/usr/libexec/8ax/v5_state_publisher --path /dev/shm/v5_verify_state_publisher --once >/tmp/v5_verify_state_publisher.out 2>&1 && test -s /dev/shm/v5_verify_state_publisher && rm -f /dev/shm/v5_verify_state_publisher" >/dev/null 2>&1; then
  ok "state publisher one-shot writes shm"
  remote 'cat /tmp/v5_verify_state_publisher.out 2>/dev/null || true' | sed 's/^/INFO state publisher: /'
else
  fail_msg "state publisher one-shot writes shm"
fi

if remote "test -s '$state_path'" >/dev/null 2>&1; then
  ok "runtime shm exists: $state_path"
  remote "stat -c 'shm_size=%s shm_mode=%a' '$state_path'" | sed 's/^/INFO /'
else
  fail_msg "runtime shm exists: $state_path"
fi

if remote '/usr/libexec/8ax/v5_lvgl_shell >/tmp/v5_lvgl_shell_verify.out 2>&1' >/dev/null 2>&1; then
  ok "v5_lvgl_shell starts"
  remote 'tail -n 3 /tmp/v5_lvgl_shell_verify.out 2>/dev/null || true' | sed 's/^/INFO ui shell: /'
else
  fail_msg "v5_lvgl_shell starts"
  remote 'tail -n 20 /tmp/v5_lvgl_shell_verify.out 2>/dev/null || true' | sed 's/^/INFO ui shell: /'
fi

if remote "/etc/init.d/v5-linuxcnc-command-gate status && /usr/libexec/8ax/v5_linuxcncrsh_probe --host 127.0.0.1 --port '$linuxcncrsh_port' --password EMC --timeout-ms 1000 >/tmp/v5_linuxcncrsh_probe.out 2>&1" >/dev/null 2>&1; then
  ok "linuxcncrsh machine probe ok: $linuxcncrsh_port"
  remote 'tail -n 5 /tmp/v5_linuxcncrsh_probe.out 2>/dev/null || true' | sed 's/^/INFO linuxcncrsh: /'
else
  fail_msg "linuxcncrsh machine probe ok: $linuxcncrsh_port"
  remote 'tail -n 10 /tmp/v5_linuxcncrsh_probe.out 2>/dev/null || true' | sed 's/^/INFO linuxcncrsh: /'
fi

if remote 'test -r /dev/fb0 && test -r /sys/class/graphics/fb0/modes && test -r /sys/class/graphics/fb0/bits_per_pixel && test -r /sys/class/graphics/fb0/stride' >/dev/null 2>&1; then
  ok "framebuffer capture inputs available"
else
  fail_msg "framebuffer capture inputs available"
fi

if remote 'test -d /dev/input && ls /dev/input/event* >/dev/null 2>&1' >/dev/null 2>&1; then
  ok "input event devices visible"
else
  warn_msg "input event devices not visible; touch evidence still missing"
fi

remote 'ps w 2>/dev/null | grep -E "v5_state_publisher|v5_lvgl_shell|linuxcncrsh|linuxcncsvr|milltask" | grep -v grep || true' |
  sed 's/^/INFO process: /'

if [ "$fail" -ne 0 ]; then
  exit 1
fi
if [ "$warn" -ne 0 ]; then
  say "verify complete with warnings"
else
  say "verify complete"
fi
