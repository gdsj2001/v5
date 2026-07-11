SUMMARY = "LinuxCNC 2.9.7 built from the Windows-owned V5 source tree"
SECTION = "v5/apps"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263"
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit pkgconfig externalsrc

V5_LINUXCNC_EXTERNAL_SOURCE ?= ""
EXTERNALSRC = "${V5_LINUXCNC_EXTERNAL_SOURCE}"
EXTERNALSRC_BUILD = "${V5_LINUXCNC_EXTERNAL_SOURCE}/src"
PV = "2.9.7+v5"

S = "${EXTERNALSRC}"
B = "${S}/src"

DEPENDS += " \
    boost \
    gettext \
    libglu \
    libepoxy \
    intltool-native \
    asciidoc-native \
    python3-yapps2-native \
    libice \
    libsm \
    libtirpc \
    libx11 \
    libxau \
    libxdmcp \
    libxext \
    libxinerama \
    libxmu \
    python3 \
    readline \
    tcl \
    tk \
"

EXTRA_OECONF = " \
    --with-realtime=uspace \
    --disable-gtk \
    --disable-gtk2 \
    --without-libmodbus \
    --without-libusb-1.0 \
    --disable-check-runtime-deps \
    --disable-userspace-pci \
    --enable-non-distributable=yes \
"

do_configure() {
    if [ -z "${V5_LINUXCNC_EXTERNAL_SOURCE}" ] || [ ! -f ${S}/v5_linuxcnc_source_identity.json ]; then
        bbfatal "V5 LinuxCNC external source owner is unavailable"
    fi
    grep -q 'v5_prepare_wrapped_rotary_target' ${S}/src/emc/motion/command.c || \
        bbfatal "V5 native rotary target owner missing"
    grep -q 'v5_wrapped_rotary_mask' ${S}/src/emc/motion/motion.c || \
        bbfatal "V5 native rotary mask owner missing"
    if grep -Eq 'z20_wrap_public_|WRAPPED_ROTARY' ${S}/src/emc/task/taskintf.cc; then
        bbfatal "retired task/public-coordinate rotary wrapping is present"
    fi
    if grep -q 'z20_wrapped_rotary_planner_idle' ${S}/src/emc/motion/control.c; then
        bbfatal "retired planner-idle rotary normalization is present"
    fi

    cd ${B}
    if [ -f Makefile ]; then
        make distclean || true
    fi
    ./autogen.sh

    tcl_cfg=$(find ${RECIPE_SYSROOT}${libdir} ${RECIPE_SYSROOT}${base_libdir} -name tclConfig.sh | head -n1)
    tk_cfg=$(find ${RECIPE_SYSROOT}${libdir} ${RECIPE_SYSROOT}${base_libdir} -name tkConfig.sh | head -n1)
    if [ -z "$tcl_cfg" ] || [ -z "$tk_cfg" ]; then
        bbfatal "Unable to locate tclConfig.sh/tkConfig.sh in target sysroot"
    fi

    export PATH="$PATH:/usr/bin:/bin:/usr/sbin:/sbin"
    export KILL=/bin/kill
    export WHOAMI=/usr/bin/whoami
    export PYTHONPATH="${RECIPE_SYSROOT_NATIVE}/usr/lib/python3.7/site-packages:${PYTHONPATH}"
    export PYTHON=python3
    export PYTHON_VERSION=3.7
    export PYTHON_CPPFLAGS="-I${RECIPE_SYSROOT}${includedir}/python3.7m"
    export PYTHON_LIBS="-L${RECIPE_SYSROOT}${libdir} -lpython3.7m"
    export PYTHON_EXTRA_LIBS="-lpthread -ldl -lutil -lm"
    export PYTHON_EXTRA_LDFLAGS=""

    ./configure \
        --host=${HOST_SYS} \
        --build=${BUILD_SYS} \
        --prefix=${prefix} \
        --exec_prefix=${exec_prefix} \
        --bindir=${bindir} \
        --sbindir=${sbindir} \
        --libdir=${libdir} \
        --includedir=${includedir} \
        --datadir=${datadir} \
        --sysconfdir=${sysconfdir} \
        --localstatedir=${localstatedir} \
        --mandir=${mandir} \
        --with-tclConfig=$tcl_cfg \
        --with-tkConfig=$tk_cfg \
        ${EXTRA_OECONF}

    sed -i -E 's|^TCL_CFLAGS=.*$|TCL_CFLAGS=-I${RECIPE_SYSROOT}${includedir}/tcl8.6 -DUSE_TCL_STUBS|' Makefile.inc
    sed -i -E 's|^TCL_LIBS=.*$|TCL_LIBS=-L${RECIPE_SYSROOT}${libdir} -ltclstub8.6 -ltk8.6 -lX11 -lXinerama -ltirpc -lpthread -ldl -lz -lm|' Makefile.inc
    sed -i -E 's|^BOOST_PYTHON_LIB=.*$|BOOST_PYTHON_LIB=-lboost_python37|' Makefile.inc
    sed -i -E 's/^CONFIG_HAL_SPEAKER=.*/CONFIG_HAL_SPEAKER=n/' Makefile.inc
    sed -i -E 's/^CONFIG_HAL_PARPORT=.*/CONFIG_HAL_PARPORT=n/' Makefile.inc
    sed -i -E 's/^CONFIG_HAL_GM=.*/CONFIG_HAL_GM=n/' Makefile.inc
    sed -i -E 's/^CONFIG_HAL_PPMC=.*/CONFIG_HAL_PPMC=n/' Makefile.inc
    sed -i "s@\$(Q)ld -d -r -o objects/\$\*.tmp \$\^@\$(Q)${TARGET_PREFIX}ld -d -r -o objects/\$\*.tmp \$\^@" Makefile
    sed -i "s@\$(Q)objdump -w -j .rtapi_export@\$(Q)${TARGET_PREFIX}objdump -w -j .rtapi_export@" Makefile
    sed -i 's#preconv -r < $@.new > $@#cat $@.new > $@#' hal/components/Submakefile
}

