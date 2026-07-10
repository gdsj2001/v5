#!/bin/sh
set -eu

board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
ssh_bin="${V5_SSH:-ssh}"
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
  "$ssh_bin" -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "$@"
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
check_remote_test "v5_rtcp_status_publisher installed executable" 'test -x /usr/libexec/8ax/v5_rtcp_status_publisher.py'
check_remote_test "v5_g53_geometry_memory_owner installed executable" 'test -x /usr/libexec/8ax/v5_g53_geometry_memory_owner.py'
check_remote_test "v5_wcs_status_publisher installed executable" 'test -x /usr/libexec/8ax/v5_wcs_status_publisher.py'
check_remote_test "native operator error mapper installed" 'test -r /usr/libexec/8ax/v5_native_operator_error_map.py'
check_remote_test "native operator error complete map installed" 'test -r /opt/8ax/v5/config/ui/v5_native_operator_error_map.tsv && test "$(grep -vc "^#" /opt/8ax/v5/config/ui/v5_native_operator_error_map.tsv)" -eq 556'
check_remote_test "v5_touch_diagnostics installed executable" 'test -x /usr/libexec/8ax/v5_touch_diagnostics'
check_remote_test "v5_linuxcncrsh_probe installed executable" 'test -x /usr/libexec/8ax/v5_linuxcncrsh_probe'
check_remote_test "v5_command_gate_server installed executable" 'test -x /usr/libexec/8ax/v5_command_gate_server'
check_remote_test "v5_linuxcncrsh_golden_run installed executable" 'test -x /usr/libexec/8ax/v5_linuxcncrsh_golden_run'
check_remote_test "v5 remote ui relay installed executable" 'test -x /usr/libexec/8ax/v5_remote_ui_relay.py'
check_remote_test "state publisher init installed" 'test -x /etc/init.d/v5-state-publisher'
check_remote_test "rtcp status publisher init installed" 'test -x /etc/init.d/v5-rtcp-status-publisher'
check_remote_test "g53 geometry memory owner init installed" 'test -x /etc/init.d/v5-g53-geometry-memory-owner'
check_remote_test "wcs status publisher init installed" 'test -x /etc/init.d/v5-wcs-status-publisher'
check_remote_test "linuxcnc command gate init installed" 'test -x /etc/init.d/v5-linuxcnc-command-gate'
check_remote_test "v5 ui relay init installed" 'test -x /etc/init.d/v5-ui-relay'
check_remote_test "v5 touch diagnostics init installed" 'test -x /etc/init.d/v5-touch-diagnostics'
check_remote_test "v5 BUS linuxcnc ini installed" 'test -r /opt/8ax/v5/linuxcnc/ini/v5_bus.ini'
check_remote_test "v5 BUS linuxcnc hal installed" 'test -r /opt/8ax/v5/linuxcnc/hal/v5_bus_2ms.hal && test -r /opt/8ax/v5/linuxcnc/hal/ethercat-conf-2ms.xml'
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

if remote '/etc/init.d/v5-rtcp-status-publisher status' >/tmp/v5_rtcp_status_publisher_status.out 2>&1; then
  ok "v5-rtcp-status-publisher init status running"
  sed 's/^/INFO rtcp status publisher init: /' /tmp/v5_rtcp_status_publisher_status.out
else
  fail_msg "v5-rtcp-status-publisher init status running"
  sed 's/^/INFO rtcp status publisher init: /' /tmp/v5_rtcp_status_publisher_status.out
fi
rm -f /tmp/v5_rtcp_status_publisher_status.out

if remote 'PYTHONPATH=/usr/lib/python3/dist-packages /usr/libexec/8ax/v5_rtcp_status_publisher.py --once >/tmp/v5_rtcp_status_publisher_once.out 2>&1 && test -s /dev/shm/v5_native_rtcp_status.bin' >/dev/null 2>&1; then
  ok "rtcp status publisher one-shot samples LinuxCNC/HAL native switchkins actual"
  remote 'tail -n 3 /tmp/v5_rtcp_status_publisher_once.out 2>/dev/null || true' | sed 's/^/INFO rtcp status publisher: /'
