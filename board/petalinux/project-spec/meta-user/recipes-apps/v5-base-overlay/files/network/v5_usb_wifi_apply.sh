#!/bin/sh
PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin

LOG=${LOG:-/var/volatile/v5_usb_wifi_apply.log}
STATE_DIR=/run/v5
STATE_FILE=$STATE_DIR/usb_wifi_state.json
WPA_CONF=${WPA_CONF:-/home/petalinux/.config/6x-cnc/usb_wifi/wpa_supplicant.conf}
WPA_DRIVER=${WPA_DRIVER:-nl80211,wext}
WIFI_CIDR=${WIFI_CIDR:-192.168.1.220/24}
WIFI_ROUTE_IP=${WIFI_ROUTE_IP:-192.168.1.220}
WIFI_TABLE_ID=${WIFI_TABLE_ID:-221}
GATEWAY=${GATEWAY:-192.168.1.10}
WIRED_A=${WIRED_A:-eth0}
WIRED_B=${WIRED_B:-eth1}
V5_MAINT_IFACE_DEFAULT=${V5_MAINT_IFACE_DEFAULT:-eth0}
V5_ETHERCAT_IFACE_DEFAULT=${V5_ETHERCAT_IFACE_DEFAULT:-eth1}

log_usb_wifi() {
    mkdir -p "$(dirname "$LOG")" 2>/dev/null || true
    echo "[$(date '+%F %T' 2>/dev/null || echo unknown-time)] $*" >>"$LOG"
}

write_state() {
    status="$1"
    reason="$2"
    iface="${3:-}"
    driver="${4:-}"
    product="${5:-}"
    assoc="${6:-unknown}"
    mkdir -p "$STATE_DIR" 2>/dev/null || true
    cat >"$STATE_FILE" <<EOF
{"status":"$status","reason":"$reason","iface":"$iface","driver":"$driver","product":"$product","assoc":"$assoc","ip":"$WIFI_ROUTE_IP","maint_iface":"$V5_MAINT_IFACE_DEFAULT","ethercat_iface":"$V5_ETHERCAT_IFACE_DEFAULT"}
EOF
}

if [ -r /usr/local/sbin/v5_net_core.sh ]; then
    . /usr/local/sbin/v5_net_core.sh
else
    log_usb_wifi "v5_net_core.sh missing"
    write_state "blocked" "v5_net_core_missing"
    exit 0
fi

ensure_r8188eu_module() {
    grep -qw '^r8188eu' /proc/modules 2>/dev/null && return 0
    command -v modprobe >/dev/null 2>&1 || return 1
    modprobe r8188eu >>"$LOG" 2>&1 || return 1
    grep -qw '^r8188eu' /proc/modules 2>/dev/null
}

ensure_r8188eu_module || log_usb_wifi "r8188eu module not loaded; continuing absent probe"

iface="$(find_usb_wifi_iface || true)"
if [ -z "$iface" ]; then
    log_usb_wifi "no supported external USB WiFi; internal rtl8733bu/b733 is intentionally ignored"
    write_state "absent" "supported_usb_wifi_not_present"
    exit 0
fi

driver="$(iface_driver_name "$iface")"
product="$(iface_usb_product "$iface")"
if [ ! -f "$WPA_CONF" ]; then
    log_usb_wifi "wpa config missing: $WPA_CONF"
    write_state "blocked" "wpa_config_missing" "$iface" "$driver" "$product"
    exit 0
fi

LOG="$LOG"
WIFI_A="$iface"
WIFI_B=
MODE=dual
WIRED_SELECTED="${WIRED_SELECTED:-$V5_MAINT_IFACE_DEFAULT}"
log_usb_wifi "apply begin iface=$iface driver=$driver product=$product"
configure_wifi || true
if ! apply_network_cpu_isolation; then
    log_usb_wifi "apply blocked: CPU isolation owner rejected network layout"
    write_state "blocked" "cpu_isolation_failed" "$iface" "$driver" "$product"
    exit 1
fi

assoc=unknown
if command -v wpa_cli >/dev/null 2>&1; then
    assoc="$(wpa_cli -i "$iface" status 2>/dev/null | awk -F= '/^wpa_state=/{print $2}' | tail -1)"
fi
[ -n "$assoc" ] || assoc=unknown
if [ "$WIFI_SELECTED" = "$iface" ] && [ "$assoc" = "COMPLETED" ]; then
    log_usb_wifi "apply pass iface=$iface assoc=$assoc ip=$WIFI_ROUTE_IP"
    write_state "pass" "associated" "$iface" "$driver" "$product" "$assoc"
else
    log_usb_wifi "apply partial iface=$iface selected=${WIFI_SELECTED:-none} assoc=$assoc"
    write_state "partial" "not_associated" "$iface" "$driver" "$product" "$assoc"
fi

exit 0
