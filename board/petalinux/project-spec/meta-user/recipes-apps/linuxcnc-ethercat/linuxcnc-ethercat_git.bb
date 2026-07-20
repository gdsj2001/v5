SUMMARY = "LinuxCNC EtherCAT HAL driver (lcec)"
SECTION = "v5/apps"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://LICENSE;md5=8264535c0c4e9c6c335635c4026a8022"
PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = " \
    git://github.com/linuxcnc-ethercat/linuxcnc-ethercat.git;protocol=https;branch=master \
    file://0001-v5-register-pdo-outputs-before-inputs.patch \
    file://0002-v5-dc-reference-health-pins.patch \
    file://0003-v5-require-initf-master-activation.patch \
"
SRCREV = "de7e377f76873fa99e8ea5dcafd7df916e118024"
PV = "1.0+git${SRCPV}"

S = "${WORKDIR}/git"
B = "${WORKDIR}/build"

DEPENDS += "linuxcnc-prebuilt ethercat-master expat"

do_configure[noexec] = "1"

do_compile() {
    cp -a ${S}/. ${B}/
    python3 - <<'PY'
from pathlib import Path

path = Path("${B}/src/lcec_main.c")
text = path.read_bytes().decode("utf-8").replace("\r\n", "\n")
old = """    lcec_pdo_entry_reg_t *master_regs = lcec_allocate_pdo_entry_reg(pdo_entry_count + 1);
    for (slave = master->first_slave; slave != NULL; slave = slave->next) {
      if (lcec_append_pdo_entry_reg(master_regs, slave->regs) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure to append PDO entries for slave %s.%s\\n", master->name, slave->name);
        goto fail2;
      }
    }
"""
new = """    lcec_pdo_entry_reg_t *master_regs = lcec_allocate_pdo_entry_reg(pdo_entry_count + 1);
    for (slave = master->first_slave; slave != NULL; slave = slave->next) {
      if (lcec_append_pdo_entry_reg_by_dir(master_regs, slave->regs, slave, EC_DIR_OUTPUT) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure to append output PDO entries for slave %s.%s\\n", master->name, slave->name);
        goto fail2;
      }
    }
    for (slave = master->first_slave; slave != NULL; slave = slave->next) {
      if (lcec_append_pdo_entry_reg_by_dir(master_regs, slave->regs, slave, EC_DIR_INPUT) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure to append input PDO entries for slave %s.%s\\n", master->name, slave->name);
        goto fail2;
      }
    }
    for (slave = master->first_slave; slave != NULL; slave = slave->next) {
      if (lcec_append_pdo_entry_reg_by_dir(master_regs, slave->regs, slave, EC_DIR_INVALID) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure to append PDO entries for slave %s.%s\\n", master->name, slave->name);
        goto fail2;
      }
    }
"""
if old not in text:
    raise SystemExit("V5 PDO direction ordering patch target not found in lcec_main.c")
text = text.replace(old, new, 1)
old = """  LCEC_CONF_MASTER_T *master_conf;
  LCEC_CONF_SLAVE_T *slave_conf;
  LCEC_CONF_DC_T *dc_conf;
  LCEC_CONF_WATCHDOG_T *wd_conf;
  LCEC_CONF_SYNCMANAGER_T *sm_conf;
  LCEC_CONF_PDO_T *pdo_conf;
  LCEC_CONF_PDOENTRY_T *pe_conf;
  LCEC_CONF_COMPLEXENTRY_T *ce_conf;
  LCEC_CONF_SDOCONF_T *sdo_conf;
  LCEC_CONF_IDNCONF_T *idn_conf;
  LCEC_CONF_MODPARAM_T *modparam_conf;
"""
new = """  LCEC_CONF_MASTER_T *master_conf;
  LCEC_CONF_MASTER_T master_conf_storage;
  LCEC_CONF_SLAVE_T *slave_conf;
  LCEC_CONF_SLAVE_T slave_conf_storage;
  LCEC_CONF_DC_T *dc_conf;
  LCEC_CONF_DC_T dc_conf_storage;
  LCEC_CONF_WATCHDOG_T *wd_conf;
  LCEC_CONF_WATCHDOG_T wd_conf_storage;
  LCEC_CONF_SYNCMANAGER_T *sm_conf;
  LCEC_CONF_SYNCMANAGER_T sm_conf_storage;
  LCEC_CONF_PDO_T *pdo_conf;
  LCEC_CONF_PDO_T pdo_conf_storage;
  LCEC_CONF_PDOENTRY_T *pe_conf;
  LCEC_CONF_PDOENTRY_T pe_conf_storage;
  LCEC_CONF_COMPLEXENTRY_T *ce_conf;
  LCEC_CONF_COMPLEXENTRY_T ce_conf_storage;
  LCEC_CONF_SDOCONF_T *sdo_conf;
  LCEC_CONF_IDNCONF_T *idn_conf;
  LCEC_CONF_MODPARAM_T *modparam_conf;
  LCEC_CONF_MODPARAM_T modparam_conf_storage;
"""
if old not in text:
    raise SystemExit("V5 unaligned config storage declaration target not found in lcec_main.c")
text = text.replace(old, new, 1)
fixed_token_replacements = [
    ("master_conf", "LCEC_CONF_MASTER_T", "master_conf_storage"),
    ("slave_conf", "LCEC_CONF_SLAVE_T", "slave_conf_storage"),
    ("dc_conf", "LCEC_CONF_DC_T", "dc_conf_storage"),
    ("wd_conf", "LCEC_CONF_WATCHDOG_T", "wd_conf_storage"),
    ("sm_conf", "LCEC_CONF_SYNCMANAGER_T", "sm_conf_storage"),
    ("pdo_conf", "LCEC_CONF_PDO_T", "pdo_conf_storage"),
    ("modparam_conf", "LCEC_CONF_MODPARAM_T", "modparam_conf_storage"),
]
for var, ctype, storage in fixed_token_replacements:
    old = f"""        // get config token
        {var} = ({ctype} *)conf;
        conf += sizeof({ctype});
"""
    new = f"""        // get config token
        memcpy(&{storage}, conf, sizeof({storage}));
        {var} = &{storage};
        conf += sizeof({storage});
"""
    if old not in text:
        raise SystemExit(f"V5 unaligned {ctype} config patch target not found in lcec_main.c")
    text = text.replace(old, new, 1)
old = """        // get config token
        pe_conf = (LCEC_CONF_PDOENTRY_T *)conf;
        conf += sizeof(LCEC_CONF_PDOENTRY_T);
"""
new = """        // get config token
        // lcec_conf writes a compact shared-memory stream. On ARM, direct
        // double/hal_float_t loads from that stream can be unaligned and trap.
        // Copy this token to aligned stack storage before reading its fields.
        memcpy(&pe_conf_storage, conf, sizeof(pe_conf_storage));
        pe_conf = &pe_conf_storage;
        conf += sizeof(pe_conf_storage);
"""
if old not in text:
    raise SystemExit("V5 unaligned PDO entry config patch target not found in lcec_main.c")
text = text.replace(old, new, 1)
old = """        // get config token
        ce_conf = (LCEC_CONF_COMPLEXENTRY_T *)conf;
        conf += sizeof(LCEC_CONF_COMPLEXENTRY_T);
"""
new = """        // get config token
        // Same packed-stream rule as PDO entry above.
        memcpy(&ce_conf_storage, conf, sizeof(ce_conf_storage));
        ce_conf = &ce_conf_storage;
        conf += sizeof(ce_conf_storage);
"""
if old not in text:
    raise SystemExit("V5 unaligned complex entry config patch target not found in lcec_main.c")
text = text.replace(old, new, 1)
# Keep this recipe as the minimal runtime fix: do not gate process-data
# queueing or DC sync here. A broader domain/prequeue/DC candidate was
# tested on the board and broke the stable 2 ms product path.
with path.open("w", encoding="utf-8", newline="\n") as fh:
    fh.write(text)
PY
    cp ${RECIPE_SYSROOT}${datadir}/linuxcnc/Makefile.modinc ${B}/v5.Makefile.modinc
    sed -i "s@\$(Q)ld -d -r@\$(Q)${LD} -d -r@g" ${B}/v5.Makefile.modinc ${B}/src/Makefile
    sed -i "s@\$(Q)objdump @\$(Q)${OBJDUMP} @g" ${B}/v5.Makefile.modinc
    sed -i "s@\$(Q)objcopy @\$(Q)${OBJCOPY} @g" ${B}/src/Makefile
    sed -i "s@^\tld -d -r@\t${LD} -d -r@g" ${B}/src/Makefile
    sed -i "s@^\tobjcopy @\t${OBJCOPY} @g" ${B}/src/Makefile
    sed -i "s@-Wl,-rpath,\$(LIBDIR)@@g" ${B}/src/Makefile
    sed -i "s@-L\$(LIBDIR)@-L${RECIPE_SYSROOT}${libdir}@g" ${B}/src/Makefile
    sed -i "s@-I/usr/include/linuxcnc@-I${RECIPE_SYSROOT}${includedir}/linuxcnc -I${RECIPE_SYSROOT}${includedir}@g" ${B}/v5.Makefile.modinc
    cat > ${B}/config.mk <<EOF
COMP = ${RECIPE_SYSROOT}${bindir}/halcompile
MODINC = ${B}/v5.Makefile.modinc
BUILDSYS = uspace
KERNELDIR =
CC = ${CC}
RTAI =
RTFLAGS =
KERNELRELEASE =
EXTRA_CFLAGS = ${CFLAGS} -DUSPACE -fno-fast-math -DRTAPI -D_GNU_SOURCE -Drealtime -D__MODULE__ -DSIM -fPIC -I. -I${RECIPE_SYSROOT}${includedir}/linuxcnc -I${RECIPE_SYSROOT}${includedir}
USE_RTLIBM =
EMC2_HOME = /usr
RUN_IN_PLACE = no
RTLIBDIR = /usr/lib/linuxcnc/modules
LIBDIR = ${libdir}
prefix = /usr
EOF
    oe_runmake -C ${B}/src all \
        MODINC=${B}/v5.Makefile.modinc \
        CC="${CC}" \
        RTLIBDIR=/usr/lib/linuxcnc/modules \
        LIBDIR=${libdir} \
        EMC2_HOME=/usr
}

do_install() {
    install -d ${D}/usr/lib/linuxcnc/modules
    install -m 0644 ${B}/src/lcec.so ${D}/usr/lib/linuxcnc/modules/lcec.so
    chmod 0644 ${D}/usr/lib/linuxcnc/modules/lcec.so

    install -d ${D}${bindir}
    install -m 0755 ${B}/src/lcec_conf ${D}${bindir}/lcec_conf
}

FILES_${PN} += " \
    /usr/lib/linuxcnc/modules/lcec.so \
    ${bindir}/lcec_conf \
"

RDEPENDS_${PN} += "ethercat-master linuxcnc-prebuilt"

INSANE_SKIP_${PN} += "already-stripped ldflags"
