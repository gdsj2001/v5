SUMMARY = "LinuxCNC 2.9.7 built from the Windows-owned V5 source tree"
SECTION = "v5/apps"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263"
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit pkgconfig externalsrc

FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += "file://v5_linuxcnc_runtime_allowlist.tsv"

V5_LINUXCNC_EXTERNAL_SOURCE ?= ""
V5_LINUXCNC_EXTERNAL_BUILD ?= ""
EXTERNALSRC = "${V5_LINUXCNC_EXTERNAL_SOURCE}"
EXTERNALSRC_BUILD = "${V5_LINUXCNC_EXTERNAL_BUILD}"
PV = "2.9.7+v5"

S = "${EXTERNALSRC}"
B = "${EXTERNALSRC_BUILD}"
do_configure[file-checksums] = " \
    ${S}/v5_linuxcnc_source_identity.json:True \
    ${WORKDIR}/v5_linuxcnc_runtime_allowlist.tsv:True \
"
# The configured build tree lives in the disposable overlay, so each single
# BitBake invocation must configure that fresh tree before compiling it.
do_configure[nostamp] = "1"

DEPENDS += " \
    boost \
    gettext \
    intltool-native \
    python3-yapps2-native \
    libtirpc \
    python3 \
    readline \
"

EXTRA_OECONF = " \
    --enable-v5-headless-runtime \
    --with-realtime=uspace \
    --disable-gtk \
    --disable-gtk2 \
    --disable-build-documentation \
    --without-libmodbus \
    --without-libusb-1.0 \
    --disable-check-runtime-deps \
    --disable-userspace-pci \
    --enable-non-distributable=yes \
"

do_configure() {
    if [ -z "${V5_LINUXCNC_EXTERNAL_SOURCE}" ] || [ -z "${V5_LINUXCNC_EXTERNAL_BUILD}" ] || [ ! -f ${S}/v5_linuxcnc_source_identity.json ]; then
        bbfatal "V5 LinuxCNC external source/build owner is unavailable"
    fi
    case "${B}" in
        "${S}"|"${S}"/*) bbfatal "V5 LinuxCNC build directory must remain outside the read-only source projection" ;;
    esac
    [ -f ${B}/autogen.sh ] || bbfatal "V5 LinuxCNC writable overlay build view is incomplete"
    grep -q 'v5_prepare_wrapped_rotary_target' ${S}/src/emc/motion/command.c || \
        bbfatal "V5 native rotary target owner missing"
    grep -q 'v5_wrapped_rotary_mask' ${S}/src/emc/motion/motion.c || \
        bbfatal "V5 native rotary mask owner missing"
    grep -q 'V5_HEADLESS_RUNTIME' ${S}/src/emc/usr_intf/axis/extensions/emcmodule.cc || \
        bbfatal "V5 headless Python native status binding owner missing"
    grep -q 'V5_HEADLESS_RUNTIME' ${S}/src/emc/usr_intf/axis/Submakefile || \
        bbfatal "V5 headless Python binding build owner missing"
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
        ${EXTRA_OECONF}

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
    oe_runmake V5_HEADLESS_RUNTIME=1
}

do_install() {
    cd ${B}
    full_install=${WORKDIR}/v5-linuxcnc-full-install
    rm -rf "$full_install"
    oe_runmake V5_HEADLESS_RUNTIME=1 DESTDIR=${D} install-software
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
        grep -Fq 'if test "xyes" != "xyes"; then' ${D}${bindir}/linuxcnc || \
            bbfatal "headless launcher configuration marker is missing"
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

    mv ${D} "$full_install"
    install -d ${D}

    v5_copy_required() {
        runtime_path="$1"
        case "$runtime_path" in
            /*) ;;
            *) bbfatal "runtime allowlist path is not absolute: $runtime_path" ;;
        esac
        case "$runtime_path" in
            *../*|*/..|*//*) bbfatal "runtime allowlist path is unsafe: $runtime_path" ;;
        esac
        source_path="$full_install$runtime_path"
        [ -e "$source_path" ] || [ -L "$source_path" ] || \
            bbfatal "required LinuxCNC runtime file is missing: $runtime_path"
        install -d "${D}$(dirname "$runtime_path")"
        cp -a "$source_path" "${D}$runtime_path"
    }

    runtime_count=0
    tab=$(printf '\t')
    while IFS="$tab" read -r runtime_path consumer; do
        [ -n "$runtime_path" ] || continue
        case "$runtime_path" in \#*) continue ;; esac
        [ -n "$consumer" ] || bbfatal "runtime allowlist consumer is missing: $runtime_path"
        v5_copy_required "$runtime_path"
        runtime_count=$(expr "$runtime_count" + 1)
    done < ${WORKDIR}/v5_linuxcnc_runtime_allowlist.tsv
    [ "$runtime_count" -eq 32 ] || \
        bbfatal "unexpected LinuxCNC runtime allowlist size: $runtime_count"

    install -d ${D}${includedir}
    cp -a "$full_install${includedir}/linuxcnc" ${D}${includedir}/linuxcnc
    v5_copy_required "${bindir}/halcompile"
    v5_copy_required "${datadir}/linuxcnc/Makefile.modinc"
    for development_path in \
        ${libdir}/liblinuxcnc.a \
        ${libdir}/liblinuxcnchal.so \
        ${libdir}/liblinuxcncini.so \
        ${libdir}/libnml.so \
        ${libdir}/libposemath.so \
        ${libdir}/libpyplugin.so \
        ${libdir}/librs274.so \
        ${libdir}/libtooldata.so
    do
        v5_copy_required "$development_path"
    done

    install -d ${D}${datadir}/v5-native
    install -m 0644 ${WORKDIR}/v5_linuxcnc_runtime_allowlist.tsv \
        ${D}${datadir}/v5-native/linuxcnc-runtime-allowlist.tsv
    install -m 0644 ${S}/v5_linuxcnc_source_identity.json \
        ${D}${datadir}/v5-native/v5_linuxcnc_source_identity.json
    runtime_hashes=${D}${datadir}/v5-native/linuxcnc-runtime-files.sha256
    : > "$runtime_hashes"
    identity=${D}${datadir}/v5-native/linuxcnc-source-identity.txt
    {
        echo 'schema=v5-linuxcnc-source-identity-v2'
        echo 'upstream_commit=472ef181410c0c0baac1d7005e9a6287d586db68'
        echo 'upstream_version=2.9.7'
        sha256sum ${S}/v5_linuxcnc_source_identity.json
        sha256sum ${WORKDIR}/v5_linuxcnc_runtime_allowlist.tsv
        sha256sum ${D}${libdir}/linuxcnc/modules/motmod.so
        sha256sum ${D}${bindir}/milltask
    } > "$identity"

    for forbidden_path in \
        ${D}${sysconfdir}/X11 \
        ${D}${libdir}/tcltk \
        ${D}${datadir}/doc/linuxcnc \
        ${D}${bindir}/axis \
        ${D}${bindir}/gmoccapy \
        ${D}${bindir}/qtvcp \
        ${D}${bindir}/touchy \
        ${D}${bindir}/pyvcp \
        ${D}${bindir}/gladevcp
    do
        [ ! -e "$forbidden_path" ] || \
            bbfatal "forbidden LinuxCNC GUI/runtime path entered the minimal package: $forbidden_path"
    done
    rm -rf "$full_install"
}

