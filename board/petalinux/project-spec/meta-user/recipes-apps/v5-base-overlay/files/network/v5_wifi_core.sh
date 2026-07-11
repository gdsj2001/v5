#!/bin/sh

wifi_assoc_ok() {
    iface="$1"
    if have_cmd wpa_cli; then
        state=$(wpa_cli -i "$iface" status 2>/dev/null | awk -F= '/^wpa_state=/{print $2}' || true)
        [ "$state" = "COMPLETED" ] && return 0
    fi
    iwconfig "$iface" 2>/dev/null | grep -qv 'Not-Associated'
}

iface_driver_name() {
    iface="$1"
    readlink "/sys/class/net/$iface/device/driver" 2>/dev/null | sed 's#.*/##' | tr 'A-Z' 'a-z'
}

iface_usb_product() {
    iface="$1"
    awk -F= '/^PRODUCT=/{print tolower($2)}' "/sys/class/net/$iface/device/uevent" 2>/dev/null
}

is_supported_usb_wifi_iface() {
    iface="$1"
    [ -n "$iface" ] || return 1
    [ "$iface" = "lo" ] && return 1
    [ "$iface" = "${WIRED_A:-}" ] && return 1
    [ "$iface" = "${WIRED_B:-}" ] && return 1
    [ "$iface" = "${V5_MAINT_IFACE_DEFAULT:-}" ] && return 1
    [ "$iface" = "${V5_ETHERCAT_IFACE_DEFAULT:-}" ] && return 1
    [ -d "/sys/class/net/$iface" ] || return 1
    driver="$(iface_driver_name "$iface")"
    product="$(iface_usb_product "$iface")"
    case "$driver:$product" in
        *8733*|*bda/b733/*|*0bda/b733/*)
            return 1
            ;;
    esac
    case "$product" in
        bda/0179/*|0bda/0179/*|bda/8179/*|0bda/8179/*)
            return 0
            ;;
    esac
    case "$driver" in
        8188eu|r8188eu|rtl8188eu|rtl8xxxu)
            return 0
            ;;
    esac
    return 1
}

find_usb_wifi_iface() {
    for path in /sys/class/net/*; do
        [ -e "$path" ] || continue
        iface="${path##*/}"
        if is_supported_usb_wifi_iface "$iface"; then
            echo "$iface"
            return 0
        fi
    done
    echo ""
    return 1
}

owned_wpa_pidfile() {
    iface="$1"
    echo "/run/v5/wpa_supplicant_${iface}.pid"
}

stop_owned_wpa() {
    iface="$1"
    pidfile="$(owned_wpa_pidfile "$iface")"
    if [ -f "$pidfile" ]; then
        pid="$(cat "$pidfile" 2>/dev/null || true)"
        [ -n "$pid" ] && kill "$pid" >/dev/null 2>&1 || true
        rm -f "$pidfile" 2>/dev/null || true
    fi
}

start_wifi_watchdog() {
    iface="$1"
    ip_cidr="$2"
    route_ip="$3"
    table_id="$4"
    (
        while true; do
            if ! wifi_assoc_ok "$iface"; then
                log "wifi watchdog reconnect iface=$iface"
                stop_owned_wpa "$iface"
                sleep 1
                iface_up "$iface"
                disable_ps "$iface"
                mkdir -p /run/v5 2>/dev/null || true
                wpa_supplicant -B -P "$(owned_wpa_pidfile "$iface")" -i "$iface" -c "$WPA_CONF" -D "$WPA_DRIVER" >/dev/null 2>&1 || \
                    wpa_supplicant -B -i "$iface" -c "$WPA_CONF" -D "$WPA_DRIVER" >/dev/null 2>&1 || true
                sleep 3
                have_cmd wpa_cli && wpa_cli -i "$iface" reconfigure >/dev/null 2>&1 || true
                have_cmd wpa_cli && wpa_cli -i "$iface" reconnect >/dev/null 2>&1 || true
                set_static_ip "$iface" "$ip_cidr" "$route_ip"
                apply_policy_route "$route_ip" "$iface" "$table_id"
                if [ -z "${WIRED_SELECTED:-}" ]; then
                    set_default_route "$iface"
                fi
            fi
            sleep 5
        done
    ) >>"$LOG" 2>&1 &
}