else
  fail_msg "rtcp status publisher one-shot samples LinuxCNC/HAL native switchkins actual"
  remote 'tail -n 10 /tmp/v5_rtcp_status_publisher_once.out 2>/dev/null || true' | sed 's/^/INFO rtcp status publisher: /'
fi

check_remote_test "retired /run RTCP v3 snapshot absent" 'test ! -e /run/8ax_v5_product_ui/v3_native_rtcp_status.bin'

if remote '/etc/init.d/v5-g53-geometry-memory-owner status' >/tmp/v5_g53_geometry_memory_owner_status.out 2>&1; then
  ok "v5-g53-geometry-memory-owner init status running"
  sed 's/^/INFO g53 geometry memory owner init: /' /tmp/v5_g53_geometry_memory_owner_status.out
else
  fail_msg "v5-g53-geometry-memory-owner init status running"
  sed 's/^/INFO g53 geometry memory owner init: /' /tmp/v5_g53_geometry_memory_owner_status.out
fi
rm -f /tmp/v5_g53_geometry_memory_owner_status.out

g53_block_check='import configparser,math,struct,sys; ini="/opt/8ax/v5/linuxcnc/ini/v5_bus.ini"; cp=configparser.ConfigParser(); cp.optionxform=str; cp.read(ini, encoding="utf-8"); exp=[0.0,cp.getfloat("RTCP","G53_A_Y"),cp.getfloat("RTCP","G53_A_Z"),cp.getfloat("RTCP","G53_B_X"),0.0,cp.getfloat("RTCP","G53_B_Z"),cp.getfloat("RTCP","G53_C_X"),cp.getfloat("RTCP","G53_C_Y"),0.0]; model=cp.get("RTCP","MODEL",fallback=cp.get("RTCP","MOTION_MODEL",fallback="")).strip(); p="/dev/shm/v5_native_g53_geometry_status.bin"; fmt=struct.Struct("<IIIIIIIIQ"+("d"*9)+"32sII"); v=fmt.unpack(open(p,"rb").read(fmt.size)); got=list(v[9:18]); got_model=v[18].split(b"\0",1)[0].decode("utf-8","replace"); sys.exit(0 if v[1]==2 and v[3]==1 and v[4]==3 and v[5]==3 and v[6] and got_model==model and all(math.isfinite(x) for x in got) and all(abs(a-b)<1e-9 for a,b in zip(got,exp)) else 1)'
g53_block_info='import struct; p="/dev/shm/v5_native_g53_geometry_status.bin"; fmt=struct.Struct("<IIIIIIIIQ"+("d"*9)+"32sII"); v=fmt.unpack(open(p,"rb").read(fmt.size)); c=list(v[9:18]); model=v[18].split(b"\0",1)[0].decode("utf-8","replace"); print("g53_block version=%d valid=%d centers=%dx%d epoch=%d model=%s A=%.3f,%.3f,%.3f C=%.3f,%.3f,%.3f" % (v[1],v[3],v[4],v[5],v[6],model,c[0],c[1],c[2],c[6],c[7],c[8]))'
if remote "/usr/libexec/8ax/v5_g53_geometry_memory_owner.py --once --path /dev/shm/v5_native_g53_geometry_status.bin --ini /opt/8ax/v5/linuxcnc/ini/v5_bus.ini >/tmp/v5_g53_geometry_memory_owner_once.out 2>&1 && test -s /dev/shm/v5_native_g53_geometry_status.bin && /usr/bin/python3 -c '$g53_block_check'" >/dev/null 2>&1; then
  ok "g53 geometry memory owner one-shot loads RTCP G53 into native memory"
  remote "tail -n 3 /tmp/v5_g53_geometry_memory_owner_once.out 2>/dev/null || true; /usr/bin/python3 -c '$g53_block_info' 2>/dev/null || true" | sed 's/^/INFO g53 geometry memory owner: /'
else
  fail_msg "g53 geometry memory owner one-shot loads RTCP G53 into native memory"
  remote 'tail -n 10 /tmp/v5_g53_geometry_memory_owner_once.out 2>/dev/null || true; ls -l /dev/shm/v5_native_g53_geometry_status.bin 2>/dev/null || true' | sed 's/^/INFO g53 geometry memory owner: /'
fi

