SUMMARY = "V5 Zynq StepGen HAL module"
SECTION = "v5/apps"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0;md5=801f80980d171dd6425610833a22dbe6"
PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = "file://zynq_stepgen_hw.c"

S = "${WORKDIR}"
B = "${WORKDIR}/build"

DEPENDS += "linuxcnc-prebuilt"

do_configure[noexec] = "1"

do_compile() {
    install -d ${B}
    install -m 0644 ${S}/zynq_stepgen_hw.c ${B}/zynq_stepgen_hw.c
    cp ${RECIPE_SYSROOT}${datadir}/linuxcnc/Makefile.modinc ${B}/v5.Makefile.modinc
    sed -i "s@\$(Q)ld -d -r@\$(Q)${LD} -d -r@g" ${B}/v5.Makefile.modinc
    sed -i "s@\$(Q)objdump @\$(Q)${OBJDUMP} @g" ${B}/v5.Makefile.modinc
    cat > ${B}/Makefile <<'EOF'
obj-m += zynq_stepgen_hw.o
include v5.Makefile.modinc
EOF
    oe_runmake -C ${B} modules \
        CC="${CC}" \
        EXTRA_CFLAGS="${CFLAGS} -DUSPACE -fno-fast-math -DRTAPI -D_GNU_SOURCE -Drealtime -D__MODULE__ -DSIM -fPIC -I. -I${RECIPE_SYSROOT}${includedir}/linuxcnc" \
        RTLIBDIR=/usr/lib/linuxcnc/modules \
        LIBDIR=${libdir} \
        EMC2_HOME=/usr
}

do_install() {
    install -d ${D}/usr/lib/linuxcnc/modules
    install -m 0644 ${B}/zynq_stepgen_hw.so ${D}/usr/lib/linuxcnc/modules/zynq_stepgen_hw.so
}

FILES_${PN} += " \
    /usr/lib/linuxcnc/modules/zynq_stepgen_hw.so \
"

RDEPENDS_${PN} += "linuxcnc-prebuilt"

INSANE_SKIP_${PN} += "already-stripped ldflags"
