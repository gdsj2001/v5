configured_ethercat_slave_count() {
  [ -r "$ETHERCAT_CONFIG" ] || return 1
  count=$(grep -c '<slave[[:space:]]' "$ETHERCAT_CONFIG" 2>/dev/null || true)
  [ -n "$count" ] && [ "$count" -gt 0 ] || return 1
  echo "$count"
}

ethercat_master_active() {
  master=$(ethercat master 2>/dev/null) || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Phase: Operation' || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Active: yes'
}

ethercat_master_inactive() {
  master=$(ethercat master 2>/dev/null) || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Phase: Idle' || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Active: no'
}

ethercat_domain_wkc_ready() {
  ethercat domains 2>/dev/null | awk '
    /WorkingCounter/ {
      seen = 1
      split($NF, wkc, "/")
      if ((wkc[1] + 0) <= 0 || (wkc[1] + 0) != (wkc[2] + 0)) bad = 1
    }
    END { exit !(seen && !bad) }
  '
}

ethercat_reference_clock_healthy() {
  [ "$(halcmd getp lcec.0.dc-time-valid 2>/dev/null || true)" = "TRUE" ] || return 1
  [ "$(halcmd getp lcec.0.dc-time-age-cycles 2>/dev/null || true)" = "0" ] || return 1
  first_seq=$(halcmd getp lcec.0.dc-time-ok-seq 2>/dev/null || true)
  first_errors=$(halcmd getp lcec.0.dc-time-error-count 2>/dev/null || true)
  [ -n "$first_seq" ] || return 1
  [ -n "$first_errors" ] || return 1
  sleep 0.20
  [ "$(halcmd getp lcec.0.dc-time-valid 2>/dev/null || true)" = "TRUE" ] || return 1
  [ "$(halcmd getp lcec.0.dc-time-age-cycles 2>/dev/null || true)" = "0" ] || return 1
  second_seq=$(halcmd getp lcec.0.dc-time-ok-seq 2>/dev/null || true)
  second_errors=$(halcmd getp lcec.0.dc-time-error-count 2>/dev/null || true)
  [ -n "$second_seq" ] || return 1
  [ -n "$second_errors" ] || return 1
  [ "$second_errors" = "$first_errors" ] || return 1
  [ "$second_seq" != "$first_seq" ]
}

ethercat_backend_ready() {
  backend_running || return 1
  ethercat_master_active || return 1
  expected=$(configured_ethercat_slave_count) || return 1
  [ "$(halcmd getp lcec.0.slaves-responding 2>/dev/null || true)" = "$expected" ] || return 1
  [ "$(halcmd getp lcec.0.dc-phased 2>/dev/null || true)" = "TRUE" ] || return 1
  ethercat_domain_wkc_ready || return 1
  ethercat_reference_clock_healthy
}

ethercat_backend_stopped() {
  backend_residue_running && {
    echo "LinuxCNC backend residue remained after stop" >&2
    return 1
  }
  i=0
  while [ "$i" -lt 40 ]; do
    ethercat_master_inactive && return 0
    i=$((i + 1))
    sleep 0.25
  done
  echo "EtherCAT master remained active after LinuxCNC stop" >&2
  return 1
}

wait_linuxcnc_backend_ready() {
  i=0
  affinity_ready=0
  while [ "$i" -lt 240 ]; do
    if backend_running; then
      if [ "$affinity_ready" = "0" ] &&
         set_linuxcnc_realtime_affinity &&
         set_linuxcnc_non_realtime_affinity &&
         set_linuxcnc_non_realtime_priority; then
        affinity_ready=1
      fi
      [ "$affinity_ready" = "1" ] &&
        linuxcnc_realtime_scheduler_ok &&
        ethercat_backend_ready && return 0
    fi
    if [ -r "$LINUXCNC_PID" ] && ! kill -0 "$(cat "$LINUXCNC_PID")" 2>/dev/null; then
      break
    fi
    i=$((i + 1))
    sleep 0.25
  done
  if backend_running && ! linuxcnc_realtime_scheduler_ok; then
    echo "LinuxCNC RTAPI servo thread remained outside the realtime scheduler" >&2
  fi
  return 1
}
