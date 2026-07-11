#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
integration_root="$project_root/board/linuxcnc"
source_root="$project_root/linuxcnc"
petalinux_overlay_tool="$project_root/board/tools/petalinux/v5_petalinux_overlay.sh"
build_root=${VM_BUILD_ROOT:-${V5_BUILD_ROOT:-$HOME/v5-build}}
petalinux_root="$build_root/petalinux/overlay/merged"
artifact_dir=${V5_LINUXCNC_ARTIFACT_DIR:-$build_root/board/linuxcnc-native}
build_user=${V5_PETALINUX_BUILD_USER:-}
downloads_root="$build_root/petalinux/output/downloads"
overlay_root="$build_root/linuxcnc-overlay"
overlay_upper="$overlay_root/upper"
overlay_work="$overlay_root/work"
overlay_merged="$overlay_root/merged"
recipe_target=""
artifact_stage=""
petalinux_overlay_active=0

if [ "$#" -ne 0 ]; then
    echo "usage: V5_PETALINUX_BUILD_USER=<non-root-user> $0" >&2
    exit 2
fi
build_root=$(mkdir -p "$build_root" && CDPATH= cd -- "$build_root" && pwd)
petalinux_root="$build_root/petalinux/overlay/merged"
downloads_root="$build_root/petalinux/output/downloads"

command -v findmnt >/dev/null 2>&1 || {
    echo "findmnt is required to validate the Windows source mount" >&2
    exit 3
}
command -v petalinux-build >/dev/null 2>&1 || {
    echo "petalinux-build is not in PATH; source the PetaLinux settings first" >&2
    exit 4
}
if [ "$(id -u)" -ne 0 ]; then
    echo "root is required to create the read-only-source overlay" >&2
    exit 5
fi
if [ -z "$build_user" ] || ! id "$build_user" >/dev/null 2>&1; then
    echo "V5_PETALINUX_BUILD_USER must name an existing non-root build user" >&2
    exit 6
fi
if [ "$(id -u "$build_user")" -eq 0 ]; then
    echo "V5_PETALINUX_BUILD_USER must not be root" >&2
    exit 7
fi
mkdir -p "$downloads_root"
chown "$build_user":"$(id -gn "$build_user")" "$downloads_root"
if [ ! -f "$source_root/v5_linuxcnc_source_identity.json" ]; then
    echo "Windows-owned LinuxCNC source identity is missing: $source_root" >&2
    exit 8
fi
if [ ! -f "$petalinux_overlay_tool" ]; then
    echo "canonical PetaLinux overlay tool is missing: $petalinux_overlay_tool" >&2
    exit 8
fi

mount_info=$(findmnt -n -o FSTYPE,OPTIONS -T "$project_root")
case "$mount_info" in
    *fuse.vmhgfs-fuse*ro*) ;;
    *) echo "project source is not a read-only Windows shared mount: $mount_info" >&2; exit 9 ;;