configure_wifi() {
    WIFI_SELECTED=""
    log "configure_wifi begin A=${WIFI_A:-none} B=${WIFI_B:-none} mode=${MODE:-unknown} wired=${WIRED_SELECTED:-none}"
    if [ -z "${WIFI_A:-}" ]; then
        WIFI_A="$(find_usb_wifi_iface || true)"
    fi
    [ -n "${WIFI_A:-}" ] || { log "configure_wifi no supported external usb wifi iface"; return 0; }
    [ -f "$WPA_CONF" ] || { log "wifi config missing"; return 0; }

    if [ "$MODE" = "wired" ] || { [ "$MODE" = "auto" ] && [ -n "${WIRED_SELECTED:-}" ]; }; then
        log "mode=$MODE wired carrier detected, keep wifi idle"
        [ -n "${WIFI_A:-}" ] && iface_exists "$WIFI_A" && stop_owned_wpa "$WIFI_A"
        [ -n "${WIFI_A:-}" ] && iface_exists "$WIFI_A" && clear_default_route_for "$WIFI_A" && clear_iface_ip "$WIFI_A" && iface_down "$WIFI_A"
        return 0
    fi

    [ "$MODE" = "wifi" ] && log "mode=wifi does not drop eth0 maintenance; USB wifi is additive"

    for iface in "$WIFI_A" "$WIFI_B"; do
        [ -n "$iface" ] && iface_exists "$iface" || continue
        is_supported_usb_wifi_iface "$iface" || { log "wifi reject unsupported iface=$iface driver=$(iface_driver_name "$iface") product=$(iface_usb_product "$iface")"; continue; }
        log "wifi try iface=$iface"
        iface_up "$iface"
        disable_ps "$iface"
        clear_iface_ip "$iface"
        stop_owned_wpa "$iface"
        mkdir -p /run/v5 2>/dev/null || true
        wpa_supplicant -B -P "$(owned_wpa_pidfile "$iface")" -i "$iface" -c "$WPA_CONF" -D "$WPA_DRIVER" >/dev/null 2>&1 || \
            wpa_supplicant -B -i "$iface" -c "$WPA_CONF" -D "$WPA_DRIVER" >/dev/null 2>&1 || true
        n=0
        while [ "$n" -lt "$WIFI_WAIT_SECS" ]; do
            if wifi_assoc_ok "$iface"; then
                WIFI_SELECTED="$iface"
                log "wifi assoc ok iface=$iface wait=${n}s"
                break
            fi
            sleep 1
            n=$((n + 1))
        done
        if [ -n "$WIFI_SELECTED" ]; then
            set_static_ip "$WIFI_SELECTED" "$WIFI_CIDR" "$WIFI_ROUTE_IP"
            apply_policy_route "$WIFI_ROUTE_IP" "$WIFI_SELECTED" "$WIFI_TABLE_ID"
            [ -n "${WIRED_SELECTED:-}" ] || set_default_route "$WIFI_SELECTED"
            log "wifi selected=$WIFI_SELECTED addr=$WIFI_CIDR assoc_wait=${n}s table=$WIFI_TABLE_ID"
            break
        fi
        log "wifi assoc timeout iface=$iface wait=${n}s"
        stop_owned_wpa "$iface"
    done

    if [ -z "$WIFI_SELECTED" ]; then
        log "wifi no association"
    else
        for iface in "$WIFI_A" "$WIFI_B"; do
            [ -n "$iface" ] && iface_exists "$iface" || continue
            [ "$iface" = "$WIFI_SELECTED" ] && continue
            clear_default_route_for "$iface"
            clear_iface_ip "$iface"
            iface_down "$iface"
        done
        start_wifi_watchdog "$WIFI_SELECTED" "$WIFI_CIDR" "$WIFI_ROUTE_IP" "$WIFI_TABLE_ID"
    fi
}
