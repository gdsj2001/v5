FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"

V5_PROJECT_SOURCE_ROOT ?= ""
V5_VM_BUILD_ROOT ?= "${@os.path.normpath(os.path.join(d.getVar('TMPDIR'), '..', '..', '..'))}"
V5_SOURCE_PROJECTION_ROOT ?= "${V5_VM_BUILD_ROOT}/temp_source/current"
V5_LINUX_BUILD_DIR ?= "${V5_VM_BUILD_ROOT}/linux"
PARALLEL_MAKE = "-j ${@oe.utils.cpu_count()}"
KERNELURI = "file://v5-linux-source.marker;name=machine"
YOCTO_META = "file://v5-realtime-source.marker;type=kmeta;name=meta;destsuffix=v5-owner-projection/linux/realtime"
S = "${WORKDIR}/v5-owner-projection/linux/kernel"
B = "${V5_LINUX_BUILD_DIR}"
KBRANCH = "master"
SRCREV_machine = "AUTOINC"
SRCREV_meta = "AUTOINC"
PV = "${LINUX_VERSION}+v5"

LINUX_KERNEL_TYPE = "preempt-rt"
LINUX_VERSION_EXTENSION = "-rt1-xilinx-v2020.2"
KERNEL_FEATURES_append = " features/rt/rt.scc"

SRC_URI += " \
    file://display.cfg \
    file://v5-rt.cfg \
    file://xlnx_atk_lcd.c \
    file://pwm-dglnt.c \
    file://0001-drm-xlnx-add-atk-lcd.patch \
    file://0002-pwm-add-dglnt-hook.patch \
    file://0003-v5-usb-genesys-hub-slow-enable-wait.patch \
"

do_v5_linux_projection() {
    [ -n "${V5_PROJECT_SOURCE_ROOT}" ] || \
        bbfatal "V5_PROJECT_SOURCE_ROOT is required"
    [ -f "${V5_PROJECT_SOURCE_ROOT}/linux/kernel/v5_linux_source_identity.json" ] || \
        bbfatal "canonical Linux kernel owner is unavailable"
    [ -f "${V5_PROJECT_SOURCE_ROOT}/linux/realtime/v5_realtime_source_identity.json" ] || \
        bbfatal "canonical realtime metadata owner is unavailable"
    python3 ${V5_PROJECT_SOURCE_ROOT}/board/tools/petalinux/project_v5_linux_source.py \
        --project-root ${V5_PROJECT_SOURCE_ROOT} \
        --build-root ${V5_VM_BUILD_ROOT} \
        --output-root ${V5_SOURCE_PROJECTION_ROOT}
    case "${WORKDIR}/v5-owner-projection" in
        "${WORKDIR}"/*) rm -rf "${WORKDIR}/v5-owner-projection" ;;
        *) bbfatal "BitBake work projection cleanup escaped WORKDIR" ;;
    esac
    cp -a --reflink=auto ${V5_SOURCE_PROJECTION_ROOT}/. \
        ${WORKDIR}/v5-owner-projection/
    [ ! -e "${V5_SOURCE_PROJECTION_ROOT}/linux/kernel/.git" ] || \
        bbfatal "persistent Linux projection contains forbidden Git metadata"
    python3 ${V5_PROJECT_SOURCE_ROOT}/board/tools/petalinux/project_v5_linux_source.py \
        --project-root ${V5_PROJECT_SOURCE_ROOT} \
        --build-root ${V5_VM_BUILD_ROOT} \
        --output-root ${WORKDIR}/v5-owner-projection \
        --persistent-projection-root ${V5_SOURCE_PROJECTION_ROOT} \
        --initialize-kernel-build-git
}
do_v5_linux_projection[dirs] = "${WORKDIR}"
do_v5_linux_projection[file-checksums] = " \
    ${V5_PROJECT_SOURCE_ROOT}/linux/kernel/v5_linux_source_identity.json:True \
    ${V5_PROJECT_SOURCE_ROOT}/linux/realtime/v5_realtime_source_identity.json:True \
    ${V5_PROJECT_SOURCE_ROOT}/board/tools/petalinux/project_v5_linux_source.py:True \
    ${V5_PROJECT_SOURCE_ROOT}/board/tools/petalinux/verify_v5_linux_source.py:True \
"
addtask v5_linux_projection after do_unpack before do_kernel_checkout do_kernel_metadata do_symlink_kernsrc

# do_symlink_kernsrc replaces S with a link to work-shared/kernel-source after
# a successful build.  A later forced checkout must not ask BitBake's dirs
# helper to mkdir that existing link; do_kernel_checkout already cd's to S.
do_kernel_checkout[dirs] = "${WORKDIR}"

python v5_prepare_symlink_kernsrc() {
    import os

    kernsrc = os.path.abspath(d.getVar("STAGING_KERNEL_DIR"))
    tmpdir = os.path.abspath(d.getVar("TMPDIR"))
    if os.path.commonpath((kernsrc, tmpdir)) != tmpdir:
        bb.fatal("staging kernel link escaped TMPDIR: %s" % kernsrc)
    if os.path.islink(kernsrc):
        os.unlink(kernsrc)
}
do_symlink_kernsrc[prefuncs] += "v5_prepare_symlink_kernsrc"

do_patch_append() {
    install -m 0644 ${WORKDIR}/xlnx_atk_lcd.c ${S}/drivers/gpu/drm/xlnx/xlnx_atk_lcd.c
    install -m 0644 ${WORKDIR}/pwm-dglnt.c ${S}/drivers/pwm/pwm-dglnt.c
}
