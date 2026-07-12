SUMMARY = "IgH EtherCAT master for LinuxCNC native EtherCAT motion"
SECTION = "v5/kernel"
LICENSE = "GPLv2 & LGPLv2.1"
LIC_FILES_CHKSUM = " \
    file://COPYING;md5=59530bdf33659b29e73d4adb9f9f6552 \
    file://COPYING.LESSER;md5=4fbd65380cdd255951079008b364516c \
"
PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = " \
    git://gitlab.com/etherlab.org/ethercat.git;protocol=https;branch=stable-1.6 \
    file://0001-v5-avoid-rewriting-identical-pdo-config.patch \
    file://0002-v5-relax-generic-frame-timeout.patch \
    file://0003-v5-split-mixed-domain-datagrams.patch \
    file://0004-v5-ignore-outgoing-generic-packet-echo.patch \
    file://0005-v5-drain-generic-rx-burst.patch \
    file://0006-v5-keep-sent-datagram-state-on-duplicate-queue.patch \
"
SRCREV = "b709e58147e65b5e3251b45f48c01ef33cc7366f"
PV = "1.6.9+git${SRCPV}"

S = "${WORKDIR}/git"
B = "${S}"

inherit autotools pkgconfig update-rc.d module

DEPENDS += "virtual/kernel autoconf-native automake-native libtool-native pkgconfig-native bison-native flex-native bc-native"

EXTRA_OECONF += " \
    --with-linux-dir=${STAGING_KERNEL_DIR} \
    --with-module-dir=ethercat \
    --enable-kernel \
    --enable-generic \
    --disable-8139too \
    --disable-eoe \
    --disable-tty \
    --enable-tool \
    --enable-userlib \
    --enable-initd \
    --disable-systemd \
"

INITSCRIPT_PACKAGES = "${PN}"
INITSCRIPT_NAME_${PN} = "ethercat"
INITSCRIPT_PARAMS_${PN} = "start 90 5 . stop 10 0 6 ."

do_configure_prepend() {
    cd ${S}
    ./bootstrap

    rm -f ${STAGING_KERNEL_DIR}/.config
    rm -rf ${STAGING_KERNEL_DIR}/include/config
    rm -rf ${STAGING_KERNEL_DIR}/include/generated
    rm -rf ${STAGING_KERNEL_DIR}/arch/arm/include/generated
    ln -snf ${STAGING_KERNEL_BUILDDIR}/kernel-abiversion ${STAGING_KERNEL_DIR}/.kernelrelease
}

do_configure_append() {
    sed -i 's@obj-m := examples/ master/ devices/@obj-m := master/ devices/@' ${S}/Kbuild
}

do_compile() {
    oe_runmake -C ${S}
    oe_runmake -C ${STAGING_KERNEL_DIR} ARCH=arm CROSS_COMPILE=${TARGET_PREFIX} O=${STAGING_KERNEL_BUILDDIR} modules_prepare
    oe_runmake -C ${STAGING_KERNEL_DIR} ARCH=arm CROSS_COMPILE=${TARGET_PREFIX} O=${STAGING_KERNEL_BUILDDIR} scripts
    oe_runmake -C ${S} ARCH=arm CROSS_COMPILE=${TARGET_PREFIX} O=${STAGING_KERNEL_BUILDDIR} modules
}

do_install() {
    oe_runmake -C ${S} DESTDIR=${D} install
    oe_runmake -C ${S} ARCH=arm CROSS_COMPILE=${TARGET_PREFIX} O=${STAGING_KERNEL_BUILDDIR} INSTALL_MOD_PATH=${D} DEPMOD=true modules_install
}

do_install_append() {
    rm -f ${D}${sysconfdir}/ethercat.conf
    if [ -f ${D}${sysconfdir}/sysconfig/ethercat ]; then
        sed -i -E 's@^MASTER0_DEVICE=.*@MASTER0_DEVICE="eth1"@' ${D}${sysconfdir}/sysconfig/ethercat
        sed -i -E 's@^DEVICE_MODULES=.*@DEVICE_MODULES="generic"@' ${D}${sysconfdir}/sysconfig/ethercat
    fi
    if [ -f ${D}${sysconfdir}/init.d/ethercat ]; then
        sed -i '/if \$ETHERCATCTL start; then/a\        chmod 0666 /dev/EtherCAT* 2>/dev/null || true' ${D}${sysconfdir}/init.d/ethercat
    fi
}

FILES_${PN} += " \
    ${bindir}/ethercat \
    ${sbindir}/ethercatctl \
    ${sysconfdir}/init.d/ethercat \
    ${sysconfdir}/sysconfig/ethercat \
    ${libdir}/libethercat* \
    ${includedir}/ethercat* \
    ${datadir}/bash-completion \
    ${datadir}/bash-completion/completions \
    ${datadir}/bash-completion/completions/ethercat \
"

RDEPENDS_${PN} += "bash kmod"

INSANE_SKIP_${PN} += "already-stripped dev-so ldflags"
