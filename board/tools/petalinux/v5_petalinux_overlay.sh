#!/bin/sh
set -eu

action=${1:-status}
verify_petalinux_owner=1
verify_linux_owner=1
source_mount=${VM_SOURCE_MOUNT_ROOT:-/mnt/v5-source}
build_root=${VM_BUILD_ROOT:-$HOME/v5-build}
build_user=${V5_PETALINUX_BUILD_USER:-}
source_root=$source_mount/board/petalinux
state_root=$build_root/petalinux/overlay
upper_root=$state_root/upper
work_root=$state_root/work
merged_root=$state_root/merged
output_root=$build_root/petalinux/output
tmp_output_root=$output_root/tmp
verifier=$source_mount/board/tools/petalinux/verify_v5_petalinux_source.py
linux_verifier=$source_mount/board/tools/petalinux/verify_v5_linux_source.py

fail() {
    echo "V5_PETALINUX_OVERLAY_ERROR $*" >&2
    exit 1
}

canonical_path() {
    readlink -f "$1"
}

validate_source_mount() {
    [ -d "$source_mount" ] || fail "source mount is missing: $source_mount"
    [ -d "$source_root" ] || fail "PetaLinux source owner is missing: $source_root"
    [ -f "$verifier" ] || fail "PetaLinux verifier is missing: $verifier"
    [ -f "$linux_verifier" ] || fail "Linux source verifier is missing: $linux_verifier"

    mount_type=$(findmnt -n -o FSTYPE -T "$source_root")
    mount_options=$(findmnt -n -o OPTIONS -T "$source_root")
    case "$mount_type" in
        fuse.vmhgfs-fuse|9p|cifs|nfs|nfs4|virtiofs) ;;
        *) fail "source owner is not on a shared/network filesystem: $mount_type" ;;
    esac
    case ",$mount_options," in
        *,ro,*) ;;
        *) fail "source mount is not read-only: $mount_options" ;;
    esac

    resolved_mount=$(canonical_path "$source_mount")
    resolved_source=$(canonical_path "$source_root")
    case "$resolved_source/" in
        "$resolved_mount"/board/petalinux/) ;;
        *) fail "PetaLinux source escaped the canonical mount: $resolved_source" ;;
    esac

    if [ "$verify_petalinux_owner" -eq 1 ]; then
        python3 "$verifier" --project-root "$source_mount" --source-root "$source_root"
    else
        echo "V5_PETALINUX_SOURCE_VERIFY_SKIPPED mode=target-only"
    fi
    if [ "$verify_linux_owner" -eq 1 ]; then
        python3 "$linux_verifier" \
            --project-root "$source_mount" \
            --build-root "$build_root" \
            --projection-root "$build_root/petalinux/linux-source-verify"
    else
        echo "V5_PETALINUX_LINUX_SOURCE_VERIFY_SKIPPED mode=target-only"
    fi
}

