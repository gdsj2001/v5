#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
repo_root="${V5_REPO_ROOT:-$default_repo_root}"
manifest="${V5_DEPLOY_MANIFEST:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
board_ssh_key="${V5_BOARD_SSH_KEY:-}"
board_ssh_key_opts=""
if [ -n "$board_ssh_key" ]; then
  board_ssh_key_opts="-i $board_ssh_key -o IdentitiesOnly=yes"
fi
board_ssh_user_known_hosts="${V5_BOARD_SSH_USER_KNOWN_HOSTS:-/dev/null}"
board_ssh_strict_host_key="${V5_BOARD_SSH_STRICT_HOST_KEY:-no}"
board_ssh_pubkey_rsa_opt=""
if ssh -G 127.0.0.1 2>/dev/null | grep -qi '^pubkeyacceptedalgorithms '; then
  board_ssh_pubkey_rsa_opt="-o PubkeyAcceptedAlgorithms=+ssh-rsa"
elif ssh -G 127.0.0.1 2>/dev/null | grep -qi '^pubkeyacceptedkeytypes '; then
  board_ssh_pubkey_rsa_opt="-o PubkeyAcceptedKeyTypes=+ssh-rsa"
fi
board_ssh_legacy_rsa_opts="-o HostKeyAlgorithms=+ssh-rsa $board_ssh_pubkey_rsa_opt -o StrictHostKeyChecking=$board_ssh_strict_host_key -o UserKnownHostsFile=$board_ssh_user_known_hosts"
scp_legacy_protocol_opt=""
if ! scp -O 2>&1 | grep -q "unknown option -- O"; then
  scp_legacy_protocol_opt="-O"
fi
remote_root="${V5_REMOTE_STAGING_ROOT:-/tmp/v5_runtime_deploy}"
apply=0
home_dir="${HOME:?HOME is required}"
build_root="${V5_BUILD_ROOT:-$home_dir/v5-build}"
temp_dir="${V5_DEPLOY_TEMP_DIR:-$build_root/temp}"
board_build_dir="${V5_BOARD_BUILD_DIR:-$build_root/board}"
linuxcnc_allowlist="$repo_root/linuxcnc/yocto/files/v5_linuxcnc_runtime_allowlist.tsv"
linuxcnc_verifier="$repo_root/tools/linuxcnc/verify_v5_linuxcnc_minimal_runtime.py"
linuxcnc_package_root="${V5_LINUXCNC_PACKAGE_ROOT:-}"
linuxcnc_bundle_enabled=0
linuxcnc_expected_count=0