if remote '/etc/init.d/v5-wcs-status-publisher status' >/tmp/v5_wcs_status_publisher_status.out 2>&1; then
  ok "v5-wcs-status-publisher init status running"
  sed 's/^/INFO wcs status publisher init: /' /tmp/v5_wcs_status_publisher_status.out
else
  fail_msg "v5-wcs-status-publisher init status running"
  sed 's/^/INFO wcs status publisher init: /' /tmp/v5_wcs_status_publisher_status.out
fi
rm -f /tmp/v5_wcs_status_publisher_status.out

wcs_block_check='import struct,sys; p="/dev/shm/v5_native_wcs_status.bin"; fmt=struct.Struct("<IIIIiIIIIIQ"+("d"*45)+"II"); data=open(p,"rb").read(fmt.size); v=fmt.unpack(data); sys.exit(0 if v[1]==2 and v[3]==1 and v[5]==9 and v[6]==5 and v[7]==1 and v[8] else 1)'
wcs_block_info='import struct; p="/dev/shm/v5_native_wcs_status.bin"; fmt=struct.Struct("<IIIIiIIIIIQ"+("d"*45)+"II"); v=fmt.unpack(open(p,"rb").read(fmt.size)); print("wcs_block version=%d valid=%d active=%d wcs_count=%d axis_count=%d epoch=%d" % (v[1], v[3], v[4], v[5], v[6], v[8]))'
if remote "PYTHONPATH=/usr/lib/python3/dist-packages /usr/libexec/8ax/v5_wcs_status_publisher.py --once --path /dev/shm/v5_native_wcs_status.bin --ini /opt/8ax/v5/linuxcnc/ini/v5_bus.ini >/tmp/v5_wcs_status_publisher_once.out 2>&1 && test -s /dev/shm/v5_native_wcs_status.bin && /usr/bin/python3 -c '$wcs_block_check'" >/dev/null 2>&1; then
  ok "wcs status publisher one-shot publishes resident native WCS memory full table"
  remote "tail -n 3 /tmp/v5_wcs_status_publisher_once.out 2>/dev/null || true; /usr/bin/python3 -c '$wcs_block_info' 2>/dev/null || true" | sed 's/^/INFO wcs status publisher: /'
else
  fail_msg "wcs status publisher one-shot publishes resident native WCS memory full table"
  remote 'tail -n 10 /tmp/v5_wcs_status_publisher_once.out 2>/dev/null || true; ls -l /dev/shm/v5_native_wcs_status.bin /run/8ax_v5_product_ui/v3_native_wcs_status.bin 2>/dev/null || true' | sed 's/^/INFO wcs status publisher: /'
fi

check_remote_test "retired /run WCS v3 snapshot absent" 'test ! -e /run/8ax_v5_product_ui/v3_native_wcs_status.bin'
check_remote_test "retired /run native position/modal snapshots absent" 'test ! -e /run/8ax_v5_product_ui/v3_native_position_status.bin && test ! -e /run/8ax_v5_product_ui/v3_native_modal_tool_status.bin'
check_remote_test "registered native position/modal blocks in dev shm" 'test -s /dev/shm/v5_native_position_status.bin && test -s /dev/shm/v5_native_modal_tool_status.bin'
check_remote_test "registered native operator error block in dev shm" 'test -s /dev/shm/v5_native_operator_error_status.bin'
check_remote_test "registered native G53 geometry block in dev shm" 'test -s /dev/shm/v5_native_g53_geometry_status.bin'

operator_error_block_check='import struct,sys; p="/dev/shm/v5_verify_native_operator_error.bin"; fmt=struct.Struct("<IIIIIIQQ64s24s96s384s256sII"); v=fmt.unpack(open(p,"rb").read(fmt.size)); text=b" ".join((v[10],v[11],v[12])).decode("utf-8","replace").lower(); banned=("linuxcnc","linuxcncrsh"," emc "," hal "," nml "); sys.exit(0 if v[1]==1 and v[2]==fmt.size and v[3]==1 and v[6]==1 and all(x not in text for x in banned) else 1)'
if remote "PYTHONPATH=/usr/lib/python3/dist-packages /usr/libexec/8ax/v5_wcs_status_publisher.py --operator-error-map /opt/8ax/v5/config/ui/v5_native_operator_error_map.tsv --operator-error-path /dev/shm/v5_verify_native_operator_error.bin --mock-native-error \"Can't run a program when not homed\" >/tmp/v5_operator_error_mock.out 2>&1 && /usr/bin/python3 -c '$operator_error_block_check'" >/dev/null 2>&1; then
  ok "native operator error mapper publishes Chinese resident event block"