esac
case "$artifact_dir" in
    "$build_root"/*) ;;
    *) echo "artifact directory must stay under V5_BUILD_ROOT: $artifact_dir" >&2; exit 10 ;;
esac
case "$overlay_root" in
    "$build_root"/*) ;;
    *) echo "overlay directory escaped V5_BUILD_ROOT: $overlay_root" >&2; exit 11 ;;
esac

run_petalinux_overlay() {
    VM_SOURCE_MOUNT_ROOT="$project_root" \
    VM_BUILD_ROOT="$build_root" \
    V5_PETALINUX_BUILD_USER="$build_user" \
        sh "$petalinux_overlay_tool" "$1"
}

ensure_build_memory() {
    required_kib=8388608
    total_kib=$(awk '
        $1 == "MemTotal:" || $1 == "SwapTotal:" { total += $2 }
        END { print total + 0 }
    ' /proc/meminfo)
    if [ "$total_kib" -ge "$required_kib" ]; then
        echo "V5_BUILD_MEMORY_OK total_kib=$total_kib"
        return
    fi

    for command_name in fallocate mkswap swapon; do
        command -v "$command_name" >/dev/null 2>&1 || {
            echo "$command_name is required to provision build-local swap" >&2
            exit 12
        }
    done
    swap_file="$build_root/petalinux/v5-build.swap"
    if [ ! -f "$swap_file" ]; then
        mkdir -p "$(dirname "$swap_file")"
        fallocate -l 8G "$swap_file"
        chmod 0600 "$swap_file"
        mkswap "$swap_file" >/dev/null
    fi
    chmod 0600 "$swap_file"
    if ! swapon --noheadings --show=NAME | grep -Fqx "$swap_file"; then
        swapon "$swap_file"
    fi
    total_kib=$(awk '
        $1 == "MemTotal:" || $1 == "SwapTotal:" { total += $2 }
        END { print total + 0 }
    ' /proc/meminfo)
    [ "$total_kib" -ge "$required_kib" ] || {
        echo "build memory remains below required capacity: $total_kib KiB" >&2
        exit 12
    }
    echo "V5_BUILD_MEMORY_OK total_kib=$total_kib swap=$swap_file"
}

cleanup() {
    if mountpoint -q "$overlay_merged"; then
        umount "$overlay_merged" || true
    fi
    case "$overlay_root" in "$build_root"/*) rm -rf "$overlay_root" ;; esac
    case "$recipe_target" in
        "$petalinux_root"/project-spec/meta-user/recipes-apps/linuxcnc-prebuilt)
            rm -rf "$recipe_target"
            ;;
    esac
    if [ -n "$artifact_stage" ] && [ -e "$artifact_stage" ]; then
        case "$artifact_stage" in "$build_root"/*) rm -rf "$artifact_stage" ;; esac
    fi
    if [ "$petalinux_overlay_active" -eq 1 ]; then
        run_petalinux_overlay clean >/dev/null 2>&1 || true
        petalinux_overlay_active=0
    fi
}
trap cleanup EXIT HUP INT TERM

ensure_build_memory
cleanup
run_petalinux_overlay clean >/dev/null
petalinux_overlay_active=1
run_petalinux_overlay prepare
petalinux_root=$(CDPATH= cd -- "$petalinux_root" && pwd)
if [ ! -f "$petalinux_root/project-spec/configs/config" ]; then
    echo "prepared PetaLinux overlay is incomplete: $petalinux_root" >&2
    exit 12
fi
recipe_target="$petalinux_root/project-spec/meta-user/recipes-apps/linuxcnc-prebuilt"
mkdir -p "$overlay_upper" "$overlay_work" "$overlay_merged" "$recipe_target"
chown "$build_user":"$(id -gn "$build_user")" \
    "$overlay_root" "$overlay_upper" "$overlay_work" "$overlay_merged"
mount -t overlay overlay \
    -o "lowerdir=$source_root,upperdir=$overlay_upper,workdir=$overlay_work" \
    "$overlay_merged"
python3 "$script_dir/verify_v5_linuxcnc_source.py" \
    --project-root "$project_root" \
    --source-root "$source_root" \
    --allow-flattened-symlinks \
    --materialize-symlinks "$overlay_merged"
ln -s "$integration_root/yocto/linuxcnc-prebuilt.bb" "$recipe_target/linuxcnc-prebuilt.bb"

run_petalinux_build() {
    task_args=$1
    build_home=$(getent passwd "$build_user" | cut -d: -f6)
    runuser -u "$build_user" -- env \
        HOME="$build_home" \
        PETALINUX="${PETALINUX:-}" \
        PETALINUX_VER="${PETALINUX_VER:-}" \
        XSCT_TOOLCHAIN="${XSCT_TOOLCHAIN:-}" \
        PATH="$PATH" \
        SHELL=/bin/bash \
        TERM="${TERM:-dumb}" \
        BB_ENV_EXTRAWHITE="${BB_ENV_EXTRAWHITE:-} V5_LINUXCNC_EXTERNAL_SOURCE" \
        V5_LINUXCNC_EXTERNAL_SOURCE="$overlay_merged" \
        sh -c "cd '$petalinux_root' && petalinux-build $task_args"
}

configure_persistent_downloads() {
    for conf in \
        "$petalinux_root/build/conf/local.conf" \
        "$petalinux_root/build/conf/plnxtool.conf"
    do
        [ -f "$conf" ] || {
            echo "generated PetaLinux download owner is missing: $conf" >&2
            exit 12
        }
        temp_conf="$conf.v5-downloads"
        if ! awk -v path="$downloads_root" '
            BEGIN { found = 0 }
            /^DL_DIR[[:space:]]*=/ {
                print "DL_DIR = \"" path "\""
                found = 1
                next
            }
            { print }
            END { if (!found) exit 42 }
        ' "$conf" > "$temp_conf"; then
            rm -f "$temp_conf"
            echo "generated PetaLinux config has no DL_DIR owner: $conf" >&2
            exit 12
        fi
        mv "$temp_conf" "$conf"
        chown "$build_user":"$(id -gn "$build_user")" "$conf"
        grep -Fqx "DL_DIR = \"$downloads_root\"" "$conf" || {
            echo "failed to redirect generated PetaLinux download owner: $conf" >&2
            exit 12
        }
    done
    echo "V5_PETALINUX_DOWNLOAD_OWNER_OK path=$downloads_root"
}

run_petalinux_build "-c kernel -x clean"
configure_persistent_downloads
run_petalinux_build "-c linuxcnc-prebuilt"
configure_persistent_downloads
run_petalinux_build ""

work_root="$build_root/petalinux/output/tmp/work"
motmod=$(find "$work_root" -type f -path '*/linuxcnc-prebuilt/*/image/usr/lib/linuxcnc/modules/motmod.so' -print | sort | tail -n 1)
if [ -z "$motmod" ]; then
    echo "built LinuxCNC image root was not found under $work_root" >&2
    exit 12
fi
image_root=${motmod%/usr/lib/linuxcnc/modules/motmod.so}

artifact_stage="$artifact_dir.v5-new.$$"
rm -rf "$artifact_stage"
install -d "$artifact_stage"
install -m 0755 "$image_root/usr/lib/linuxcnc/modules/motmod.so" "$artifact_stage/motmod.so"
install -m 0755 "$image_root/usr/bin/milltask" "$artifact_stage/milltask"
install -m 0644 "$image_root/usr/share/v5-native/linuxcnc-source-identity.txt" \
    "$artifact_stage/linuxcnc-source-identity.txt"
install -m 0644 "$image_root/usr/share/v5-native/v5_linuxcnc_source_identity.json" \
    "$artifact_stage/v5_linuxcnc_source_identity.json"
install -m 0644 "$project_root/board/petalinux/v5_petalinux_source_identity.json" \
    "$artifact_stage/v5_petalinux_source_identity.json"

petalinux_images="$petalinux_root/images/linux"
if [ ! -d "$petalinux_images" ]; then
    echo "PetaLinux image output is missing: $petalinux_images" >&2
    exit 13
fi
install -d "$artifact_stage/petalinux-images"
image_count=0
for image in "$petalinux_images"/*; do
    [ -f "$image" ] || continue
    install -m 0644 "$image" "$artifact_stage/petalinux-images/$(basename "$image")"
    image_count=$((image_count + 1))
done
if [ "$image_count" -eq 0 ]; then
    echo "PetaLinux image output contains no regular artifacts: $petalinux_images" >&2
    exit 14
fi
(
    cd "$artifact_stage"
    find petalinux-images -type f -print | sort | while IFS= read -r image; do
        sha256sum "$image"
    done
) > "$artifact_stage/petalinux-image-sha256.txt"

python3 "$script_dir/verify_v5_linuxcnc_source.py" \
    --project-root "$project_root" \
    --source-root "$source_root" \
    --allow-flattened-symlinks \
    --artifact-root "$image_root" \
    --write-artifact-identity "$artifact_stage/v5_linuxcnc_artifact_identity.json"

run_petalinux_build "-c linuxcnc-prebuilt -x clean"
rm -rf "$artifact_dir"
mv "$artifact_stage" "$artifact_dir"
artifact_stage=""
cleanup
trap - EXIT HUP INT TERM
echo "V5_LINUXCNC_BUILD_OK artifact_dir=$artifact_dir petalinux_images=$image_count"
