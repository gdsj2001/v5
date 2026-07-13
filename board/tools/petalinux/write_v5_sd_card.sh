#!/bin/sh
set -eu

source_mount=${VM_SOURCE_MOUNT_ROOT:-/mnt/v5-source}
build_root=${VM_BUILD_ROOT:-$HOME/v5-build}
device=
apply=0
stage_only=0
boot_mount=
root_mount=

fail() {
    echo "V5_SD_CARD_ERROR $*" >&2
    exit 1
}

usage() {
    echo "usage: $0 (--device /dev/<removable-disk> [--apply] | --stage-only)" >&2
    exit 2
}

cleanup() {
    sync || true
    if [ -n "$boot_mount" ] && mountpoint -q "$boot_mount"; then
        umount "$boot_mount" || true
    fi
    if [ -n "$root_mount" ] && mountpoint -q "$root_mount"; then
        umount "$root_mount" || true
    fi
}

trap cleanup EXIT HUP INT TERM

while [ "$#" -gt 0 ]; do
    case "$1" in
        --device)
            [ "$#" -ge 2 ] || usage
            device=$2
            shift 2
            ;;
        --apply)
            apply=1
            shift
            ;;
        --stage-only)
            stage_only=1
            shift
            ;;
        *)
            usage
            ;;
    esac
done

if [ "$stage_only" -eq 1 ]; then
    [ -z "$device" ] && [ "$apply" -eq 0 ] || usage
else
    [ -n "$device" ] || usage
fi
[ "$(id -u)" -eq 0 ] || fail "root is required"

for command_name in \
    awk blockdev cmake dumpimage e2fsck file find findmnt git grep install \
    lsblk mkfs.ext4 mkimage mount mountpoint partprobe python3 readelf readlink \
    sfdisk sha256sum sort tar udevadm umount wc wipefs
do
    command -v "$command_name" >/dev/null 2>&1 || fail "missing command: $command_name"
done

if command -v mkfs.vfat >/dev/null 2>&1; then
    mkfs_vfat=mkfs.vfat
elif command -v mkfs.fat >/dev/null 2>&1; then
    mkfs_vfat=mkfs.fat
else
    fail "missing command: mkfs.vfat"
fi

bootgen_cmd=${BOOTGEN:-}
if [ -z "$bootgen_cmd" ] && [ -n "${PETALINUX:-}" ]; then
    candidate=$PETALINUX/components/yocto/buildtools/sysroots/x86_64-petalinux-linux/usr/bin/bootgen
    [ -x "$candidate" ] && bootgen_cmd=$candidate
fi
[ -n "$bootgen_cmd" ] && [ -x "$bootgen_cmd" ] || \
    fail "bootgen is unavailable; source the PetaLinux settings or set BOOTGEN"

[ -d "$source_mount/board" ] || fail "Windows source mount is missing: $source_mount"
mount_type=$(findmnt -n -o FSTYPE -T "$source_mount/board")
mount_options=$(findmnt -n -o OPTIONS -T "$source_mount/board")
case "$mount_type" in
    fuse.vmhgfs-fuse|9p|cifs|nfs|nfs4|virtiofs) ;;
    *) fail "source owner is not on an approved shared mount: $mount_type" ;;
esac
case ",$mount_options," in
    *,ro,*) ;;
    *) fail "source mount is not read-only: $mount_options" ;;
esac

if [ "$stage_only" -eq 1 ]; then
    device=stage-only
    device_bytes=0
    removable=not-applicable
    device_model=staged-payload
    device_vendor=not-applicable
