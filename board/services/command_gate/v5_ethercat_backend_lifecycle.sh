ethercat_master_inactive() {
  master=$(ethercat master 2>/dev/null) || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Phase: Idle' || return 1
  printf '%s\n' "$master" | grep -q '^[[:space:]]*Active: no'
}

ethercat_backend_ready() {
  backend_running || return 1
  # lcec owns the 100-cycle OP/WKC/DC proof in the resident servo path.
  # Each readiness/status pass performs one bounded HAL attach only.
  [ "$(timeout 1 halcmd getp lcec.0.runtime-ready 2>/dev/null || true)" = "TRUE" ]
}

start_position_hal_consumer_before_ready() {
  position_init=/etc/init.d/v5-position-status-publisher
  [ -x "$position_init" ] || {
    echo "Position Publisher init owner missing before EtherCAT readiness" >&2
    return 1
  }
  if "$position_init" status >/dev/null 2>&1; then
    return 0
  fi
  "$position_init" start || return 1
  i=0
  while [ "$i" -lt 40 ]; do
    "$position_init" status >/dev/null 2>&1 && return 0
    i=$((i + 1))
    sleep 0.25
  done
  echo "Position Publisher failed to attach before EtherCAT readiness" >&2
  return 1
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
  position_consumer_attempted=0
  position_consumer_ready=0
  while [ "$i" -lt 240 ]; do
    if backend_running; then
      if [ "$affinity_ready" = "0" ] &&
         set_linuxcnc_realtime_affinity &&
         set_linuxcnc_non_realtime_affinity &&
         set_linuxcnc_non_realtime_priority; then
        affinity_ready=1
      fi
      if [ "$affinity_ready" = "1" ] &&
         [ "$position_consumer_attempted" = "0" ]; then
        position_consumer_attempted=1
        start_position_hal_consumer_before_ready || return 1
        position_consumer_ready=1
      fi
      [ "$affinity_ready" = "1" ] &&
        [ "$position_consumer_ready" = "1" ] &&
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
