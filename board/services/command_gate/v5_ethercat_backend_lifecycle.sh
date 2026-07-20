configured_ethercat_slave_count() {
  [ -r "$ETHERCAT_CONFIG" ] || return 1
  count=$(grep -c '<slave[[:space:]]' "$ETHERCAT_CONFIG" 2>/dev/null || true)
  [ -n "$count" ] && [ "$count" -gt 0 ] || return 1
  echo "$count"
}

ethercat_transport_scanned() {
  expected=$(configured_ethercat_slave_count) || return 1
  master=$(ethercat master 2>/dev/null) || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Link: UP' || return 1
  reported=$(printf '%s\n' "$master" | awk '/^[[:space:]]*Slaves:/ { print $2; exit }')
  [ "$reported" = "$expected" ] || return 1
  slaves=$(ethercat slaves 2>/dev/null) || return 1
  actual=$(printf '%s\n' "$slaves" | awk '/^[[:space:]]*[0-9]+[[:space:]]/ { count++ } END { print count + 0 }')
  [ "$actual" = "$expected" ] || return 1
  printf '%s\n' "$slaves" | grep -q 'ERROR' && return 1
  printf '%s\n' "$slaves" | awk '
    /^[[:space:]]*[0-9]+[[:space:]]/ {
      seen++
      if (($1 + 0) != (seen - 1)) bad = 1
      if ($3 != "PREOP") bad = 1
      if ($4 == "E") bad = 1
    }
    END { exit !(seen > 0 && !bad) }
  '
}

wait_ethercat_transport_scanned() {
  i=0
  while [ "$i" -lt 40 ]; do
    ethercat_transport_scanned && return 0
    i=$((i + 1))
    sleep 0.5
  done
  echo "EtherCAT transport scan incomplete; refusing LinuxCNC/lcec activation" >&2
  ethercat master >&2 2>/dev/null || true
  ethercat slaves >&2 2>/dev/null || true
  return 1
}

ethercat_kernel_fault_count() {
  kernel_log=$(dmesg 2>/dev/null) || return 1
  printf '%s\n' "$kernel_log" | awk '
    /AL status message 0x001A: "Synchronization error"/ ||
    /Working counter changed to 0\// ||
    /Datagram .* was SKIPPED/ ||
    /datagrams TIMED OUT/ ||
    /datagrams UNMATCHED/ { count++ }
    END { print count + 0 }
  '
}

capture_ethercat_attach_fault_baseline() {
  count=$(ethercat_kernel_fault_count) || return 1
  [ -n "$count" ] || return 1
  mkdir -p "${ETHERCAT_ATTACH_FAULT_BASELINE%/*}" || return 1
  printf '%s\n' "$count" >"$ETHERCAT_ATTACH_FAULT_BASELINE"
}

ethercat_no_post_attach_faults() {
  [ -r "$ETHERCAT_ATTACH_FAULT_BASELINE" ] || return 1
  read -r baseline <"$ETHERCAT_ATTACH_FAULT_BASELINE" || return 1
  current=$(ethercat_kernel_fault_count) || return 1
  [ -n "$baseline" ] && [ "$current" = "$baseline" ]
}

ethercat_slaves_clean_state() {
  target="$1"
  expected=$(configured_ethercat_slave_count) || return 1
  slaves=$(ethercat slaves 2>/dev/null) || return 1
  printf '%s\n' "$slaves" | awk -v expected="$expected" -v target="$target" '
    /^[[:space:]]*[0-9]+[[:space:]]/ {
      seen++
      if (($1 + 0) != (seen - 1)) bad = 1
      if ($3 != target) bad = 1
      if ($4 == "E") bad = 1
    }
    END { exit !(seen == expected && !bad) }
  '
}

wait_ethercat_slaves_clean_state() {
  target="$1"
  i=0
  while [ "$i" -lt 40 ]; do
    ethercat_slaves_clean_state "$target" && return 0
    i=$((i + 1))
    sleep 0.05
  done
  return 1
}

quiesce_ethercat_slaves_before_release() {
  ethercat states PREOP >/dev/null 2>&1 || true
  wait_ethercat_slaves_clean_state PREOP && return 0

  # A latched AL error may require an INIT transition before PREOP can be
  # acknowledged. Both transitions happen while the master is still owned;
  # releasing it first would strand powered drives in SAFEOP+ERROR.
  ethercat states INIT >/dev/null 2>&1 || return 1
  wait_ethercat_slaves_clean_state INIT || return 1
  ethercat states PREOP >/dev/null 2>&1 || return 1
  wait_ethercat_slaves_clean_state PREOP
}

ethercat_master_active() {
  master=$(ethercat master 2>/dev/null) || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Phase: Operation' || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Active: yes'
}

ethercat_master_inactive() {
  [ -r /proc/modules ] || return 1
  if ! grep -Eq '^(ec_master|ec_generic)[[:space:]]' /proc/modules; then
    return 0
  fi
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

ethercat_resident_all_op() {
  expected=$(configured_ethercat_slave_count) || return 1
  [ "$(halcmd getp lcec.0.slaves-responding 2>/dev/null || true)" = "$expected" ] || return 1
  [ "$(halcmd getp lcec.0.all-op 2>/dev/null || true)" = "TRUE" ] || return 1
  ethercat_slaves_clean_state OP
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
  ethercat_resident_all_op || return 1
  [ "$(halcmd getp lcec.0.dc-phased 2>/dev/null || true)" = "TRUE" ] || return 1
  ethercat_domain_wkc_ready || return 1
  ethercat_reference_clock_healthy || return 1
  ethercat_no_post_attach_faults
}

ethercat_backend_stopped() {
  i=0
  while [ "$i" -lt 40 ]; do
    # LinuxCNC children can disappear just after the bounded stop loop above.
    # Require both process quiescence and an inactive master, but do not turn
    # that normal teardown interval into a false restart failure.
    if ! backend_residue_running && ethercat_master_inactive; then
      return 0
    fi
    i=$((i + 1))
    sleep 0.25
  done
  if backend_residue_running; then
    echo "LinuxCNC backend residue remained after stop" >&2
  fi
  if ! ethercat_master_inactive; then
    echo "EtherCAT master remained active after LinuxCNC stop" >&2
  fi
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
