#!/bin/sh
PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin

: "${NETMASK:=255.255.255.0}"
: "${MODE_FILE:=/etc/v5/network_mode}"
: "${WPA_CONF:=/home/petalinux/.config/6x-cnc/usb_wifi/wpa_supplicant.conf}"
: "${WPA_DRIVER:=nl80211}"
: "${WIRED_WAIT_SECS:=12}"
: "${WIFI_WAIT_SECS:=20}"
: "${WIRED_TABLE_ID:=220}"
: "${WIFI_TABLE_ID:=221}"
: "${WIRED_ROUTE_IP:=192.168.1.221}"
: "${WIFI_ROUTE_IP:=192.168.1.220}"
: "${WIRED_FORCE_MODE:=100full}"
: "${WIRED_SET_MAC:=1}"
: "${WIRED_WATCHDOG:=1}"
: "${V5_MAINT_IFACE_DEFAULT:=eth0}"
: "${V5_MAINT_MAC_DEFAULT:=00:0a:35:00:8b:87}"
: "${V5_ETHERCAT_IFACE_DEFAULT:=eth1}"
: "${V5_ETHERCAT_MAC_DEFAULT:=00:0a:35:00:11:55}"
: "${SSH_BACKEND:=none}"

log() {
    [ -n "${LOG:-}" ] || return 0
    mkdir -p "$(dirname "$LOG")" 2>/dev/null || true
    echo "[$(date '+%F %T' 2>/dev/null || echo unknown-time)] $*" >>"$LOG"
}

have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

read_mode() {
    if [ -f "$MODE_FILE" ]; then
        tr -d ' \t\r\n' <"$MODE_FILE" 2>/dev/null | tr 'A-Z' 'a-z'
        return 0
    fi
    echo auto
}

carrier() {
    cat "/sys/class/net/$1/carrier" 2>/dev/null || echo 0
}

iface_exists() {
    [ -d "/sys/class/net/$1" ]
}

iface_mac_current() {
    cat "/sys/class/net/$1/address" 2>/dev/null | tr 'A-Z' 'a-z'
}

iface_mac_matches() {
    iface="$1"
    expected="$2"
    [ -n "$expected" ] || return 0
    [ "$(iface_mac_current "$iface")" = "$(printf '%s' "$expected" | tr 'A-Z' 'a-z')" ]
}

is_ethercat_reserved_iface() {
    [ -n "${V5_ETHERCAT_IFACE_DEFAULT:-}" ] && [ "$1" = "$V5_ETHERCAT_IFACE_DEFAULT" ]
}

iface_up() {
    iface="$1"
    if have_cmd ip; then
        ip link set "$iface" up 2>/dev/null || true
    else
        ifconfig "$iface" up 2>/dev/null || true
    fi
}

iface_down() {
    iface="$1"
    if have_cmd ip; then
        ip link set "$iface" down 2>/dev/null || true
    else
        ifconfig "$iface" down 2>/dev/null || true
    fi
}

clear_iface_ip() {
    iface="$1"
    if have_cmd ip; then
        ip addr flush dev "$iface" 2>/dev/null || true
    else
        ifconfig "$iface" 0.0.0.0 up 2>/dev/null || true
    fi
}

set_iface_mac() {
    iface="$1"
    mac="$2"
    iface_down "$iface"
    if have_cmd ip; then
        ip link set dev "$iface" address "$mac" 2>/dev/null || true
    else
        ifconfig "$iface" hw ether "$mac" 2>/dev/null || true
    fi
    iface_up "$iface"
}

set_static_ip() {
    iface="$1"
    cidr="$2"
    raw="$3"
    if have_cmd ip; then
        ip addr replace "$cidr" dev "$iface" 2>/dev/null || true
    else
        ifconfig "$iface" "$raw" netmask "$NETMASK" up 2>/dev/null || true
    fi
}

