#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TMPDIR_V5=$(mktemp -d)
trap 'rm -rf "$TMPDIR_V5"' EXIT HUP INT TERM

ETHERCAT_CONFIG=$TMPDIR_V5/ethercat.xml
ETHERCAT_ATTACH_FAULT_BASELINE=$TMPDIR_V5/fault-baseline
{
  echo '<masters><master>'
  i=0
  while [ "$i" -lt 5 ]; do
    printf '  <slave idx="%s" />\n' "$i"
    i=$((i + 1))
  done
  echo '</master></masters>'
} >"$ETHERCAT_CONFIG"

STATE=PREOP
ERROR_FLAG=+
MASTER_PHASE=Idle
FAULT_COUNT=0
DMESG_FAIL=0
SECOND_DC=0
DIRECT_PREOP_BLOCK=0
PREOP_REQUESTS=0
ALL_OP_OVERRIDE=

backend_running() {
  return 0
}

sleep() {
  SECOND_DC=1
}

dmesg() {
  [ "$DMESG_FAIL" = "0" ] || return 1
  i=0
  while [ "$i" -lt "$FAULT_COUNT" ]; do
    echo 'EtherCAT ERROR 0-0: AL status message 0x001A: "Synchronization error".'
    i=$((i + 1))
  done
}

ethercat() {
  command_name=${1:-}
  case "$command_name" in
    master)
      printf 'Phase: %s\n' "$MASTER_PHASE"
      echo 'Active: yes'
      echo 'Link: UP'
      echo 'Slaves: 5'
      ;;
    slaves)
      i=0
      while [ "$i" -lt 5 ]; do
        printf '%s  0:%s  %s  %s  SV630N\n' "$i" "$i" "$STATE" "$ERROR_FLAG"
        i=$((i + 1))
      done
      ;;
    domains)
      echo 'Domain0: WorkingCounter 10/10'
      ;;
    states)
      requested=${2:-}
      case "$requested" in
        PREOP)
          PREOP_REQUESTS=$((PREOP_REQUESTS + 1))
          if [ "$DIRECT_PREOP_BLOCK" = "0" ] || [ "$PREOP_REQUESTS" -gt 1 ]; then
            STATE=PREOP
            ERROR_FLAG=+
          fi
          ;;
        INIT)
          STATE=INIT
          ERROR_FLAG=+
          ;;
        *)
          return 1
          ;;
      esac
      ;;
    *)
      return 1
      ;;
  esac
}

halcmd() {
  [ "${1:-}" = getp ] || return 1
  case "${2:-}" in
    lcec.0.slaves-responding) echo 5 ;;
    lcec.0.all-op)
      if [ -n "$ALL_OP_OVERRIDE" ]; then
        echo "$ALL_OP_OVERRIDE"
      elif [ "$STATE" = OP ]; then
        echo TRUE
      else
        echo FALSE
      fi
      ;;
    lcec.0.dc-phased) echo TRUE ;;
    lcec.0.dc-time-valid) echo TRUE ;;
    lcec.0.dc-time-age-cycles) echo 0 ;;
    lcec.0.dc-time-ok-seq)
      [ "$SECOND_DC" = "0" ] && echo 41 || echo 42
      ;;
    lcec.0.dc-time-error-count) echo 0 ;;
    *) return 1 ;;
  esac
}

. "$SCRIPT_DIR/v5_ethercat_backend_lifecycle.sh"

ethercat_transport_scanned
FAULT_COUNT=1
capture_ethercat_attach_fault_baseline
ethercat_no_post_attach_faults
FAULT_COUNT=2
if ethercat_no_post_attach_faults; then
  echo 'post-attach EtherCAT fault was accepted' >&2
  exit 1
fi

DMESG_FAIL=1
if ethercat_kernel_fault_count >/dev/null; then
  echo 'unreadable kernel log was accepted' >&2
  exit 1
fi
DMESG_FAIL=0

FAULT_COUNT=1
MASTER_PHASE=Operation
STATE=OP
SECOND_DC=0
ethercat_backend_ready

STATE=SAFEOP
ALL_OP_OVERRIDE=TRUE
if ethercat_resident_all_op; then
  echo 'resident all-op without per-slave OP readback was accepted' >&2
  exit 1
fi
STATE=OP
ALL_OP_OVERRIDE=

STATE=PREOP
ERROR_FLAG=E
if ethercat_slaves_clean_state PREOP; then
  echo 'PREOP state with AL error flag was accepted' >&2
  exit 1
fi

STATE=OP
ERROR_FLAG=+
DIRECT_PREOP_BLOCK=0
PREOP_REQUESTS=0
quiesce_ethercat_slaves_before_release
[ "$STATE" = PREOP ]

STATE=SAFEOP
DIRECT_PREOP_BLOCK=1
PREOP_REQUESTS=0
quiesce_ethercat_slaves_before_release
[ "$STATE" = PREOP ]
[ "$PREOP_REQUESTS" -eq 2 ]

echo V5_ETHERCAT_BACKEND_LIFECYCLE_SMOKE_OK