v5_refresh_runtime_hashes() {
    package_root="${PKGDEST}/${PN}"
    runtime_hashes="$package_root${datadir}/v5-native/linuxcnc-runtime-files.sha256"
    [ -f "$runtime_hashes" ] || \
        bbfatal "runtime hash manifest placeholder is missing from the final package"
    tab=$(printf '\t')
    (
        cd "$package_root"
        while IFS="$tab" read -r runtime_path consumer; do
            [ -n "$runtime_path" ] || continue
            case "$runtime_path" in \#*) continue ;; esac
            [ -e ".$runtime_path" ] || [ -L ".$runtime_path" ] || \
                bbfatal "final package is missing runtime hash target: $runtime_path"
            sha256sum ".$runtime_path"
        done < ${WORKDIR}/v5_linuxcnc_runtime_allowlist.tsv
    ) > "$runtime_hashes"
}

PACKAGEFUNCS_prepend = "v5_refresh_runtime_hashes "

FILES_${PN} = " \
    ${sysconfdir}/linuxcnc/rtapi.conf \
    ${bindir}/halcmd \
    ${bindir}/inivar \
    ${bindir}/io \
    ${bindir}/linuxcnc \
    ${bindir}/linuxcnc_module_helper \
    ${bindir}/linuxcncrsh \
    ${bindir}/linuxcncsvr \
    ${bindir}/milltask \
    ${bindir}/rtapi_app \
    ${bindir}/v5_native_hal_owner \
    ${libdir}/liblinuxcnchal.so.0 \
    ${libdir}/liblinuxcncini.so.0 \
    ${libdir}/libnml.so.0 \
    ${libdir}/libposemath.so.0 \
    ${libdir}/libpyplugin.so.0 \
    ${libdir}/librs274.so.0 \
    ${libdir}/libtooldata.so.0 \
    ${libdir}/linuxcnc/realtime \
    ${libdir}/linuxcnc/modules/hal_lib.so \
    ${libdir}/linuxcnc/modules/motmod.so \
    ${libdir}/linuxcnc/modules/tpmod.so \
    ${libdir}/linuxcnc/modules/homemod.so \
    ${libdir}/linuxcnc/modules/trivkins.so \
    ${libdir}/linuxcnc/modules/xyzac-trt-kins.so \
    ${libdir}/linuxcnc/modules/xyzbc-trt-kins.so \
    ${libdir}/linuxcnc/modules/bitslice.so \
    ${libdir}/linuxcnc/modules/mux2.so \
    ${libdir}/linuxcnc/modules/or2.so \
    ${libdir}/linuxcnc/modules/v5_safety_latch.so \
    ${libdir}/python3/dist-packages/linuxcnc.so \
    ${datadir}/linuxcnc/linuxcnc.nml \
    ${datadir}/v5-native/linuxcnc-runtime-allowlist.tsv \
    ${datadir}/v5-native/linuxcnc-runtime-files.sha256 \
    ${datadir}/v5-native/linuxcnc-source-identity.txt \
    ${datadir}/v5-native/v5_linuxcnc_source_identity.json \
"

FILES_${PN}-dev += " \
    ${bindir}/halcompile \
    ${datadir}/linuxcnc/Makefile.modinc \
"

RDEPENDS_${PN}-dev += "python3-core"

RDEPENDS_${PN} += " \
    bash \
    python3-core \
"

INSANE_SKIP_${PN} += "already-stripped dev-so file-rdeps ldflags"
ERROR_QA_remove += "compile-host-path"
WARN_QA_append += " compile-host-path"