set_default_route() {
    iface="$1"
    if have_cmd ip; then
        ip route replace default via "$GATEWAY" dev "$iface" onlink 2>/dev/null || true
    else
        route del default 2>/dev/null || true
        route add default gw "$GATEWAY" "$iface" 2>/dev/null || true
    fi
}

clear_default_route_for() {
    iface="$1"
    if have_cmd ip; then
        ip route del default dev "$iface" 2>/dev/null || true
    else
        route del default 2>/dev/null || true
    fi
}

disable_ps() {
    iface="$1"
    have_cmd iw && iw dev "$iface" set power_save off >/dev/null 2>&1 || true
    if have_cmd ethtool; then
        ethtool --set-eee "$iface" eee off >/dev/null 2>&1 || true
        ethtool -s "$iface" wol d >/dev/null 2>&1 || true
    fi
}

force_wired_mode() {
    iface="$1"
    have_cmd ethtool || return 0
    case "${WIRED_FORCE_MODE:-100full}" in
        auto|autoneg|none|"")
            ethtool -s "$iface" autoneg on >/dev/null 2>&1 || true
            ;;
        100full|100-full)
            ethtool -s "$iface" speed 100 duplex full autoneg off >/dev/null 2>&1 || true
            ;;
        *)
            ethtool -s "$iface" autoneg on >/dev/null 2>&1 || true
            ;;
    esac
}

wait_carrier() {
    iface="$1"
    max_wait="$2"
    n=0
    while [ "$n" -lt "$max_wait" ]; do
        [ "$(carrier "$iface")" = "1" ] && return 0
        sleep 1
        n=$((n + 1))
    done
    return 1
}

apply_multihome_sysctl() {
    for kv in \
        "net.ipv4.conf.all.arp_ignore=1" \
        "net.ipv4.conf.all.arp_announce=2" \
        "net.ipv4.conf.all.rp_filter=0"
    do
        sysctl -w "$kv" >/dev/null 2>&1 || true
    done
    for iface in ${SYSCTL_IFACES:-"$WIRED_A $WIRED_B $WIFI_A $WIFI_B"}; do
        [ -n "$iface" ] || continue
        sysctl -w "net.ipv4.conf.$iface.rp_filter=0" >/dev/null 2>&1 || true
    done
}

apply_policy_route() {
    src_ip="$1"
    iface="$2"
    table_id="$3"
    have_cmd ip || return 0
    ip rule del from "$src_ip/32" table "$table_id" 2>/dev/null || true
    ip route flush table "$table_id" 2>/dev/null || true
    ip route add 192.168.1.0/24 dev "$iface" src "$src_ip" table "$table_id" 2>/dev/null || true
    ip route add default via "$GATEWAY" dev "$iface" table "$table_id" 2>/dev/null || true
    ip rule add from "$src_ip/32" table "$table_id" priority "$table_id" 2>/dev/null || true
}

warm_l2_l3_path() {
    iface="$1"
    src_ip="$2"
    # Prime neighbor caches so the first external packet is less likely to pay the full ARP/link cost.
    if have_cmd arping; then
        arping -q -c 1 -A -I "$iface" "$src_ip" >/dev/null 2>&1 || true
        arping -q -c 1 -U -I "$iface" "$src_ip" >/dev/null 2>&1 || true
        arping -q -c 1 -I "$iface" "$GATEWAY" >/dev/null 2>&1 || true
    fi
    if have_cmd ping; then
        ping -c 1 -W 1 -I "$iface" "$GATEWAY" >/dev/null 2>&1 || true
    fi
}

