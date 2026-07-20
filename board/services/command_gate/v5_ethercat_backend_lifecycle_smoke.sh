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
DIRECT_PREOP_BLOCK=0
PREOP_REQUESTS=0

backend_residue_running() {
  return 1
}

sleep() {
  return 0
}

dmesg() {
  [ "$DMESG_FAIL" = "0" ] || return 1
  i=0
  while [ "$i" -lt "$FAULT_COUNT" ]; do
    echo 'EtherCAT ERROR: datagrams UNMATCHED'
    i=$((i + 1))
  done
}

ethercat() {
  command_name=${1:-}
  case "$command_name" in
    master)
      printf 'Phase: %s\n' "$MASTER_PHASE"
      echo 'Active: no'
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
        *) return 1 ;;
      esac
      ;;
    *) return 1 ;;
  esac
}

. "$SCRIPT_DIR/v5_ethercat_backend_lifecycle.sh"

ethercat_transport_scanned
FAULT_COUNT=9
capture_ethercat_attach_fault_baseline
[ "$(cat "$ETHERCAT_ATTACH_FAULT_BASELINE")" = 9 ]

# A hot restart may add one diagnostic UNMATCHED event while resident OP/WKC/DC
# actuals remain healthy.  The lifecycle library must not contain a dmesg-count
# ready gate; native readiness is owned by v5_native_hal_owner instead.
FAULT_COUNT=10
[ "$(ethercat_kernel_fault_count)" = 10 ]
if grep -Eq 'ethercat_no_post_attach_faults|ethercat_backend_ready|ethercat_domain_wkc_ready|ethercat_reference_clock_healthy|halcmd getp|ethercat domains' \
    "$SCRIPT_DIR/v5_ethercat_backend_lifecycle.sh"; then
  echo 'retired shell runtime readiness gate was restored' >&2
  exit 1
fi

DMESG_FAIL=1
if ethercat_kernel_fault_count >/dev/null; then
  echo 'unreadable diagnostic kernel log was accepted' >&2
  exit 1
fi
DMESG_FAIL=0

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
