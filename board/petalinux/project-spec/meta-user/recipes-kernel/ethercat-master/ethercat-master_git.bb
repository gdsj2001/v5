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
    file://0007-v5-silent-cyclic-reference-clock-errors.patch \
    file://0008-v5-bound-initial-dc-sync-wait.patch \
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
# Motion-driver SelectLink owns cold-start.  Keep the canonical stop link, but
# never start EtherCAT unconditionally before SETTINGS/bus_pulse_setting is read.
INITSCRIPT_PARAMS_${PN} = "stop 10 0 6 ."

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
    v5_ethercat_count_exact() {
        awk -v v5_line="$1" '$0 == v5_line { count += 1 } END { print count + 0 }' "$2"
    }
    v5_ethercat_init=${D}${sysconfdir}/init.d/ethercat
    [ -f "$v5_ethercat_init" ] || bbfatal "EtherCAT init is missing: $v5_ethercat_init"
    v5_ethercat_start_line='    if $ETHERCATCTL start; then'
    v5_ethercat_start_count=$(v5_ethercat_count_exact "$v5_ethercat_start_line" "$v5_ethercat_init")
    [ "$v5_ethercat_start_count" -eq 1 ] || bbfatal "EtherCAT init start target count is $v5_ethercat_start_count, expected 1"
    [ "$(v5_ethercat_count_exact 'v5_ethercat_permission_fail() {' "$v5_ethercat_init")" -eq 0 ] || bbfatal "EtherCAT permission fail function already exists"
    [ "$(v5_ethercat_count_exact 'v5_ethercat_apply_permissions() {' "$v5_ethercat_init")" -eq 0 ] || bbfatal "EtherCAT permission apply function already exists"
    [ "$(v5_ethercat_count_exact '        v5_ethercat_apply_permissions' "$v5_ethercat_init")" -eq 0 ] || bbfatal "EtherCAT permission call already exists"
    sed -i '1a\
v5_ethercat_permission_fail() {\
    $ETHERCATCTL stop >/dev/null 2>&1\
    exit 1\
}\
v5_ethercat_apply_permissions() {\
    v5_ethercat_gid=$(id -g petalinux) || v5_ethercat_permission_fail\
    v5_ethercat_found=0\
    for v5_ethercat_node in /dev/EtherCAT*; do\
        [ -c "$v5_ethercat_node" ] || continue\
        v5_ethercat_found=1\
        chown root:petalinux "$v5_ethercat_node" || v5_ethercat_permission_fail\
        chmod 0660 "$v5_ethercat_node" || v5_ethercat_permission_fail\
        v5_ethercat_actual=$(stat -c "%u:%g:%a" "$v5_ethercat_node") || v5_ethercat_permission_fail\
        [ "$v5_ethercat_actual" = "0:${v5_ethercat_gid}:660" ] || v5_ethercat_permission_fail\
    done\
    [ "$v5_ethercat_found" = 1 ] || v5_ethercat_permission_fail\
}' "$v5_ethercat_init"
    sed -i '/^    if \$ETHERCATCTL start; then$/a\        v5_ethercat_apply_permissions' "$v5_ethercat_init"
    [ "$(v5_ethercat_count_exact 'v5_ethercat_permission_fail() {' "$v5_ethercat_init")" -eq 1 ] || bbfatal "EtherCAT permission fail function count is not 1"
    [ "$(v5_ethercat_count_exact 'v5_ethercat_apply_permissions() {' "$v5_ethercat_init")" -eq 1 ] || bbfatal "EtherCAT permission apply function count is not 1"
    v5_ethercat_call_count=$(v5_ethercat_count_exact '        v5_ethercat_apply_permissions' "$v5_ethercat_init")
    [ "$v5_ethercat_call_count" -eq 1 ] || bbfatal "EtherCAT permission call count is $v5_ethercat_call_count, expected 1"
    v5_ethercat_expected_call_number=$(awk -v v5_line="$v5_ethercat_start_line" '$0 == v5_line { print NR + 1 }' "$v5_ethercat_init")
    v5_ethercat_call_number=$(awk '$0 == "        v5_ethercat_apply_permissions" { print NR }' "$v5_ethercat_init")
    [ "$v5_ethercat_call_number" -eq "$v5_ethercat_expected_call_number" ] || bbfatal "EtherCAT permission call is not adjacent to start target"
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