start_ssh_backend() {
    log "start_ssh_backend begin backend=$SSH_BACKEND"
    case "$SSH_BACKEND" in
        auto)
            if [ -n "${DROPBEAR_BIN:-}" ] && [ -x "${DROPBEAR_BIN:-}" ]; then
                if ! pidof dropbear >/dev/null 2>&1; then
                    "$DROPBEAR_BIN" -r /etc/dropbear/dropbear_rsa_host_key -p 22 -B || true
                    log "auto ssh backend -> dropbear"
                fi
            elif have_cmd systemctl; then
                systemctl start --no-block ssh.service >/dev/null 2>&1 || systemctl start --no-block sshd.service >/dev/null 2>&1 || true
                log "auto ssh backend -> systemd ssh"
            fi
            ;;
        dropbear)
            if [ -n "${DROPBEAR_BIN:-}" ] && ! pidof dropbear >/dev/null 2>&1; then
                "$DROPBEAR_BIN" -r /etc/dropbear/dropbear_rsa_host_key -p 22 -B || true
                log "dropbear start requested"
            fi
            ;;
        systemd:ssh.service|systemd:sshd.service)
            svc="${SSH_BACKEND#systemd:}"
            have_cmd systemctl && systemctl start --no-block "$svc" >/dev/null 2>&1 || true
            log "systemd ssh service start requested: $svc"
            ;;
        *)
            ;;
    esac
    log "start_ssh_backend done backend=$SSH_BACKEND"
}

start_wired_watchdog() {
    iface="$1"
    ip_cidr="$2"
    raw_ip="$3"
    table_id="$4"
    route_ip="$5"
    (
        i=0
        while [ "$i" -lt 18 ]; do
            sleep 5
            if [ "$(carrier "$iface")" != "1" ]; then
                log "wired watchdog bounce iface=$iface iter=$i"
                iface_down "$iface"
                sleep 2
                iface_up "$iface"
                force_wired_mode "$iface"
                wait_carrier "$iface" 6 || true
                set_static_ip "$iface" "$ip_cidr" "$raw_ip"
                set_default_route "$iface"
                apply_policy_route "$route_ip" "$iface" "$table_id"
                warm_l2_l3_path "$iface" "$route_ip"
            fi
            i=$((i + 1))
        done
    ) >>"$LOG" 2>&1 &
}

pick_wired_iface() {
    maint_iface="${V5_MAINT_IFACE_DEFAULT:-$WIRED_B}"
    if [ -n "$maint_iface" ]; then
        if iface_exists "$maint_iface"; then
            if ! iface_mac_matches "$maint_iface" "${V5_MAINT_MAC_DEFAULT:-}"; then
                log "maint iface mac mismatch iface=$maint_iface actual=$(iface_mac_current "$maint_iface") expected=${V5_MAINT_MAC_DEFAULT:-unset}; refuse drift"
                echo ""
                return 0
            fi
            tries=0
            while [ "$tries" -lt "$WIRED_WAIT_SECS" ]; do
                [ "$(carrier "$maint_iface")" = "1" ] && { echo "$maint_iface"; return 0; }
                tries=$((tries + 1))
                sleep 1
            done
            echo "$maint_iface"
            return 0
        fi
        log "maint iface missing iface=$maint_iface; refuse automatic drift to EtherCAT port"
        echo ""
        return 0
    fi
    tries=0
    while [ "$tries" -lt "$WIRED_WAIT_SECS" ]; do
        if [ -n "${WIRED_B:-}" ] && ! is_ethercat_reserved_iface "$WIRED_B" && iface_exists "$WIRED_B" && [ "$(carrier "$WIRED_B")" = "1" ]; then
            echo "$WIRED_B"
            return 0
        fi
        if [ -n "${WIRED_A:-}" ] && ! is_ethercat_reserved_iface "$WIRED_A" && iface_exists "$WIRED_A" && [ "$(carrier "$WIRED_A")" = "1" ]; then
            echo "$WIRED_A"
            return 0
        fi
        tries=$((tries + 1))
        sleep 1
    done
    if [ -n "${WIRED_B:-}" ] && ! is_ethercat_reserved_iface "$WIRED_B" && iface_exists "$WIRED_B"; then
        echo "$WIRED_B"
        return 0
    fi
    if [ -n "${WIRED_A:-}" ] && ! is_ethercat_reserved_iface "$WIRED_A" && iface_exists "$WIRED_A"; then
        echo "$WIRED_A"
        return 0
    fi
    echo ""
}