else
  fail_msg "native operator error mapper publishes Chinese resident event block"
  remote 'tail -n 10 /tmp/v5_operator_error_mock.out 2>/dev/null || true' | sed 's/^/INFO operator error mapper: /'
fi
remote 'rm -f /dev/shm/v5_verify_native_operator_error.bin /tmp/v5_operator_error_mock.out' >/dev/null 2>&1 || true

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

if remote '/usr/libexec/8ax/v5_lvgl_shell --once >/tmp/v5_lvgl_shell_verify.out 2>&1' >/dev/null 2>&1; then
  ok "v5_lvgl_shell starts"
  remote 'tail -n 3 /tmp/v5_lvgl_shell_verify.out 2>/dev/null || true' | sed 's/^/INFO ui shell: /'
else
  fail_msg "v5_lvgl_shell starts"
  remote 'tail -n 20 /tmp/v5_lvgl_shell_verify.out 2>/dev/null || true' | sed 's/^/INFO ui shell: /'
fi

if remote '/etc/init.d/v5-touch-diagnostics status' >/tmp/v5_touch_diag_status.out 2>&1; then
  ok "v5-touch-diagnostics init status running"
  sed 's/^/INFO touch diagnostics init: /' /tmp/v5_touch_diag_status.out
else
  fail_msg "v5-touch-diagnostics init status running"
  sed 's/^/INFO touch diagnostics init: /' /tmp/v5_touch_diag_status.out
fi
rm -f /tmp/v5_touch_diag_status.out

check_remote_test "v5 safe touch calibration readable" 'test -r /opt/8ax/safe_ui/re_touch_calibration.json && grep -q raw-evdev-cal-v2 /opt/8ax/safe_ui/re_touch_calibration.json'
check_remote_test "retired touch calibration path absent" 'test ! -e /opt/8ax/ui/re_touch_calibration.json'
check_remote_test "retired microkernel parameter table absent" 'test ! -e /opt/8ax/v5/config/settings/microkernel_parameter_table.tsv'

if remote 'test -S /run/8ax_v5_product_ui/v5_command_gate.sock && /etc/init.d/v5-linuxcnc-command-gate status' >/tmp/v5_command_gate_status.out 2>&1; then
  ok "native command gate UDS running"
  sed 's/^/INFO command gate: /' /tmp/v5_command_gate_status.out
else
  fail_msg "native command gate UDS running"
  sed 's/^/INFO command gate: /' /tmp/v5_command_gate_status.out
fi
rm -f /tmp/v5_command_gate_status.out

if remote '/etc/init.d/v5-ui-relay status' >/tmp/v5_ui_relay_status.out 2>&1; then
  ok "v5-ui-relay init status running"
  sed 's/^/INFO ui relay init: /' /tmp/v5_ui_relay_status.out
else
  fail_msg "v5-ui-relay init status running"
  sed 's/^/INFO ui relay init: /' /tmp/v5_ui_relay_status.out
fi
rm -f /tmp/v5_ui_relay_status.out

if remote 'wget -q -O /tmp/v5_remote_info_probe.json http://127.0.0.1:18080/remote/info && grep -q "\"protocol_version\"" /tmp/v5_remote_info_probe.json && grep -q "\"view_only\":false" /tmp/v5_remote_info_probe.json && grep -q "\"input_enabled\":true" /tmp/v5_remote_info_probe.json && grep -q "\"cpu0_percent\"" /tmp/v5_remote_info_probe.json && grep -q "\"cpu1_percent\"" /tmp/v5_remote_info_probe.json' >/dev/null 2>&1; then
  ok "v5 remote relay info JSON, input enabled, and system metrics probe: 18080"
  remote "wc -c /tmp/v5_remote_info_probe.json | sed 's/^/remote_info_bytes=/'" | sed 's/^/INFO /'
else
  fail_msg "v5 remote relay info JSON probe: 18080"
  remote 'head -c 160 /tmp/v5_remote_info_probe.json 2>/dev/null || true' | sed 's/^/INFO remote_info_prefix: /'
