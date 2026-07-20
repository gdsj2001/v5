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

ethercat_master_inactive() {
  [ -r /proc/modules ] || return 1
  if ! grep -Eq '^(ec_master|ec_generic)[[:space:]]' /proc/modules; then
    return 0
  fi
  master=$(ethercat master 2>/dev/null) || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Phase: Idle' || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Active: no'
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
