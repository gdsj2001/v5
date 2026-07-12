FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"

V5_PROJECT_SOURCE_ROOT ?= ""
KERNELURI = "file://v5-linux-source.marker;name=machine"
YOCTO_META = "file://v5-realtime-source.marker;type=kmeta;name=meta;destsuffix=v5-owner-projection/linux/realtime"
S = "${WORKDIR}/v5-owner-projection/linux/kernel"
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
        --build-root ${WORKDIR} \
        --output-root ${WORKDIR}/v5-owner-projection
}
do_v5_linux_projection[dirs] = "${WORKDIR}"
do_v5_linux_projection[file-checksums] = " \
    ${V5_PROJECT_SOURCE_ROOT}/linux/kernel/v5_linux_source_identity.json:True \
    ${V5_PROJECT_SOURCE_ROOT}/linux/realtime/v5_realtime_source_identity.json:True \
    ${V5_PROJECT_SOURCE_ROOT}/board/tools/petalinux/project_v5_linux_source.py:True \
    ${V5_PROJECT_SOURCE_ROOT}/board/tools/petalinux/verify_v5_linux_source.py:True \
"
addtask v5_linux_projection after do_unpack before do_symlink_kernsrc

do_patch_append() {
    install -m 0644 ${WORKDIR}/xlnx_atk_lcd.c ${S}/drivers/gpu/drm/xlnx/xlnx_atk_lcd.c
    install -m 0644 ${WORKDIR}/pwm-dglnt.c ${S}/drivers/pwm/pwm-dglnt.c
}