else
    device=$(readlink -f "$device")
    [ -b "$device" ] || fail "target is not a block device: $device"
    device_name=$(basename "$device")
    [ "$(lsblk -dn -o TYPE "$device" | tr -d ' ')" = "disk" ] || \
        fail "target must be a whole disk: $device"
    [ -r "/sys/class/block/$device_name/removable" ] || \
        fail "target has no removable identity: $device"
    [ "$(cat "/sys/class/block/$device_name/removable")" = "1" ] || \
        fail "target is not removable: $device"

    root_source=$(findmnt -n -o SOURCE /)
    root_parent=$(lsblk -no PKNAME "$root_source" 2>/dev/null | head -n 1)
    [ -n "$root_parent" ] || root_parent=$(basename "$root_source")
    [ "$device_name" != "$root_parent" ] || fail "target is the VM system disk: $device"

    device_bytes=$(blockdev --getsize64 "$device")
    [ "$device_bytes" -ge 8589934592 ] || fail "target is smaller than 8 GiB: $device_bytes"
    removable=$(cat "/sys/class/block/$device_name/removable")
    device_model=$(tr -d '\n' <"/sys/class/block/$device_name/device/model" 2>/dev/null || true)
    device_vendor=$(tr -d '\n' <"/sys/class/block/$device_name/device/vendor" 2>/dev/null || true)
fi

project_root=$source_mount
board_root=$project_root/board
peta_root=$board_root/petalinux
deploy_manifest=$board_root/config/deploy/v5_runtime_deploy_manifest.tsv
boot_template=$peta_root/project-spec/meta-user/recipes-bsp/u-boot/u-boot-zynq-scr/boot.cmd.default.ext4
bitstream=$peta_root/project-spec/hw-description/system.bit
peta_verify=$board_root/tools/petalinux/verify_v5_petalinux_source.py
linuxcnc_verify=$board_root/tools/linuxcnc/verify_v5_linuxcnc_source.py
product_closure_verify=$board_root/tools/deploy/verify_v5_product_source_closure.py
product_file_manifest_tool=$board_root/tools/deploy/v5_product_file_manifest.py

[ -r "$deploy_manifest" ] || fail "deploy manifest is missing"
[ -r "$boot_template" ] || fail "ext4 boot owner is missing"
[ -r "$bitstream" ] || fail "hardware bitstream is missing"
[ -r "$product_closure_verify" ] || fail "product source closure verifier is missing"
[ -r "$product_file_manifest_tool" ] || fail "product file manifest tool is missing"
python3 "$peta_verify" --project-root "$project_root" --source-root "$peta_root"
python3 "$linuxcnc_verify" \
    --project-root "$project_root" \
    --source-root "$project_root/linuxcnc" \
    --allow-flattened-symlinks

deploy_dir=$build_root/petalinux/output/tmp/deploy/images/zynq-generic
[ -d "$deploy_dir" ] || fail "PetaLinux deploy output is missing: $deploy_dir"

latest_file() {
    pattern=$1
    result=$(find "$deploy_dir" -maxdepth 1 -type f -name "$pattern" -print | sort | tail -n 1)
    [ -n "$result" ] && [ -f "$result" ] || fail "missing PetaLinux artifact: $pattern"
    printf '%s\n' "$result"
}

zimage=$(latest_file 'zImage--*.bin')
dtb=$(latest_file 'system-top.dtb')
rootfs_tar=$(latest_file 'petalinux-image-minimal-*.rootfs.tar.gz')
fsbl=$(latest_file 'fsbl-*.elf')
uboot=$(latest_file 'u-boot-*.elf')

recipe_root=$(find "$build_root/petalinux/output/tmp/work/zynq_generic-xilinx-linux-gnueabi/linux-xlnx" \
    -mindepth 1 -maxdepth 1 -type d -print | sort | tail -n 1)
[ -n "$recipe_root" ] || fail "kernel Yocto sysroot is missing"
sysroot=$recipe_root/recipe-sysroot
native_sysroot=$recipe_root/recipe-sysroot-native
cc=$native_sysroot/usr/bin/arm-xilinx-linux-gnueabi/arm-xilinx-linux-gnueabi-gcc
cxx=$native_sysroot/usr/bin/arm-xilinx-linux-gnueabi/arm-xilinx-linux-gnueabi-g++
[ -x "$cc" ] && [ -x "$cxx" ] && [ -d "$sysroot" ] || \
    fail "ARM cross toolchain is incomplete"

