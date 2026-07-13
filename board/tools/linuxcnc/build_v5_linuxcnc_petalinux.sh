#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
integration_root="$project_root/board/linuxcnc"
source_root="$project_root/linuxcnc"
petalinux_overlay_tool="$project_root/board/tools/petalinux/v5_petalinux_overlay.sh"
source_package_verifier="$project_root/board/tools/petalinux/verify_v5_source_packages.py"
source_package_root="$project_root/board/third_party/petalinux-source-packages"
minimal_runtime_verifier="$project_root/board/tools/linuxcnc/verify_v5_linuxcnc_minimal_runtime.py"
runtime_allowlist="$integration_root/yocto/files/v5_linuxcnc_runtime_allowlist.tsv"
build_root=${VM_BUILD_ROOT:-${V5_BUILD_ROOT:-$HOME/v5-build}}
petalinux_root="$build_root/petalinux/overlay/merged"
artifact_dir=${V5_LINUXCNC_ARTIFACT_DIR:-$build_root/board/linuxcnc-native}
build_user=${V5_PETALINUX_BUILD_USER:-}
downloads_root="$build_root/petalinux/cache/downloads"
missing_source_report="$build_root/petalinux/v5-missing-source-inputs.json"
overlay_root="$build_root/linuxcnc-overlay"
overlay_upper="$overlay_root/upper"
overlay_work="$overlay_root/work"
overlay_merged="$overlay_root/merged"
source_projection_root=${VM_SOURCE_PROJECTION_ROOT:-$build_root/temp_source/current}
linuxcnc_projection="$source_projection_root/linuxcnc"
linuxcnc_projection_state="$source_projection_root/.linuxcnc-source-identity"
recipe_target=""
artifact_stage=""
rootfs_gate_marker=""
petalinux_overlay_active=0
build_mode=focused
clean_kernel=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --focused) build_mode=focused ;;
        --full) build_mode=full ;;
        --clean-kernel) clean_kernel=1 ;;
        *)
            echo "usage: V5_PETALINUX_BUILD_USER=<non-root-user> $0 [--focused|--full] [--clean-kernel]" >&2
            exit 2
            ;;
    esac
    shift
done
if [ "$clean_kernel" -eq 1 ] && [ "$build_mode" != full ]; then
    echo "--clean-kernel is only valid with the one final --full build" >&2
    exit 2
fi
build_root=$(mkdir -p "$build_root" && CDPATH= cd -- "$build_root" && pwd)
petalinux_root="$build_root/petalinux/overlay/merged"
downloads_root="$build_root/petalinux/cache/downloads"
source_projection_root=${VM_SOURCE_PROJECTION_ROOT:-$build_root/temp_source/current}
linuxcnc_projection="$source_projection_root/linuxcnc"
linuxcnc_projection_state="$source_projection_root/.linuxcnc-source-identity"
missing_source_report="$build_root/petalinux/v5-missing-source-inputs.json"

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
if [ ! -f "$source_root/v5_linuxcnc_source_identity.json" ]; then
    echo "Windows-owned LinuxCNC source identity is missing: $source_root" >&2
    exit 8
fi
if [ ! -f "$petalinux_overlay_tool" ] || [ ! -f "$source_package_verifier" ]; then
    echo "canonical PetaLinux overlay/source package tool is missing" >&2
    exit 8