manifest_source_path() {
  kind="$1"
  source="$2"
  case "$kind:$source" in
    binary:build/board/app/*)
      printf '%s/app/%s' "$board_build_dir" "${source#build/board/app/}"
      ;;
    *)
      printf '%s/%s' "$repo_root" "$source"
      ;;
  esac
}

copy_manifest_source_to_bundle() {
  kind="$1"
  source="$2"
  bundle_root="$3"
  source_path="$(manifest_source_path "$kind" "$source")"
  target_path="$bundle_root/$source"
  install -d "$(dirname "$target_path")"
  cp -p "$source_path" "$target_path"
}

for arg in "$@"; do
  case "$arg" in
    --apply)
      apply=1
      ;;
    --help)
      echo "usage: push_v5_runtime_to_board.sh [--apply]"
      echo "  --apply deploys the Windows-owned source bundle and VM build artifacts"
      exit 0
      ;;
    *)
      echo "unknown argument: $arg" >&2
      exit 2
      ;;
  esac
done

if [ ! -r "$manifest" ]; then
  echo "missing deploy manifest: $manifest" >&2
  exit 2
fi

if [ -n "$linuxcnc_package_root" ]; then
  linuxcnc_bundle_enabled=1
  if [ ! -d "$linuxcnc_package_root" ]; then
    echo "explicit LinuxCNC package root is missing: $linuxcnc_package_root" >&2
    exit 6
  fi
  if [ ! -r "$linuxcnc_allowlist" ] || [ ! -r "$linuxcnc_verifier" ]; then
    echo "LinuxCNC deploy verifier inputs are missing" >&2
    exit 6
  fi
  python3 "$linuxcnc_verifier" \
    --allowlist "$linuxcnc_allowlist" \
    --package-root "$linuxcnc_package_root"
  if find "$linuxcnc_package_root" -type l -print -quit | grep -q .; then
    echo "LinuxCNC deploy bundle does not allow symlinks" >&2
    exit 6
  fi
  linuxcnc_allowlist_count=$(awk -F '\t' '$0 !~ /^#/ && NF >= 1 {count++} END {print count + 0}' "$linuxcnc_allowlist")
  linuxcnc_expected_count=$((linuxcnc_allowlist_count + 4))
fi

V5_BOARD_BUILD_DIR="$board_build_dir" "$repo_root/tools/deploy/precheck_v5_board.sh" "$manifest"

tab=$(printf '\t')
while IFS="$tab" read -r kind source destination mode extra; do
  case "$kind" in
    ''|'#'*) continue ;;
  esac
  if [ -n "${extra:-}" ]; then
    echo "bad manifest row: $kind $source $destination $mode ${extra:-}" >&2
    exit 3
  fi
  source_path="$(manifest_source_path "$kind" "$source")"
  if [ ! -e "$source_path" ]; then
    echo "missing deploy source: $source_path" >&2
    exit 4
  fi
done < "$manifest"

if [ "$apply" -eq 0 ]; then
  echo "dry-run remote deploy bundle:"
  echo "  board: ${board_ssh:-<set V5_BOARD_SSH>} port=$board_ssh_port"
  echo "  source: $repo_root"
  echo "  build: $board_build_dir"
  if [ "$linuxcnc_bundle_enabled" -eq 1 ]; then
    echo "  linuxcnc package: $linuxcnc_package_root files=$linuxcnc_expected_count"
  else
    echo "  linuxcnc package: disabled (runtime-only fast path)"
  fi
  echo "  local temp: $temp_dir"
  echo "  staging: $remote_root"
  echo "  manifest: $manifest"
  echo "  transfer: local tar archive scp staging, then remote install"
  echo "  command: V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply"
  exit 0
fi

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH is required for --apply" >&2
  exit 5
fi

remote_sh() {
  ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 $board_ssh_legacy_rsa_opts $board_ssh_key_opts -p "$board_ssh_port" "$board_ssh" "$@"
}

archive_dir="$temp_dir"
archive="$archive_dir/v5_runtime_deploy_$$.tar"
bundle_dir="$archive_dir/v5_runtime_deploy_bundle_$$"
mkdir -p "$archive_dir"
rm -f "$archive"
rm -rf "$bundle_dir"
install -d "$bundle_dir/tools/deploy" "$bundle_dir/config/deploy"
cp -p "$repo_root/tools/deploy/install_v5_runtime.sh" "$bundle_dir/tools/deploy/install_v5_runtime.sh"
cp -p "$manifest" "$bundle_dir/config/deploy/v5_runtime_deploy_manifest.tsv"
if [ "$linuxcnc_bundle_enabled" -eq 1 ]; then
  cp -p "$linuxcnc_allowlist" "$bundle_dir/config/deploy/v5_linuxcnc_runtime_allowlist.tsv"
  install -d "$bundle_dir/linuxcnc-package-root"
  cp -a "$linuxcnc_package_root/." "$bundle_dir/linuxcnc-package-root/"
  (
    cd "$bundle_dir/linuxcnc-package-root"
    find . -type f -print | LC_ALL=C sort | while IFS= read -r runtime_file; do
      sha256sum "$runtime_file"
    done
  ) >"$bundle_dir/config/deploy/v5_linuxcnc_deploy_bundle.sha256"
  linuxcnc_bundle_count=$(wc -l <"$bundle_dir/config/deploy/v5_linuxcnc_deploy_bundle.sha256")
  if [ "$linuxcnc_bundle_count" -ne "$linuxcnc_expected_count" ]; then
    echo "unexpected LinuxCNC deploy bundle size: expected=$linuxcnc_expected_count actual=$linuxcnc_bundle_count" >&2
    exit 6
  fi
fi
tab=$(printf '\t')
while IFS="$tab" read -r kind source destination mode extra; do
  case "$kind" in
    ''|'#'*) continue ;;
  esac
  copy_manifest_source_to_bundle "$kind" "$source" "$bundle_dir"
done < "$manifest"
(cd "$bundle_dir" && tar -cf "$archive" .)
rm -rf "$bundle_dir"
remote_sh "rm -rf '$remote_root' && mkdir -p '$remote_root'"
scp $scp_legacy_protocol_opt -q $board_ssh_legacy_rsa_opts $board_ssh_key_opts -P "$board_ssh_port" "$archive" "$board_ssh:$remote_root/v5_runtime_deploy.tar"
rm -f "$archive"
remote_sh "tar -m -C '$remote_root' -xf '$remote_root/v5_runtime_deploy.tar' && rm -f '$remote_root/v5_runtime_deploy.tar'"
remote_sh "V5_REPO_ROOT='$remote_root' '$remote_root/tools/deploy/install_v5_runtime.sh' --apply"
remote_sh "rm -rf '$remote_root'"