work_root=$build_root/sd-card
arm_build=$build_root/board-arm
rootfs_stage=$work_root/rootfs
boot_stage=$work_root/boot
mount_root=$work_root/mount
boot_mount=$mount_root/boot
root_mount=$mount_root/rootfs
toolchain=$work_root/arm-toolchain.cmake
rm -rf "$work_root"
install -d "$work_root" "$rootfs_stage" "$boot_stage" "$boot_mount" "$root_mount"

cat >"$toolchain" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER "$cc")
set(CMAKE_CXX_COMPILER "$cxx")
set(CMAKE_SYSROOT "$sysroot")
set(CMAKE_FIND_ROOT_PATH "$sysroot")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -mthumb")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -mthumb")
EOF

install -d "$arm_build"
python3 "$product_closure_verify" \
    --board-root "$board_root" \
    --build-dir "$arm_build" \
    --prepare-cmake-query
cmake -S "$board_root" -B "$arm_build" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE=Release
python3 "$product_closure_verify" \
    --board-root "$board_root" \
    --build-dir "$arm_build" \
    --validate-shell
cmake --build "$arm_build" --target v5_product_runtime -- -j2

binary_count=0
tab=$(printf '\t')
while IFS="$tab" read -r kind source destination mode extra; do
    [ "$kind" = "binary" ] || continue
    binary=$(basename "$source")
    path=$arm_build/app/$binary
    [ -x "$path" ] || fail "ARM build output is missing: $path"
    file "$path" | grep -q 'ELF 32-bit.*ARM' || fail "non-ARM product binary: $path"
    readelf -h "$path" | grep -q 'hard-float ABI' || fail "non-hard-float product binary: $path"
    binary_count=$(expr "$binary_count" + 1)
done <"$deploy_manifest"
[ "$binary_count" -gt 0 ] || fail "deploy manifest has no ARM product binaries"

tar -xpf "$rootfs_tar" -C "$rootfs_stage"

while IFS="$tab" read -r kind source destination mode extra; do
    case "$kind" in
        ''|'#'*) continue ;;
    esac
    [ -n "$source" ] && [ -n "$destination" ] && [ -n "$mode" ] && [ -z "${extra:-}" ] || \
        fail "bad deploy manifest row: $kind $source $destination $mode ${extra:-}"
    case "$kind" in
        binary)
            source_path=$arm_build/app/$(basename "$source")
            ;;
        *)
            source_path=$board_root/$source
            ;;
    esac
    [ -e "$source_path" ] || fail "manifest source is missing: $source_path"
    target=$rootfs_stage$destination
    install -d "$(dirname "$target")"
    install -m "$mode" "$source_path" "$target"
done <"$deploy_manifest"

enable_service() {
    name=$1
    start_prio=$2
    stop_prio=$3
    for level in 2 3 4 5; do
        dir=$rootfs_stage/etc/rc$level.d
        [ -d "$dir" ] || continue
        rm -f "$dir"/S??"$name"
        ln -s "../init.d/$name" "$dir/S${start_prio}${name}"
    done
    for level in 0 1 6; do
        dir=$rootfs_stage/etc/rc$level.d
        [ -d "$dir" ] || continue
        rm -f "$dir"/K??"$name"
        ln -s "../init.d/$name" "$dir/K${stop_prio}${name}"
    done
}

enable_service v5-linuxcnc-command-gate 91 19
enable_service v5-wcs-status-publisher 92 18
enable_service v5-state-publisher 93 17
enable_service v5-ui-relay 94 16
enable_service v5-settings-actiond 95 15
enable_service v5-touch-diagnostics 96 14

rm -f \
    "$rootfs_stage/opt/8ax/v5/config/settings/microkernel_parameter_table.tsv" \
    "$rootfs_stage/opt/8ax/v5/gcode/golden/cc.ngc" \
    "$rootfs_stage/usr/libexec/8ax/v5_rtcp_status_publisher.py" \
    "$rootfs_stage/usr/libexec/8ax/v5_g53_geometry_memory_owner.py" \
    "$rootfs_stage/usr/libexec/8ax/v5_native_safety_latch_owner.py" \
    "$rootfs_stage/etc/init.d/v5-rtcp-status-publisher" \
    "$rootfs_stage/etc/init.d/v5-g53-geometry-memory-owner"