validate_build_root() {
    mkdir -p "$build_root/petalinux" "$output_root"
    resolved_mount=$(canonical_path "$source_mount")
    resolved_build=$(canonical_path "$build_root")
    case "$resolved_build/" in
        "$resolved_mount"/*) fail "build root must be outside the source mount: $resolved_build" ;;
    esac
    resolved_state=$(canonical_path "$build_root/petalinux")/overlay
    case "$resolved_state/" in
        "$resolved_build"/petalinux/overlay/) ;;
        *) fail "overlay state escaped the canonical build root: $resolved_state" ;;
    esac
    resolved_output=$(canonical_path "$output_root")
    case "$resolved_output/" in
        "$resolved_build"/petalinux/output/) ;;
        *) fail "PetaLinux output escaped the canonical build root: $resolved_output" ;;
    esac
}

validate_build_user() {
    [ -n "$build_user" ] || fail "V5_PETALINUX_BUILD_USER is required"
    id "$build_user" >/dev/null 2>&1 || fail "PetaLinux build user does not exist: $build_user"
    [ "$(id -u "$build_user")" -ne 0 ] || fail "PetaLinux build user must not be root"
}

overlay_is_mounted() {
    mountpoint -q "$merged_root"
}

configure_native_tmpdir() {
    project_config=$merged_root/project-spec/configs/config
    [ -f "$project_config" ] || fail "PetaLinux project config is missing: $project_config"
    grep -q '^CONFIG_TMP_DIR_LOCATION=' "$project_config" || \
        fail "PetaLinux project config has no TMPDIR owner"
    sed -i \
        "s|^CONFIG_TMP_DIR_LOCATION=.*|CONFIG_TMP_DIR_LOCATION=\"$tmp_output_root\"|" \
        "$project_config"
    grep -Fqx "CONFIG_TMP_DIR_LOCATION=\"$tmp_output_root\"" "$project_config" || \
        fail "failed to redirect PetaLinux TMPDIR to native build storage"
    chown "$build_user:$(id -gn "$build_user")" "$project_config"
}

configure_source_owner() {
    layer_conf=$merged_root/project-spec/meta-user/conf/layer.conf
    [ -f "$layer_conf" ] || fail "PetaLinux layer config is missing: $layer_conf"
    case "$source_mount" in
        *\"*) fail "source mount contains an unsupported quote" ;;
    esac
    sed -i '/^V5_PROJECT_SOURCE_ROOT = /d' "$layer_conf"
    printf '\nV5_PROJECT_SOURCE_ROOT = "%s"\n' "$source_mount" >> "$layer_conf"
    grep -Fqx "V5_PROJECT_SOURCE_ROOT = \"$source_mount\"" "$layer_conf" || \
        fail "failed to bind the canonical Linux owner root"
    chown "$build_user:$(id -gn "$build_user")" "$layer_conf"
}

prepare_overlay() {
    validate_source_mount
    validate_build_root
    validate_build_user
    overlay_is_mounted && fail "overlay is already mounted: $merged_root"
    rm -rf "$state_root"
    mkdir -p "$upper_root" "$work_root" "$merged_root" "$tmp_output_root"
    build_group=$(id -gn "$build_user")
    chown "$build_user:$build_group" \
        "$upper_root" "$work_root" "$merged_root" "$output_root" "$tmp_output_root"
    mount -t overlay v5-petalinux-overlay \
        -o "lowerdir=$source_root,upperdir=$upper_root,workdir=$work_root" \
        "$merged_root"
    [ "$(findmnt -n -o FSTYPE -T "$merged_root")" = "overlay" ] || \
        fail "merged project is not an overlay mount"
    chown "$build_user:$build_group" \
        "$merged_root" \
        "$merged_root/.petalinux" \
        "$merged_root/project-spec" \
        "$merged_root/project-spec/configs" \
        "$merged_root/project-spec/meta-user"
    configure_native_tmpdir
    configure_source_owner
    [ "$(findmnt -n -o FSTYPE -T "$tmp_output_root")" != "overlay" ] || \
        fail "PetaLinux TMPDIR must not run on OverlayFS: $tmp_output_root"
    echo "V5_PETALINUX_OVERLAY_READY lower=$source_root merged=$merged_root upper=$upper_root tmp=$tmp_output_root user=$build_user"
}

unmount_overlay() {
    if overlay_is_mounted; then
        umount "$merged_root"
    fi
    echo "V5_PETALINUX_OVERLAY_UNMOUNTED merged=$merged_root"
}

clean_overlay() {
    validate_build_root
    unmount_overlay
    rm -rf "$state_root"
    echo "V5_PETALINUX_OVERLAY_CLEAN state=$state_root tmp=$tmp_output_root"
}

show_status() {
    validate_source_mount
    validate_build_root
    if overlay_is_mounted; then
        echo "V5_PETALINUX_OVERLAY_STATUS mounted=1 lower=$source_root merged=$merged_root upper=$upper_root tmp=$tmp_output_root"
    else
        echo "V5_PETALINUX_OVERLAY_STATUS mounted=0 lower=$source_root merged=$merged_root upper=$upper_root tmp=$tmp_output_root"
    fi
}

dry_run() {
    validate_source_mount
    validate_build_user
    before=$(python3 "$verifier" --project-root "$source_mount" --source-root "$source_root" --print-source-hash)
    prepare_overlay
    trap 'clean_overlay >/dev/null 2>&1 || true' EXIT HUP INT TERM

    probe=$merged_root/.v5-overlay-write-probe
    runuser -u "$build_user" -- env V5_OVERLAY_PROBE="$probe" \
        sh -c 'printf "%s\n" "overlay-write-probe" > "$V5_OVERLAY_PROBE"'
    [ -f "$probe" ] || fail "merged overlay is not writable"
    [ -f "$upper_root/.v5-overlay-write-probe" ] || fail "probe did not land in the build upper layer"
    [ ! -e "$source_root/.v5-overlay-write-probe" ] || fail "probe reached the read-only source owner"

    tmp_probe=$tmp_output_root/.v5-tmp-write-probe
    runuser -u "$build_user" -- env V5_TMP_PROBE="$tmp_probe" \
        sh -c 'printf "%s\n" "tmp-write-probe" > "$V5_TMP_PROBE"'
    [ -f "$tmp_probe" ] || fail "TMPDIR probe did not land in native build output"
    [ "$(findmnt -n -o FSTYPE -T "$tmp_probe")" != "overlay" ] || \
        fail "PetaLinux TMPDIR probe remained on OverlayFS"
    grep -Fqx "CONFIG_TMP_DIR_LOCATION=\"$tmp_output_root\"" \
        "$merged_root/project-spec/configs/config" || \
        fail "overlay project does not consume native TMPDIR"
    grep -Fqx "V5_PROJECT_SOURCE_ROOT = \"$source_mount\"" \
        "$merged_root/project-spec/meta-user/conf/layer.conf" || \
        fail "overlay project does not consume the canonical Linux owner"

    after=$(python3 "$verifier" --project-root "$source_mount" --source-root "$source_root" --print-source-hash)
    [ "$before" = "$after" ] || fail "Windows source identity changed during overlay probe"
    rm -f "$probe" "$tmp_probe"
    clean_overlay
    trap - EXIT HUP INT TERM
    echo "V5_PETALINUX_OVERLAY_DRY_RUN_OK source=$before"
}

case "$action" in
    prepare) prepare_overlay ;;
    prepare-target-only)
        verify_petalinux_owner=0
        verify_linux_owner=0
        prepare_overlay
        ;;
    status) show_status ;;
    unmount) unmount_overlay ;;
    clean) clean_overlay ;;
    dry-run) dry_run ;;
    *) fail "usage: $0 {prepare|prepare-target-only|status|unmount|clean|dry-run}" ;;
esac
