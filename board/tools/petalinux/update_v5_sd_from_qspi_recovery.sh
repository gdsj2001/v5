#!/bin/sh
set -eu

payload=
payload_sha256=
manifest_tool=
device=/dev/mmcblk0
apply=0
work_root=/run/v5_sd_recovery_update
boot_mount=$work_root/mount/boot
root_mount=$work_root/mount/rootfs

fail() {
    echo "V5_QSPI_SD_UPDATE_ERROR $*" >&2
    exit 1
}

usage() {
    echo "usage: $0 --payload <payload.tar.gz> --payload-sha256 <sha256> --manifest-tool <v5_product_file_manifest.py> [--device /dev/mmcblk0] --apply" >&2
    exit 2
}

cleanup() {
    sync || true
    mountpoint -q "$boot_mount" 2>/dev/null && umount "$boot_mount" || true
    mountpoint -q "$root_mount" 2>/dev/null && umount "$root_mount" || true
}

trap cleanup EXIT HUP INT TERM

while [ "$#" -gt 0 ]; do
    case "$1" in
        --payload)
            [ "$#" -ge 2 ] || usage
            payload=$2
            shift 2
            ;;
        --payload-sha256)
            [ "$#" -ge 2 ] || usage
            payload_sha256=$2
            shift 2
            ;;
        --manifest-tool)
            [ "$#" -ge 2 ] || usage
            manifest_tool=$2
            shift 2
            ;;
        --device)
            [ "$#" -ge 2 ] || usage
            device=$2
            shift 2
            ;;
        --apply)
            apply=1
            shift
            ;;
        *) usage ;;
    esac
done

[ "$apply" -eq 1 ] || usage
[ "$(id -u)" -eq 0 ] || fail "root is required"
[ -n "$payload" ] && [ -f "$payload" ] || fail "payload is missing: $payload"
[ -n "$payload_sha256" ] || fail "payload SHA-256 is required"
[ -n "$manifest_tool" ] && [ -f "$manifest_tool" ] || \
    fail "manifest verifier is missing: $manifest_tool"

for command_name in awk file find findmnt grep install lsblk mount mountpoint \
    python3 readlink rm sha256sum sort sync tar umount
do
    command -v "$command_name" >/dev/null 2>&1 || fail "missing command: $command_name"
done

grep -qw 'v5.recovery=qspi' /proc/cmdline || \
    fail "board is not running from the QSPI recovery path"

device=$(readlink -f "$device")
[ "$device" = /dev/mmcblk0 ] || fail "recovery target must be /dev/mmcblk0: $device"
[ -b "$device" ] || fail "target is not a block device: $device"
boot_partition=${device}p1
root_partition=${device}p2
[ -b "$boot_partition" ] && [ -b "$root_partition" ] || \
    fail "target SD partitions are missing"

root_source=$(readlink -f "$(findmnt -n -o SOURCE /)")
case "$root_source" in
    /dev/mmcblk1p*) ;;
    *) fail "QSPI recovery root must be on eMMC mmcblk1: $root_source" ;;
esac
case "$root_source" in
    "$device"*) fail "target SD is the current root device" ;;
esac

actual_payload_sha256=$(sha256sum "$payload" | awk '{print $1}')
[ "$actual_payload_sha256" = "$payload_sha256" ] || \
    fail "payload SHA-256 mismatch"

case "$work_root" in
    /run/v5_sd_recovery_update) ;;
    *) fail "unsafe work root: $work_root" ;;
esac
rm -rf "$work_root"
install -d "$work_root/payload" "$boot_mount" "$root_mount"

tar -tzf "$payload" | while IFS= read -r entry; do
    case "$entry" in
        boot|boot/*|rootfs|rootfs/*) ;;
        *) fail "unexpected payload path: $entry" ;;
    esac
    case "/$entry/" in
        */../*) fail "unsafe payload path: $entry" ;;
    esac
done
tar -xzf "$payload" -C "$work_root/payload"

payload_boot=$work_root/payload/boot
payload_root=$work_root/payload/rootfs
for name in BOOT.BIN boot.scr image.ub system.dtb \
    v5-rootfs-file-manifest.tsv v5-sd-manifest.txt
do
    [ -s "$payload_boot/$name" ] || fail "payload boot file is missing: $name"
done

PYTHONPATH="$payload_root/usr/lib/python3.7${PYTHONPATH:+:$PYTHONPATH}" \
python3 "$manifest_tool" verify \
    --root "$payload_root" \
    --manifest "$payload_boot/v5-rootfs-file-manifest.tsv" || \
    fail "payload rootfs manifest verification failed"
file "$payload_root/usr/libexec/8ax/v5_lvgl_shell" | grep -q 'ELF 32-bit.*ARM' || \
    fail "payload UI is not ARM"
[ -z "$(find "$payload_root" -xdev -type d -name .git -print -quit)" ] || \
    fail "payload rootfs contains Git metadata"

for partition in "$boot_partition" "$root_partition"; do
    findmnt -rn -S "$partition" -o TARGET | sort -r | while IFS= read -r target; do
        [ -n "$target" ] && umount "$target"
    done
done

mount -t vfat -o rw "$boot_partition" "$boot_mount"
mount -t ext4 -o rw "$root_partition" "$root_mount"
[ "$(readlink -f "$(findmnt -n -o SOURCE -T "$boot_mount")")" = "$boot_partition" ] || \
    fail "boot mount source mismatch"
[ "$(readlink -f "$(findmnt -n -o SOURCE -T "$root_mount")")" = "$root_partition" ] || \
    fail "rootfs mount source mismatch"
[ "$(findmnt -n -o FSTYPE -T "$boot_mount")" = vfat ] || fail "boot partition is not vfat"
[ "$(findmnt -n -o FSTYPE -T "$root_mount")" = ext4 ] || fail "rootfs partition is not ext4"

find "$boot_mount" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
find "$root_mount" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
tar -C "$payload_boot" -cpf - . | tar -C "$boot_mount" -xpf -
tar -C "$payload_root" -cpf - . | tar -C "$root_mount" -xpf -
sync

for name in BOOT.BIN boot.scr image.ub system.dtb \
    v5-rootfs-file-manifest.tsv v5-sd-manifest.txt
do
    expected=$(sha256sum "$payload_boot/$name" | awk '{print $1}')
    actual=$(sha256sum "$boot_mount/$name" | awk '{print $1}')
    [ "$actual" = "$expected" ] || fail "boot readback mismatch: $name"
done
PYTHONPATH="$root_mount/usr/lib/python3.7${PYTHONPATH:+:$PYTHONPATH}" \
python3 "$manifest_tool" verify \
    --root "$root_mount" \
    --manifest "$boot_mount/v5-rootfs-file-manifest.tsv" || \
    fail "written rootfs manifest verification failed"
file "$root_mount/usr/libexec/8ax/v5_lvgl_shell" | grep -q 'ELF 32-bit.*ARM' || \
    fail "written rootfs UI is not ARM"
[ -z "$(find "$root_mount" -xdev -type d -name .git -print -quit)" ] || \
    fail "written rootfs contains Git metadata"

sync
umount "$boot_mount"
umount "$root_mount"
trap - EXIT HUP INT TERM
echo "V5_QSPI_SD_UPDATE_OK device=$device payload_sha256=$payload_sha256"