for level in 0 1 2 3 4 5 6; do
    rm -f "$rootfs_stage/etc/rc${level}.d"/[SK]??v5-rtcp-status-publisher
    rm -f "$rootfs_stage/etc/rc${level}.d"/[SK]??v5-g53-geometry-memory-owner
done

install -d \
    "$rootfs_stage/opt/8ax/drive-profiles/public" \
    "$rootfs_stage/opt/8ax/drive-profiles/private"
public_profile=$rootfs_stage/opt/8ax/v5/config/drive-profiles/public/driver_profile_map.json
private_profile=$rootfs_stage/opt/8ax/v5/config/drive-profiles/private/535e661e9ea313143fed0d86e9d982368ca9a70c7062823e25560f34ceef7f9d_driver_profile_map.json
install -m 0644 "$public_profile" \
    "$rootfs_stage/opt/8ax/drive-profiles/public/driver_profile_map.json"
install -m 0644 "$private_profile" \
    "$rootfs_stage/opt/8ax/drive-profiles/private/driver_profile_map.json"
(
    cd "$rootfs_stage/opt/8ax/drive-profiles/public"
    sha256sum driver_profile_map.json >driver_profile_map.json.sha256
)
(
    cd "$rootfs_stage/opt/8ax/drive-profiles/private"
    sha256sum driver_profile_map.json >driver_profile_map.json.sha256
)

for required in \
    usr/bin/milltask \
    usr/lib/linuxcnc/modules/motmod.so \
    usr/libexec/8ax/v5_lvgl_shell \
    usr/libexec/8ax/v5_command_gate_server \
    opt/8ax/v5/config/settings/self_parameter_table.tsv \
    opt/8ax/v5/config/settings/drive_parameter_table.tsv \
    opt/8ax/v5/linuxcnc/ini/v5_bus.ini \
    etc/init.d/v5-ui-relay
do
    [ -s "$rootfs_stage/$required" ] || fail "assembled rootfs is missing: $required"
done
[ -z "$(find "$rootfs_stage" -xdev -type d -name .git -print -quit)" ] || \
    fail "assembled rootfs contains Git metadata"

cp "$zimage" "$boot_stage/zImage"
cp "$dtb" "$boot_stage/system.dtb"
cat >"$boot_stage/image.its" <<'EOF'
/dts-v1/;

/ {
    description = "V5 product SD kernel and device tree";
    #address-cells = <1>;

    images {
        kernel-1 {
            description = "V5 Linux kernel";
            data = /incbin/("zImage");
            type = "kernel";
            arch = "arm";
            os = "linux";
            compression = "none";
            load = <0x00200000>;
            entry = <0x00200000>;
            hash-1 { algo = "sha256"; };
        };
        fdt-1 {
            description = "V5 system device tree";
            data = /incbin/("system.dtb");
            type = "flat_dt";
            arch = "arm";
            compression = "none";
            hash-1 { algo = "sha256"; };
        };
    };

    configurations {
        default = "conf-1";
        conf-1 {
            description = "V5 external ext4 rootfs";
            kernel = "kernel-1";
            fdt = "fdt-1";
        };
    };
};
EOF
(
    cd "$boot_stage"
    mkimage -f image.its image.ub
)
dumpimage -l "$boot_stage/image.ub" | grep -q 'V5 external ext4 rootfs' || \
    fail "kernel-only FIT verification failed"
if dumpimage -l "$boot_stage/image.ub" | grep -q 'RAMDisk Image'; then
    fail "product FIT unexpectedly contains a ramdisk"
fi