fi

if remote 'wget -q -O /tmp/v5_remote_frame_probe.bin http://127.0.0.1:18080/remote/frame/full && test -s /tmp/v5_remote_frame_probe.bin' >/dev/null 2>&1; then
  ok "v5 remote frame relay full-frame probe: 18080"
  remote "stat -c 'remote_frame_bytes=%s' /tmp/v5_remote_frame_probe.bin" | sed 's/^/INFO /'
else
  fail_msg "v5 remote frame relay full-frame probe: 18080"
fi

if remote 'found=0; for p in /proc/[0-9]*/cmdline; do c=$(tr "\000" " " < "$p" 2>/dev/null || true); case "$c" in *"milltask"*"v5_bus.ini"*) found=1;; esac; done; test "$found" = 1 && halcmd show comp 2>/dev/null | grep -Eq "(^|[[:space:]])(lcec|cia402)([[:space:]]|$)"' >/dev/null 2>&1; then
  ok "LinuxCNC active BUS/EtherCAT runtime loaded"
else
  fail_msg "LinuxCNC active BUS/EtherCAT runtime loaded"
  remote 'for p in /proc/[0-9]*/cmdline; do c=$(tr "\000" " " < "$p" 2>/dev/null || true); case "$c" in *linuxcnc*|*milltask*|*linuxcncrsh*|*v5_bus.ini*) printf "%s\n" "$c";; esac; done; halcmd show comp 2>/dev/null | grep -E "lcec|cia402|zynq_stepgen_hw" || true' | sed 's/^/INFO bus runtime: /'
fi

rtapi_affinity_check='pids=$(pidof rtapi_app 2>/dev/null || true); test -n "$pids"; for pid in $pids; do for status in /proc/$pid/task/[0-9]*/status; do test -r "$status"; tid=${status%/status}; tid=${tid##*/}; comm=$(cat /proc/$pid/task/$tid/comm 2>/dev/null || true); cpus=$(awk -F: '"'"'/^Cpus_allowed_list:/ {gsub(/[ \t]/, "", $2); print $2}'"'"' "$status"); printf "pid=%s tid=%s comm=%s Cpus_allowed_list=%s\n" "$pid" "$tid" "$comm" "$cpus"; test "$cpus" = 0; done; done'
if remote "$rtapi_affinity_check" >/tmp/v5_rtapi_affinity.out 2>&1; then
  ok "LinuxCNC RTAPI realtime threads pinned to CPU0"
  sed 's/^/INFO rtapi affinity: /' /tmp/v5_rtapi_affinity.out
else
  fail_msg "LinuxCNC RTAPI realtime threads pinned to CPU0"
  sed 's/^/INFO rtapi affinity: /' /tmp/v5_rtapi_affinity.out
fi
rm -f /tmp/v5_rtapi_affinity.out

if remote "/etc/init.d/v5-linuxcnc-command-gate status && /usr/libexec/8ax/v5_linuxcncrsh_probe --host 127.0.0.1 --port '$linuxcncrsh_port' --password EMC --timeout-ms 1000 >/tmp/v5_linuxcncrsh_probe.out 2>&1 && grep -q 'MACHINE ON' /tmp/v5_linuxcncrsh_probe.out" >/dev/null 2>&1; then
  ok "linuxcncrsh machine auto-on confirmed: $linuxcncrsh_port"
  remote 'tail -n 5 /tmp/v5_linuxcncrsh_probe.out 2>/dev/null || true' | sed 's/^/INFO linuxcncrsh: /'
else
  fail_msg "linuxcncrsh machine auto-on confirmed: $linuxcncrsh_port"
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

remote 'ps w 2>/dev/null | grep -E "v5_state_publisher|v5_rtcp_status_publisher|v5_g53_geometry_memory_owner|v5_wcs_status_publisher|v5_lvgl_shell|v5_remote_ui_relay|linuxcncrsh|linuxcncsvr|milltask" | grep -v grep || true' |
  sed 's/^/INFO process: /'

if [ "$fail" -ne 0 ]; then
  exit 1
fi
if [ "$warn" -ne 0 ]; then
  say "verify complete with warnings"
else
  say "verify complete"
fi