configure_wired() {
    WIRED_SELECTED=""
    log "configure_wired begin A=${WIRED_A:-none} B=${WIRED_B:-none} mode=${MODE:-unknown} maint=${V5_MAINT_IFACE_DEFAULT:-none} ethercat=${V5_ETHERCAT_IFACE_DEFAULT:-none}"
    [ -n "${WIRED_A:-}" ] && iface_exists "$WIRED_A" && iface_up "$WIRED_A" && force_wired_mode "$WIRED_A" && disable_ps "$WIRED_A"
    [ -n "${WIRED_B:-}" ] && iface_exists "$WIRED_B" && iface_up "$WIRED_B" && force_wired_mode "$WIRED_B" && disable_ps "$WIRED_B"
    sleep 2

    [ "$MODE" = "wifi" ] && log "mode=wifi keeps wired maintenance alive; external USB wifi is additive"

    WIRED_SELECTED=$(pick_wired_iface)
    [ -n "$WIRED_SELECTED" ] || { log "configure_wired no carrier-selected interface"; return 0; }
    WIRED_MAC="$WIRED_A_MAC"
    [ "$WIRED_SELECTED" = "$WIRED_B" ] && WIRED_MAC="$WIRED_B_MAC"
    [ "$WIRED_SELECTED" = "${V5_MAINT_IFACE_DEFAULT:-}" ] && [ -n "${V5_MAINT_MAC_DEFAULT:-}" ] && WIRED_MAC="$V5_MAINT_MAC_DEFAULT"
    if is_ethercat_reserved_iface "$WIRED_SELECTED"; then
        log "ERROR: selected EtherCAT reserved iface for maintenance, refuse iface=$WIRED_SELECTED"
        WIRED_SELECTED=""
        return 0
    fi

    for iface in "$WIRED_A" "$WIRED_B"; do
        [ -n "$iface" ] && iface_exists "$iface" || continue
        clear_iface_ip "$iface"
    done
    if [ "${WIRED_SET_MAC:-1}" = "1" ]; then
        set_iface_mac "$WIRED_SELECTED" "$WIRED_MAC"
    else
        log "wired mac rewrite disabled iface=$WIRED_SELECTED"
    fi
    force_wired_mode "$WIRED_SELECTED"
    wait_carrier "$WIRED_SELECTED" 8 || true
    set_static_ip "$WIRED_SELECTED" "$WIRED_CIDR" "$WIRED_ROUTE_IP"
    set_default_route "$WIRED_SELECTED"
    apply_policy_route "$WIRED_ROUTE_IP" "$WIRED_SELECTED" "$WIRED_TABLE_ID"
    warm_l2_l3_path "$WIRED_SELECTED" "$WIRED_ROUTE_IP"
    log "wired selected=$WIRED_SELECTED addr=$WIRED_CIDR gw=$GATEWAY table=$WIRED_TABLE_ID"
    if [ "${WIRED_WATCHDOG:-1}" != "0" ]; then
        start_wired_watchdog "$WIRED_SELECTED" "$WIRED_CIDR" "$WIRED_ROUTE_IP" "$WIRED_TABLE_ID" "$WIRED_ROUTE_IP"
    else
        log "wired watchdog disabled iface=$WIRED_SELECTED"
    fi
}

. /usr/local/sbin/v5_wifi_core.sh

v5_net_apply() {
    MODE=$(read_mode)
    case "$MODE" in
        ""|auto|wired|wifi|dual) ;;
        *) MODE=auto ;;
    esac
    if [ "${V5_FORCE_WIRED:-0}" = "1" ] && [ "$MODE" != "wifi" ]; then
        MODE=wired
    fi
    log "net apply start mode=$MODE"
    apply_multihome_sysctl
    configure_wired
    configure_wifi
    start_ssh_backend
    log "net apply done wired=${WIRED_SELECTED:-none} wifi=${WIFI_SELECTED:-none}"
    return 0
}
