#!/bin/sh
set -eu

board_ssh="${V5_BOARD_SSH:-}"
port="${V5_BOARD_SSH_PORT:-22}"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --host)
      shift
      board_ssh="${1:-}"
      ;;
    --port)
      shift
      port="${1:-22}"
      ;;
    --help)
      echo "usage: diagnose_v5_board_ssh.sh [--host HOST] [--port 22]"
      echo "read-only network diagnostic; no deploy, screenshot, input, linuxcncrsh, or motion command is sent"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH or --host is required" >&2
  exit 3
fi

echo "diagnose board ssh: host=$board_ssh port=$port"

if command -v getent >/dev/null 2>&1; then
  if getent hosts "$board_ssh" >/tmp/v5_board_getent.out 2>&1; then
    sed 's/^/OK resolve: /' /tmp/v5_board_getent.out
  else
    echo "WARN resolve failed: $board_ssh"
  fi
  rm -f /tmp/v5_board_getent.out
fi

if command -v ping >/dev/null 2>&1; then
  if ping -c 1 -W 1 "$board_ssh" >/tmp/v5_board_ping.out 2>&1; then
    tail -n 2 /tmp/v5_board_ping.out | sed 's/^/OK ping: /'
  else
    echo "WARN ping failed: $board_ssh"
  fi
  rm -f /tmp/v5_board_ping.out
else
  echo "WARN ping command missing"
fi

if command -v nc >/dev/null 2>&1; then
  if nc -z -w 2 "$board_ssh" "$port" >/dev/null 2>&1; then
    echo "OK tcp port reachable: $board_ssh:$port"
  else
    echo "FAIL tcp port unreachable: $board_ssh:$port"
  fi
else
  echo "WARN nc command missing; skipping tcp port check"
fi

if ssh -o BatchMode=yes -o ConnectTimeout=5 -p "$port" "$board_ssh" 'true' >/tmp/v5_board_ssh.out 2>&1; then
  echo "OK ssh batch login: $board_ssh"
else
  echo "FAIL ssh batch login: $board_ssh"
  sed 's/^/INFO ssh: /' /tmp/v5_board_ssh.out
  rm -f /tmp/v5_board_ssh.out
  exit 1
fi
rm -f /tmp/v5_board_ssh.out
