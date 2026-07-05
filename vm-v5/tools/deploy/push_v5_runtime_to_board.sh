#!/bin/sh
set -eu

repo_root="${V5_REPO_ROOT:-/root/Desktop/v5}"
manifest="${V5_DEPLOY_MANIFEST:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
remote_root="${V5_REMOTE_STAGING_ROOT:-/tmp/v5_runtime_deploy}"
apply=0

if [ "${1:-}" = "--apply" ]; then
  apply=1
fi

if [ ! -r "$manifest" ]; then
  echo "missing deploy manifest: $manifest" >&2
  exit 2
fi

"$repo_root/tools/deploy/precheck_v5_board.sh" "$manifest"

tab=$(printf '\t')
while IFS="$tab" read -r kind source destination mode extra; do
  case "$kind" in
    ''|'#'*) continue ;;
  esac
  if [ -n "${extra:-}" ]; then
    echo "bad manifest row: $kind $source $destination $mode ${extra:-}" >&2
    exit 3
  fi
  if [ ! -e "$repo_root/$source" ]; then
    echo "missing deploy source: $repo_root/$source" >&2
    exit 4
  fi
done < "$manifest"

if [ "$apply" -eq 0 ]; then
  echo "dry-run remote deploy bundle:"
  echo "  board: ${board_ssh:-<set V5_BOARD_SSH>} port=$board_ssh_port"
  echo "  staging: $remote_root"
  echo "  manifest: $manifest"
  echo "  command: V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply"
  exit 0
fi

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH is required for --apply" >&2
  exit 5
fi

if ! V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/precheck_v5_board.sh" "$manifest"; then
  echo "board precheck failed; deploy not started" >&2
  exit 6
fi

ssh -o BatchMode=yes -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "rm -rf '$remote_root' && mkdir -p '$remote_root'"
(
  cd "$repo_root"
  {
    printf '%s\n' tools/deploy/install_v5_runtime.sh config/deploy/v5_runtime_deploy_manifest.tsv
    awk -F '\t' 'NF && $1 !~ /^#/ { print $2 }' "$manifest"
  } | sort -u | tar -cf - -T -
) | ssh -o BatchMode=yes -p "$board_ssh_port" "$board_ssh" "tar -C '$remote_root' -xf -"
ssh -o BatchMode=yes -p "$board_ssh_port" "$board_ssh" "V5_REPO_ROOT='$remote_root' '$remote_root/tools/deploy/install_v5_runtime.sh' --apply"
