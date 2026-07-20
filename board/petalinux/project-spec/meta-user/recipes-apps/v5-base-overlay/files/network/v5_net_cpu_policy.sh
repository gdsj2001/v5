#!/bin/sh

ETHERCAT_SOFTIRQ_PRIORITY=49

ethercat_softirq_tids() {
    for v5_softirq_comm in /proc/[0-9]*/task/[0-9]*/comm; do
        [ -r "$v5_softirq_comm" ] || continue
        [ "$(cat "$v5_softirq_comm" 2>/dev/null || true)" = "ksoftirqd/0" ] || continue
        v5_softirq_task=${v5_softirq_comm%/comm}
        printf '%s\n' "${v5_softirq_task##*/}"
    done
}

set_ethercat_softirq_priority() {
    [ -x /usr/bin/chrt ] || {
        log "ERROR EtherCAT softirq owner missing /usr/bin/chrt"
        return 1
    }
    set -- $(ethercat_softirq_tids)
    [ "$#" -eq 1 ] || {
        log "ERROR EtherCAT ksoftirqd/0 thread count=$# expected=1"
        return 1
    }
    v5_softirq_tid=$1
    /usr/bin/chrt -f -p "$ETHERCAT_SOFTIRQ_PRIORITY" "$v5_softirq_tid" || return 1
    v5_softirq_policy=$(awk '{print $41}' "/proc/$v5_softirq_tid/stat" 2>/dev/null || true)
    v5_softirq_priority=$(awk '{print $40}' "/proc/$v5_softirq_tid/stat" 2>/dev/null || true)
    v5_softirq_cpus=$(awk -F: '/^Cpus_allowed_list:/ {gsub(/[ \t]/, "", $2); print $2}' \
        "/proc/$v5_softirq_tid/status" 2>/dev/null || true)
    [ "$v5_softirq_policy" = "1" ] || return 1
    [ "$v5_softirq_priority" = "$ETHERCAT_SOFTIRQ_PRIORITY" ] || return 1
    [ "$v5_softirq_cpus" = "0" ] || return 1
    log "cpu isolation EtherCAT softirq tid=$v5_softirq_tid cpu=0 policy=1 priority=$ETHERCAT_SOFTIRQ_PRIORITY"
}

dropbear_cpu1_affinity_ok() {
    v5_dropbear_pids=$(pidof dropbear 2>/dev/null || true)
    [ -n "$v5_dropbear_pids" ] || return 1
    v5_dropbear_found=0
    for v5_dropbear_pid in $v5_dropbear_pids; do
        for v5_dropbear_status in /proc/"$v5_dropbear_pid"/task/[0-9]*/status; do
            [ -r "$v5_dropbear_status" ] || continue
            v5_dropbear_found=1
            v5_dropbear_cpu_list=$(awk -F: '/^Cpus_allowed_list:/ {gsub(/[ \t]/, "", $2); print $2}' \
                "$v5_dropbear_status" 2>/dev/null || true)
            [ "$v5_dropbear_cpu_list" = "1" ] || return 1
        done
    done
    [ "$v5_dropbear_found" = "1" ]
}

stop_dropbear_after_affinity_failure() {
    v5_dropbear_pids=$(pidof dropbear 2>/dev/null || true)
    [ -z "$v5_dropbear_pids" ] || kill $v5_dropbear_pids 2>/dev/null || true
    log "ERROR dropbear stopped because CPU1 affinity could not be guaranteed"
}

enforce_dropbear_cpu1_affinity() {
    cpu1_available || return 1
    [ -x /usr/bin/taskset ] || return 1
    v5_dropbear_pids=$(pidof dropbear 2>/dev/null || true)
    [ -n "$v5_dropbear_pids" ] || return 1
    for v5_dropbear_pid in $v5_dropbear_pids; do
        /usr/bin/taskset -a -pc 1 "$v5_dropbear_pid" >/dev/null 2>&1 || return 1
    done
    dropbear_cpu1_affinity_ok || return 1
    log "dropbear cpu isolation pids=$v5_dropbear_pids cpu=1"
}

start_dropbear_cpu1() {
    cpu1_available || return 1
    [ -x /usr/bin/taskset ] || return 1
    /usr/bin/taskset -c 1 "$DROPBEAR_BIN" \
        -r /etc/dropbear/dropbear_rsa_host_key -p 22 -B || return 1
    enforce_dropbear_cpu1_affinity
}