fi
if [ ! -f "$minimal_runtime_verifier" ] || [ ! -f "$runtime_allowlist" ]; then
    echo "minimal LinuxCNC runtime verifier/allowlist is missing" >&2
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
    if [ -n "$rootfs_gate_marker" ] && [ -f "$rootfs_gate_marker" ]; then
        case "$rootfs_gate_marker" in "$build_root"/*) rm -f "$rootfs_gate_marker" ;; esac
    fi
    if [ "$petalinux_overlay_active" -eq 1 ]; then
        run_petalinux_overlay clean >/dev/null 2>&1 || true
        petalinux_overlay_active=0
    fi
}
trap cleanup EXIT HUP INT TERM

ensure_build_memory
cleanup
mkdir -p "$downloads_root"
chown "$build_user":"$(id -gn "$build_user")" "$downloads_root"
run_petalinux_overlay clean >/dev/null
petalinux_overlay_active=1
run_petalinux_overlay prepare
petalinux_root=$(CDPATH= cd -- "$petalinux_root" && pwd)
if [ ! -f "$petalinux_root/project-spec/configs/config" ]; then
    echo "prepared PetaLinux overlay is incomplete: $petalinux_root" >&2
    exit 12
fi
recipe_target="$petalinux_root/project-spec/meta-user/recipes-apps/linuxcnc-prebuilt"
mkdir -p "$overlay_upper" "$overlay_work" "$overlay_merged" "$recipe_target/files"
chown "$build_user":"$(id -gn "$build_user")" \
    "$overlay_root" "$overlay_upper" "$overlay_work" "$overlay_merged"
command -v rsync >/dev/null 2>&1 || {
    echo "rsync is required for the persistent LinuxCNC projection" >&2
    exit 12
}
case "$linuxcnc_projection" in
    "$source_projection_root"/*) ;;
    *) echo "LinuxCNC projection escaped the unique current projection" >&2; exit 12 ;;
esac
[ ! -L "$source_projection_root" ] && [ ! -L "$linuxcnc_projection" ] || {
    echo "LinuxCNC projection path must not be a symlink" >&2
    exit 12
}
mkdir -p "$source_projection_root" "$linuxcnc_projection"
source_identity=$(sha256sum "$source_root/v5_linuxcnc_source_identity.json" | awk '{print $1}')
source_hash=$(python3 "$script_dir/verify_v5_linuxcnc_source.py" \
    --project-root "$project_root" \
    --source-root "$source_root" \
    --allow-flattened-symlinks \
    --print-source-hash)
source_projection_key="$source_identity:$source_hash"
projected_identity=
[ ! -f "$linuxcnc_projection_state" ] || projected_identity=$(cat "$linuxcnc_projection_state")
if [ "$projected_identity" = "$source_projection_key" ] && \
   [ -f "$linuxcnc_projection/v5_linuxcnc_source_identity.json" ]; then
    echo "V5_LINUXCNC_PROJECTION_REUSED identity=$source_identity content=$source_hash"
else
    rsync -a --checksum --delete --exclude '.git/' \
        "$source_root/" "$linuxcnc_projection/"
    python3 "$script_dir/verify_v5_linuxcnc_source.py" \
        --project-root "$project_root" \
        --source-root "$source_root" \
        --allow-flattened-symlinks \
        --materialize-symlinks "$linuxcnc_projection" >/dev/null
    projected_hash=$(python3 "$script_dir/verify_v5_linuxcnc_source.py" \
        --project-root "$project_root" \
        --source-root "$linuxcnc_projection" \
        --print-source-hash)
    [ "$projected_hash" = "$source_hash" ] || {
        echo "LinuxCNC projection hash mismatch: $projected_hash != $source_hash" >&2
        exit 12
    }
    printf '%s\n' "$source_projection_key" >"$linuxcnc_projection_state.new"
    mv "$linuxcnc_projection_state.new" "$linuxcnc_projection_state"
    echo "V5_LINUXCNC_PROJECTION_UPDATED identity=$source_identity content=$projected_hash"
fi
mount -t overlay overlay \
    -o "lowerdir=$linuxcnc_projection,upperdir=$overlay_upper,workdir=$overlay_work" \
    "$overlay_merged"
chown "$build_user":"$(id -gn "$build_user")" "$overlay_merged"
python3 "$script_dir/verify_v5_linuxcnc_source.py" \
    --project-root "$project_root" \
    --source-root "$linuxcnc_projection"
ln -s "$integration_root/yocto/linuxcnc-prebuilt.bb" "$recipe_target/linuxcnc-prebuilt.bb"
ln -s "$runtime_allowlist" "$recipe_target/files/v5_linuxcnc_runtime_allowlist.tsv"

run_petalinux_build() {
    task_args=$1
    build_home=$(getent passwd "$build_user" | cut -d: -f6)
    runuser -u "$build_user" -- env \
        -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY \
        -u http_proxy -u https_proxy -u all_proxy \
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

run_bitbake_direct() {
    task_args=$1
    build_home=$(getent passwd "$build_user" | cut -d: -f6)
    bitbake_bin="$petalinux_root/components/yocto/layers/core/bitbake/bin/bitbake"
    [ -x "$bitbake_bin" ] || {
        echo "generated BitBake executable is missing: $bitbake_bin" >&2
        exit 12
    }
    runuser -u "$build_user" -- env \
        -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY \
        -u http_proxy -u https_proxy -u all_proxy \
        HOME="$build_home" \
        PETALINUX="${PETALINUX:-}" \
        PETALINUX_VER="${PETALINUX_VER:-}" \
        XSCT_TOOLCHAIN="${XSCT_TOOLCHAIN:-}" \
        PATH="$(dirname "$bitbake_bin"):$PATH" \
        SHELL=/bin/bash \
        TERM="${TERM:-dumb}" \
        BB_ENV_EXTRAWHITE="${BB_ENV_EXTRAWHITE:-} V5_LINUXCNC_EXTERNAL_SOURCE" \
        V5_LINUXCNC_EXTERNAL_SOURCE="$overlay_merged" \
        sh -c "cd '$petalinux_root/build' && bitbake $task_args"
}

configure_download_cache() {
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
    echo "V5_PETALINUX_DOWNLOAD_CACHE_OK path=$downloads_root"
}

configure_offline_bitbake() {
    for conf in \
        "$petalinux_root/build/conf/local.conf" \
        "$petalinux_root/build/conf/plnxtool.conf"
    do
        temp_conf="$conf.v5-offline"
        awk '
            BEGIN { skipping = 0; quote_parity = 0 }
            skipping {
                quote_parity = (quote_parity + split($0, quotes, /"/) - 1) % 2
                continued = ($0 ~ /\\[[:space:]]*$/)
                if (!continued && quote_parity == 0) skipping = 0
                next
            }
            /^[[:space:]]*(BB_NO_NETWORK|BB_FETCH_PREMIRRORONLY|SOURCE_MIRROR_URL|SSTATE_MIRRORS|BB_GENERATE_MIRROR_TARBALLS|RM_WORK_EXCLUDE)[[:space:]]*[?:+]?=/ {
                quote_parity = (split($0, quotes, /"/) - 1) % 2
                continued = ($0 ~ /\\[[:space:]]*$/)
                if (continued || quote_parity != 0) skipping = 1
                next
            }
            /^[[:space:]]*do_fetch\[nostamp\][[:space:]]*=/ { next }
            /^[[:space:]]*INHERIT[[:space:]]*\+=[[:space:]]*"own-mirrors"[[:space:]]*$/ { next }
            { print }
        ' "$conf" > "$temp_conf"
        cat >> "$temp_conf" <<EOF
BB_NO_NETWORK = "1"
SOURCE_MIRROR_URL = "file://$source_package_root"
INHERIT += "own-mirrors"
BB_FETCH_PREMIRRORONLY = "1"
SSTATE_MIRRORS = ""
BB_GENERATE_MIRROR_TARBALLS = "0"
RM_WORK_EXCLUDE += " linuxcnc-prebuilt petalinux-image-minimal"
EOF
        mv "$temp_conf" "$conf"
        chown "$build_user":"$(id -gn "$build_user")" "$conf"
        grep -Fqx 'BB_NO_NETWORK = "1"' "$conf" || exit 12
        grep -Fqx "SOURCE_MIRROR_URL = \"file://$source_package_root\"" "$conf" || exit 12
        grep -Fqx 'INHERIT += "own-mirrors"' "$conf" || exit 12
        grep -Fqx 'BB_FETCH_PREMIRRORONLY = "1"' "$conf" || exit 12
        grep -Fqx 'SSTATE_MIRRORS = ""' "$conf" || exit 12
    done
    echo "V5_PETALINUX_NETWORK_DISABLED"
}

verify_windows_source_packages() {
    if python3 "$source_package_verifier" \
        --project-root "$project_root" \
        --source-root "$source_package_root" \
        --missing-report "$missing_source_report" \
        --resume-scope "linuxcnc-$build_mode"
    then
        return 0
    else
        verify_rc=$?
    fi
    if [ "$verify_rc" -eq 42 ]; then
        echo "V5_WINDOWS_SOURCE_IMPORT_REQUIRED report=$missing_source_report resume_scope=linuxcnc-$build_mode" >&2
    fi
    return "$verify_rc"
}

verify_rootfs_package_selection() {
    rootfs_config="$petalinux_root/project-spec/configs/rootfs_config"
    custom_allowlist="$petalinux_root/project-spec/meta-user/conf/user-rootfsconfig"
    [ -f "$rootfs_config" ] && [ -f "$custom_allowlist" ] || {
        echo "PetaLinux rootfs package selection is incomplete" >&2
        exit 12
    }
    if grep -Eq '^CONFIG_(packagegroup-petalinux-(qt|qt-extended|matchbox)|packagegroup-core-x11|gtk|gtkPLUS|qt|tk|libx11|xserver|wayland|weston).*=[ym]$' "$rootfs_config"; then
        echo "forbidden GUI package is enabled in rootfs_config" >&2
        exit 12
    fi
    if grep -Eq '^CONFIG_python3=[ym]$' "$rootfs_config"; then
        echo "top-level python3 package expands to python3-modules and forbidden Tk/X11 runtime" >&2
        exit 12
    fi
    if grep -Eq '^CONFIG_python3-(modules|tkinter)=[ym]$' "$rootfs_config"; then
        echo "forbidden broad Python/Tk package is enabled in rootfs_config" >&2
        exit 12
    fi
    grep -Fqx '# CONFIG_python3 is not set' "$rootfs_config" || {
        echo "top-level python3 package must be explicitly disabled" >&2
        exit 12
    }
    if grep -Eiq '^CONFIG_.*(pyqt|qtvcp|gmoccapy|gladevcp|pyvcp|tklinuxcnc|touchy|axis)' "$custom_allowlist"; then
        echo "forbidden LinuxCNC GUI package is present in the custom rootfs allowlist" >&2
        exit 12
    fi
    echo "V5_ROOTFS_PACKAGE_WHITELIST_OK"
}

verify_rootfs_package_selection
verify_windows_source_packages
run_petalinux_build "-c linuxcnc-prebuilt -x listtasks"
configure_download_cache
configure_offline_bitbake
run_bitbake_direct "linuxcnc-prebuilt -c listtasks"

audit_minimal_runtime() {
    work_root="$build_root/petalinux/output/tmp/work"
    package_root=$(find "$work_root" -type d -path '*/linuxcnc-prebuilt/*/packages-split/linuxcnc-prebuilt' -print | sort | tail -n 1)
    [ -n "$package_root" ] || {
        echo "linuxcnc-prebuilt runtime package root was not found" >&2
        exit 12
    }
    rootfs_root=$(find "$work_root" -type d -path '*/petalinux-image-minimal/*/rootfs' -print | sort | tail -n 1)
    [ -n "$rootfs_root" ] || {
        echo "focused rootfs directory was not found" >&2
        exit 12
    }
    image_manifest=""
    license_root="$build_root/petalinux/output/tmp/deploy/licenses"
    if [ -d "$license_root" ] && [ -f "$rootfs_gate_marker" ]; then
        for candidate in $(find "$license_root" -type f -name package.manifest -newer "$rootfs_gate_marker" -print | sort); do
            if grep -Eq '^linuxcnc-prebuilt([[:space:]]|$)' "$candidate"; then
                image_manifest="$candidate"
            fi
        done
    fi
    [ -n "$image_manifest" ] || {
        echo "fresh rootfs package manifest containing linuxcnc-prebuilt was not found" >&2
        exit 12
    }
    python3 "$minimal_runtime_verifier" \
        --allowlist "$runtime_allowlist" \
        --package-root "$package_root" \
        --rootfs "$rootfs_root" \
        --image-manifest "$image_manifest"
}

if [ "$build_mode" = focused ]; then
    run_bitbake_direct "linuxcnc-prebuilt -c package"
    rootfs_gate_marker="$build_root/petalinux/rootfs-gate.$$.marker"
    touch "$rootfs_gate_marker"
    run_bitbake_direct "petalinux-image-minimal -c rootfs"
    audit_minimal_runtime
    cleanup
    trap - EXIT HUP INT TERM
    echo "V5_LINUXCNC_FOCUSED_OK package_root=$package_root rootfs=$rootfs_root manifest=$image_manifest"
    exit 0
fi

if [ "$clean_kernel" -eq 1 ]; then
    run_petalinux_build "-c kernel -x clean"
fi
rootfs_gate_marker="$build_root/petalinux/rootfs-gate.$$.marker"
touch "$rootfs_gate_marker"
run_bitbake_direct "petalinux-image-minimal"
audit_minimal_runtime

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

petalinux_images="$build_root/petalinux/output/tmp/deploy/images/zynq-generic"
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

rm -rf "$artifact_dir"
mv "$artifact_stage" "$artifact_dir"
artifact_stage=""
cleanup
trap - EXIT HUP INT TERM
echo "V5_LINUXCNC_BUILD_OK mode=$build_mode clean_kernel=$clean_kernel artifact_dir=$artifact_dir petalinux_images=$image_count"