do_compile() {
    cd ${B}
    export PYTHONPATH="${RECIPE_SYSROOT_NATIVE}/usr/lib/python3.7/site-packages:${PYTHONPATH}"
    oe_runmake
}

do_install() {
    cd ${B}
    oe_runmake DESTDIR=${D} install-software
    rm -f ${D}${libdir}/linuxcnc/modules/zynq_stepgen_hw.so

    if [ -d ${D} ]; then
        grep -RIl '^#!/usr/bin/python3\.[0-9]' ${D} | xargs -r sed -i -E '1s|^#!/usr/bin/python3\.[0-9]+m?$|#!/usr/bin/python3|'
        grep -RIl '/usr/bin/python3\.[0-9]' ${D} | xargs -r sed -i -E 's|/usr/bin/python3\.[0-9]+m?|/usr/bin/python3|g'
    fi
    if [ -f ${D}${bindir}/linuxcnc ]; then
        sed -i -E 's#^PS=.*$#PS=/bin/ps#' ${D}${bindir}/linuxcnc
        sed -i -E 's#^AWK=.*$#AWK=/usr/bin/awk#' ${D}${bindir}/linuxcnc
        sed -i -E 's#^GREP=.*$#GREP=/bin/grep#' ${D}${bindir}/linuxcnc
        sed -i -E 's#^IPCS=.*$#IPCS=/usr/bin/ipcs#' ${D}${bindir}/linuxcnc
        sed -i -E 's#PYTHONPATH=\$LINUXCNC_HOME/lib/python#PYTHONPATH=/usr/lib/python3/dist-packages:/usr/lib/python3.7/site-packages:\$LINUXCNC_HOME/lib/python#' ${D}${bindir}/linuxcnc
    fi
    for realtime_helper in ${D}${libdir}/linuxcnc/realtime ${D}${bindir}/realtime; do
        if [ -f "$realtime_helper" ]; then
            sed -i -E 's#^PS=.*$#PS=/bin/ps#' "$realtime_helper"
            sed -i -E 's#^GREP=.*$#GREP=/bin/grep#' "$realtime_helper"
        fi
    done

    grep -aq 'v5_wrapped_rotary_mask' ${D}${libdir}/linuxcnc/modules/motmod.so || \
        bbfatal "built motmod.so lacks V5 rotary owner"
    if grep -aEq 'z20_wrap_public_|WRAPPED_ROTARY' ${D}${bindir}/milltask; then
        bbfatal "built milltask contains retired rotary coordinate wrapping"
    fi

    install -d ${D}${datadir}/v5-native
    install -m 0644 ${S}/v5_linuxcnc_source_identity.json \
        ${D}${datadir}/v5-native/v5_linuxcnc_source_identity.json
    identity=${D}${datadir}/v5-native/linuxcnc-source-identity.txt
    {
        echo 'schema=v5-linuxcnc-source-identity-v2'
        echo 'upstream_commit=472ef181410c0c0baac1d7005e9a6287d586db68'
        echo 'upstream_version=2.9.7'
        sha256sum ${S}/v5_linuxcnc_source_identity.json
        sha256sum ${D}${libdir}/linuxcnc/modules/motmod.so
        sha256sum ${D}${bindir}/milltask
    } > "$identity"
}

FILES_${PN} += " \
    ${bindir} \
    ${sbindir} \
    ${libdir} \
    ${datadir} \
    ${includedir}/linuxcnc \
    ${sysconfdir}/linuxcnc \
    ${base_libdir}/linuxcnc \
    /etc/X11/app-defaults \
"

RDEPENDS_${PN} += " \
    bash \
    python3-core \
    python3-modules \
    python3-pyqt5 \
    tcl \
    tk \
"

INSANE_SKIP_${PN} += "already-stripped dev-so file-rdeps ldflags"
ERROR_QA_remove += "compile-host-path"
WARN_QA_append += " compile-host-path"