sed \
    -e 's#@@PRE_BOOTENV@@##g' \
    -e 's#@@KERNEL_BOOTCMD@@#bootm#g' \
    -e 's#@@KERNEL_LOAD_ADDRESS@@#0x00200000#g' \
    -e 's#@@DEVICETREE_ADDRESS@@#0x00100000#g' \
    -e 's#@@KERNEL_IMAGE@@#uImage#g' \
    -e 's#@@QSPI_KERNEL_IMAGE@@#image.ub#g' \
    -e 's#@@NAND_KERNEL_IMAGE@@#image.ub#g' \
    -e 's#@@QSPI_FIT_IMAGE_LOAD_ADDRESS@@#0x10000000#g' \
    -e 's#@@FIT_IMAGE_LOAD_ADDRESS@@#0x10000000#g' \
    -e 's#@@QSPI_KERNEL_OFFSET@@#0x1000000#g' \
    -e 's#@@NAND_KERNEL_OFFSET@@#0x1000000#g' \
    -e 's#@@QSPI_KERNEL_SIZE@@#0x500000#g' \
    -e 's#@@NAND_KERNEL_SIZE@@#0x3200000#g' \
    -e 's#@@QSPI_FIT_IMAGE_SIZE@@#0xF00000#g' \
    -e 's#@@NAND_FIT_IMAGE_LOAD_ADDRESS@@#0x10000000#g' \
    -e 's#@@NAND_FIT_IMAGE_SIZE@@#0x6400000#g' \
    -e 's#@@FIT_IMAGE@@#image.ub#g' \
    "$boot_template" >"$boot_stage/boot.cmd"
if grep -q '@@' "$boot_stage/boot.cmd"; then
    fail "boot command still contains unresolved placeholders"
fi
mkimage -C none -A arm -T script -d "$boot_stage/boot.cmd" "$boot_stage/boot.scr"
dumpimage -p 0 -o "$boot_stage/boot.readback.cmd" "$boot_stage/boot.scr" >/dev/null
grep -q 'root=/dev/mmcblk0p2 rw rootwait' "$boot_stage/boot.readback.cmd" || \
    fail "boot script rootfs readback failed"
if grep -q 'bootm ramdisk\|root=/dev/ram0' "$boot_stage/boot.readback.cmd"; then
    fail "retired initrd boot path survived"
fi

cat >"$boot_stage/boot.bif" <<EOF
the_ROM_image:
{
    [bootloader] $fsbl
    $bitstream
    $uboot
}
EOF
"$bootgen_cmd" -arch zynq -image "$boot_stage/boot.bif" \
    -o "$boot_stage/BOOT.BIN" -w on
[ -s "$boot_stage/BOOT.BIN" ] || fail "BOOT.BIN generation failed"

rootfs_file_manifest=$boot_stage/v5-rootfs-file-manifest.tsv
python3 "$product_file_manifest_tool" create \
    --root "$rootfs_stage" \
    --manifest "$rootfs_file_manifest"
rootfs_manifest_entries=$(grep -vc '^#' "$rootfs_file_manifest")
rootfs_manifest_sha256=$(sha256sum "$rootfs_file_manifest" | awk '{print $1}')

source_commit=$(git -C "$project_root" rev-parse HEAD)
peta_identity=$(sha256sum "$peta_root/v5_petalinux_source_identity.json" | awk '{print $1}')
linuxcnc_identity=$(sha256sum "$project_root/linuxcnc/v5_linuxcnc_source_identity.json" | awk '{print $1}')
rootfs_files=$(find "$rootfs_stage" -xdev -type f | wc -l)
cat >"$boot_stage/v5-sd-manifest.txt" <<EOF
schema=v5-sd-card-build-v2
source_commit=$source_commit
target_device=$device
target_bytes=$device_bytes
target_removable=$removable
target_vendor=$device_vendor
target_model=$device_model
peta_identity_sha256=$peta_identity
linuxcnc_identity_sha256=$linuxcnc_identity
rootfs_file_count=$rootfs_files
rootfs_manifest_entries=$rootfs_manifest_entries
rootfs_manifest_sha256=$rootfs_manifest_sha256
EOF
(
    cd "$boot_stage"
    sha256sum BOOT.BIN boot.scr image.ub system.dtb v5-rootfs-file-manifest.tsv >>v5-sd-manifest.txt
)

if [ "$stage_only" -eq 1 ]; then
    echo "V5_SD_PAYLOAD_STAGE_OK boot=$boot_stage rootfs=$rootfs_stage"
    exit 0
fi

