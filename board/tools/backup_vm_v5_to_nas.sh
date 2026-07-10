#!/usr/bin/env bash
set -euo pipefail

# Non-source backup helper.
# Canonical source remains the Windows D:\v5 / D:\v5\board tree.
# This script runs on the VM and mirrors the VM source copy directly to NAS.

VM_SOURCE="${VM_SOURCE:-/root/Desktop/v5}"
NAS_SHARE="${NAS_SHARE:-//sjnas/备份}"
NAS_SUBDIR="${NAS_SUBDIR:-vm-v5}"
NAS_USER="${NAS_USER:-}"
NAS_PASS="${NAS_PASS:-}"
MOUNT_DIR="${MOUNT_DIR:-/mnt/v5_nas_backup}"
LOCK_FILE="${LOCK_FILE:-/tmp/backup_vm_v5_to_nas.lock}"

if [[ -z "$NAS_USER" || -z "$NAS_PASS" ]]; then
  echo "ERROR: NAS_USER and NAS_PASS are required." >&2
  exit 2
fi

if [[ ! -d "$VM_SOURCE" ]]; then
  echo "ERROR: VM source directory not found: $VM_SOURCE" >&2
  exit 3
fi

if [[ ! -e "$VM_SOURCE/AGENTS.md" && ! -d "$VM_SOURCE/board" ]]; then
  echo "ERROR: VM source marker not found under $VM_SOURCE" >&2
  exit 4
fi

for cmd in mount mountpoint umount rsync flock; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: required command not found on VM: $cmd" >&2
    exit 5
  fi
done

exec 9>"$LOCK_FILE"
if ! flock -n 9; then
  echo "ERROR: another VM-to-NAS backup is already running." >&2
  exit 6
fi

mounted_here=0
cred_file=""

cleanup() {
  if [[ -n "$cred_file" && -f "$cred_file" ]]; then
    rm -f "$cred_file"
  fi
  if [[ "$mounted_here" == "1" ]] && mountpoint -q "$MOUNT_DIR"; then
    umount "$MOUNT_DIR" || true
  fi
}
trap cleanup EXIT

mkdir -p "$MOUNT_DIR"

if ! mountpoint -q "$MOUNT_DIR"; then
  cred_file="$(mktemp /tmp/v5_nas_cred.XXXXXX)"
  chmod 600 "$cred_file"
  {
    printf 'username=%s\n' "$NAS_USER"
    printf 'password=%s\n' "$NAS_PASS"
  } > "$cred_file"

  if ! mount -t cifs "$NAS_SHARE" "$MOUNT_DIR" -o "credentials=$cred_file,iocharset=utf8,vers=3.0,noperm"; then
    mount -t cifs "$NAS_SHARE" "$MOUNT_DIR" -o "credentials=$cred_file,iocharset=utf8,noperm"
  fi
  mounted_here=1
fi

dest="$MOUNT_DIR/$NAS_SUBDIR"
mkdir -p "$dest"

touch "$dest/.write_test"
rm -f "$dest/.write_test"

echo "VM source: $VM_SOURCE"
echo "NAS target: $NAS_SHARE/$NAS_SUBDIR"
echo "Starting rsync mirror..."

rsync -aH --delete --delete-excluded --info=stats2,progress2 \
  --exclude='/.git/' \
  --exclude='/repo_ignored/' \
  --exclude='/.pytest_cache/' \
  --exclude='__pycache__/' \
  --exclude='/node_modules/' \
  --exclude='/.venv/' \
  --exclude='/build/' \
  --exclude='*.pyc' \
  "$VM_SOURCE"/ "$dest"/

cat > "$dest/_vm_backup_note.txt" <<EOF
backup_time_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
vm_source=$VM_SOURCE
nas_target=$NAS_SHARE/$NAS_SUBDIR
note=non-source backup; rebuild from Windows source truth and canonical VM build/deploy workflow
EOF

sync
echo "Backup complete: $NAS_SHARE/$NAS_SUBDIR"
