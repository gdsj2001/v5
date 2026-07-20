#!/bin/sh
set -eu

payload=
payload_sha256=
manifest_tool=
boot_script=
boot_script_sha256=
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
    echo "usage: $0 (--payload <payload.tar.gz> --payload-sha256 <sha256> --manifest-tool <v5_product_file_manifest.py> | --boot-script <boot.scr> --boot-script-sha256 <sha256>) [--device /dev/mmcblk0] --apply" >&2
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
        --boot-script)
            [ "$#" -ge 2 ] || usage
            boot_script=$2
            shift 2
            ;;
        --boot-script-sha256)
            [ "$#" -ge 2 ] || usage
            boot_script_sha256=$2
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
if [ -n "$boot_script" ] || [ -n "$boot_script_sha256" ]; then
    [ -z "$payload" ] && [ -z "$payload_sha256" ] && [ -z "$manifest_tool" ] || usage
    [ -n "$boot_script" ] && [ -f "$boot_script" ] || \
        fail "boot script is missing: $boot_script"
    [ -n "$boot_script_sha256" ] || fail "boot script SHA-256 is required"
    focused_boot_update=1
else
    [ -n "$payload" ] && [ -f "$payload" ] || fail "payload is missing: $payload"
    [ -n "$payload_sha256" ] || fail "payload SHA-256 is required"
    [ -n "$manifest_tool" ] && [ -f "$manifest_tool" ] || \
        fail "manifest verifier is missing: $manifest_tool"
    focused_boot_update=0
fi

if [ "$focused_boot_update" -eq 1 ]; then
    required_commands="awk findmnt grep install lsblk mount mountpoint mv python3 readlink rm sha256sum sort sync umount"
else
    required_commands="awk file find findmnt grep install lsblk mount mountpoint \
python3 readlink rm sha256sum sort sync tar umount"
fi
for command_name in $required_commands
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

if [ "$focused_boot_update" -eq 1 ]; then
    actual_boot_script_sha256=$(sha256sum "$boot_script" | awk '{print $1}')
    [ "$actual_boot_script_sha256" = "$boot_script_sha256" ] || \
        fail "boot script SHA-256 mismatch"
    python3 - "$boot_script" <<'PY'
import struct
import sys
import zlib
from pathlib import Path

path = Path(sys.argv[1])
blob = path.read_bytes()
header_struct = struct.Struct(">7I4B32s")
if len(blob) < header_struct.size:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR boot script header is truncated")
magic, hcrc, _time, size, _load, _entry, dcrc, _os, arch, image_type, comp, _name = \
    header_struct.unpack_from(blob)
if magic != 0x27051956 or arch != 2 or image_type != 6 or comp != 0:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR boot script image identity is invalid")
header = bytearray(blob[:header_struct.size])
header[4:8] = b"\0\0\0\0"
if zlib.crc32(header) & 0xffffffff != hcrc:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR boot script header CRC is invalid")
payload = blob[header_struct.size:header_struct.size + size]
if len(payload) != size or zlib.crc32(payload) & 0xffffffff != dcrc:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR boot script data CRC is invalid")
text = payload.decode("utf-8", "ignore")
product = [line for line in text.splitlines()
           if line.lstrip().startswith("setenv bootargs ")
           and "root=/dev/mmcblk0p2" in line]
recovery = [line for line in text.splitlines()
            if line.lstrip().startswith("setenv bootargs ")
            and "root=/dev/mmcblk1p1" in line]
product_isolcpus = ([token for token in product[0].split()
                    if token.startswith("isolcpus=")]
                   if len(product) == 1 else [])
recovery_isolcpus = ([token for token in recovery[0].split()
                     if token.startswith("isolcpus=")]
                    if len(recovery) == 1 else ["invalid-recovery-line-count"])
if len(product) != 1:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR product bootargs line count is invalid")
if len(recovery) != 1:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR QSPI recovery bootargs line count is invalid")
if product_isolcpus:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR product bootargs must not isolate the ARM boot CPU")
if recovery_isolcpus:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR QSPI recovery bootargs must not isolate a CPU")
if "root=/dev/ram0" in text or "bootm ramdisk" in text:
    raise SystemExit("V5_QSPI_SD_UPDATE_ERROR retired initrd boot path survived")
print("V5_QSPI_BOOT_SCRIPT_INPUT_OK header_crc=valid data_crc=valid product_isolated=0 recovery_isolated=0")
PY

    for partition in "$boot_partition" "$root_partition"; do
        findmnt -rn -S "$partition" -o TARGET | sort -r | while IFS= read -r target; do
            [ -n "$target" ] && umount "$target"
        done
    done
    if findmnt -rn -S "$root_partition" -o TARGET | grep -q .; then
        fail "target rootfs partition remained mounted"
    fi
    case "$work_root" in
        /run/v5_sd_recovery_update) ;;
        *) fail "unsafe work root: $work_root" ;;
    esac
    rm -rf "$work_root"
    install -d "$boot_mount"
    mount -t vfat -o rw "$boot_partition" "$boot_mount"
    [ "$(readlink -f "$(findmnt -n -o SOURCE -T "$boot_mount")")" = "$boot_partition" ] || \
        fail "boot mount source mismatch"
    [ "$(findmnt -n -o FSTYPE -T "$boot_mount")" = vfat ] || \
        fail "boot partition is not vfat"
    install -m 0644 "$boot_script" "$boot_mount/boot.scr.new"
    sync
    mv -f "$boot_mount/boot.scr.new" "$boot_mount/boot.scr"
    sync
    written_boot_script_sha256=$(sha256sum "$boot_mount/boot.scr" | awk '{print $1}')
    [ "$written_boot_script_sha256" = "$boot_script_sha256" ] || \
        fail "written boot script SHA-256 mismatch"
    echo "V5_QSPI_BOOT_SCRIPT_UPDATE_OK device=$device boot_sha256=$written_boot_script_sha256 rootfs=untouched"
    exit 0
fi

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