if [ "$apply" -ne 1 ]; then
    echo "V5_SD_CARD_DRY_RUN_OK device=$device bytes=$device_bytes model=$device_model"
    echo "Run with --apply to erase and write the removable disk."
    exit 0
fi

for partition in $(lsblk -ln -o NAME,TYPE "$device" | awk '$2 == "part" {print "/dev/" $1}'); do
    while target=$(findmnt -rn -S "$partition" -o TARGET | head -n 1) && [ -n "$target" ]; do
        umount "$target"
    done
done

wipefs -af "$device"
sfdisk --wipe always "$device" <<EOF
label: dos
unit: sectors

start=2048, size=4194304, type=c, bootable
start=4196352, type=83
EOF
partprobe "$device"
udevadm settle

case "$device" in
    /dev/mmcblk*|/dev/nvme*) boot_partition=${device}p1; root_partition=${device}p2 ;;
    *) boot_partition=${device}1; root_partition=${device}2 ;;
esac
[ -b "$boot_partition" ] && [ -b "$root_partition" ] || \
    fail "partition nodes were not created"

for partition in "$boot_partition" "$root_partition"; do
    while target=$(findmnt -rn -S "$partition" -o TARGET | head -n 1) && [ -n "$target" ]; do
        umount "$target"
    done
done

"$mkfs_vfat" -F 32 -n V5_BOOT "$boot_partition"
mkfs.ext4 -F -L rootfs -m 0 "$root_partition"
udevadm settle

for partition in "$boot_partition" "$root_partition"; do
    while target=$(findmnt -rn -S "$partition" -o TARGET | head -n 1) && [ -n "$target" ]; do
        umount "$target"
    done
done

mount "$boot_partition" "$boot_mount"
mount "$root_partition" "$root_mount"
install -m 0644 \
    "$boot_stage/BOOT.BIN" \
    "$boot_stage/boot.scr" \
    "$boot_stage/image.ub" \
    "$boot_stage/system.dtb" \
    "$boot_stage/v5-rootfs-file-manifest.tsv" \
    "$boot_stage/v5-sd-manifest.txt" \
    "$boot_mount/"
tar -C "$rootfs_stage" -cpf - . | tar -C "$root_mount" -xpf -
sync

for name in BOOT.BIN boot.scr image.ub system.dtb v5-rootfs-file-manifest.tsv v5-sd-manifest.txt; do
    expected=$(sha256sum "$boot_stage/$name" | awk '{print $1}')
    actual=$(sha256sum "$boot_mount/$name" | awk '{print $1}')
    [ "$actual" = "$expected" ] || fail "boot readback mismatch: $name"
done
python3 "$product_file_manifest_tool" verify \
    --root "$root_mount" \
    --manifest "$boot_mount/v5-rootfs-file-manifest.tsv"
for required in \
    usr/bin/milltask \
    usr/lib/linuxcnc/modules/motmod.so \
    usr/libexec/8ax/v5_lvgl_shell \
    usr/libexec/8ax/v5_command_gate_server \
    opt/8ax/v5/config/settings/self_parameter_table.tsv \
    opt/8ax/v5/linuxcnc/ini/v5_bus.ini \
    etc/init.d/v5-ui-relay
do
    [ -s "$root_mount/$required" ] || fail "rootfs readback is missing: $required"
done
file "$root_mount/usr/libexec/8ax/v5_lvgl_shell" | grep -q 'ELF 32-bit.*ARM' || \
    fail "rootfs UI binary readback is not ARM"
[ -z "$(find "$root_mount" -xdev -type d -name .git -print -quit)" ] || \
    fail "written rootfs contains Git metadata"

sync
umount "$boot_mount"
umount "$root_mount"
boot_mount=
root_mount=
e2fsck -fn "$root_partition"
if command -v fsck.vfat >/dev/null 2>&1; then
    fsck.vfat -n "$boot_partition"
fi

echo "V5_SD_CARD_READY device=$device bytes=$device_bytes model=$device_model source_commit=$source_commit"
sfdisk -d "$device"
lsblk -o NAME,RM,RO,SIZE,TYPE,FSTYPE,LABEL,UUID,MOUNTPOINT "$device"
trap - EXIT HUP INT TERM
