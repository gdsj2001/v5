FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

# Keep SD device tree aligned with the currently validated QSPI runtime tree.
SRC_URI += " file://system-top.dts"
DT_FILES_PATH = "${WORKDIR}"

# Match QSPI DTB byte layout (no extra reserve/padding flags).
DTC_FLAGS = ""
DTC_BFLAGS = ""

# Use static DTS input; do not regenerate from XSA in this recipe path.
do_configure[noexec] = "1"
