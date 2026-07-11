SUMMARY = "LinuxCNC HAL CiA402 component"
SECTION = "v5/apps"
LICENSE = "GPLv2+"
LIC_FILES_CHKSUM = "file://cia402.comp;beginline=1;endline=18;md5=74d33ade5912256d986c8b8e969496d0"
PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = " \
    file://cia402.comp \
    file://cia402.c \
"

S = "${WORKDIR}"
B = "${WORKDIR}/build"

DEPENDS += "linuxcnc-prebuilt"

do_configure[noexec] = "1"

do_compile() {
    install -d ${B}
    install -m 0644 ${S}/cia402.c ${B}/cia402.c
    cp ${RECIPE_SYSROOT}${datadir}/linuxcnc/Makefile.modinc ${B}/v5.Makefile.modinc
    sed -i "s@\$(Q)ld -d -r@\$(Q)${LD} -d -r@g" ${B}/v5.Makefile.modinc
    sed -i "s@\$(Q)objdump @\$(Q)${OBJDUMP} @g" ${B}/v5.Makefile.modinc
    cat > ${B}/Makefile <<'EOF'
obj-m += cia402.o
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
    install -m 0644 ${B}/cia402.so ${D}/usr/lib/linuxcnc/modules/cia402.so
    chmod 0644 ${D}/usr/lib/linuxcnc/modules/cia402.so

    install -d ${D}/usr/src/hal-cia402
    install -m 0644 ${S}/cia402.comp ${D}/usr/src/hal-cia402/cia402.comp
    install -m 0644 ${S}/cia402.c ${D}/usr/src/hal-cia402/cia402.c
}

FILES_${PN} += " \
    /usr/lib/linuxcnc/modules/cia402.so \
    /usr/src/hal-cia402 \
"

RDEPENDS_${PN} += "linuxcnc-prebuilt"

INSANE_SKIP_${PN} += "already-stripped ldflags"
