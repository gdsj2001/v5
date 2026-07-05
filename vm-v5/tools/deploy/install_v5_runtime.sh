#!/bin/sh
set -eu

repo_root="${V5_REPO_ROOT:-/root/Desktop/v5}"
manifest="${1:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
apply=0

if [ "${2:-}" = "--apply" ]; then
  apply=1
fi
if [ "${1:-}" = "--apply" ]; then
  manifest="$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv"
  apply=1
fi

if [ ! -r "$manifest" ]; then
  echo "missing deploy manifest: $manifest" >&2
  exit 2
fi

tab=$(printf '\t')
while IFS="$tab" read -r kind source destination mode extra; do
  case "$kind" in
    ''|'#'*) continue ;;
  esac
  if [ -n "${extra:-}" ] || [ -z "${source:-}" ] || [ -z "${destination:-}" ] || [ -z "${mode:-}" ]; then
    echo "bad manifest row: $kind $source $destination $mode ${extra:-}" >&2
    exit 3
  fi
  source_path="$repo_root/$source"
  if [ ! -e "$source_path" ]; then
    echo "missing deploy source: $source_path" >&2
    exit 4
  fi
  if [ "$apply" -eq 0 ]; then
    printf 'deploy %s %s -> %s mode=%s\n' "$kind" "$source_path" "$destination" "$mode"
    continue
  fi
  install -d "$(dirname "$destination")"
  install -m "$mode" "$source_path" "$destination"
done < "$manifest"

if [ "$apply" -eq 1 ]; then
  /etc/init.d/v5-state-publisher restart
  /etc/init.d/v5-linuxcnc-command-gate restart
  /etc/init.d/v5-ui-relay restart
else
  echo "dry-run only; pass --apply to install files and restart v5 init services"
fi
