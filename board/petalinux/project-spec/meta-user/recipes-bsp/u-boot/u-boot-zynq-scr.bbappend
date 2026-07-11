FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://boot.cmd.default.ext4"

BOOTMODE = "default"
BOOTFILE_EXT = ".ext4"
#Make this value to "1" to skip appending base address to ddr offsets.
SKIP_APPEND_BASEADDR = "0"

KERNEL_IMAGE_zynq = "uImage"
KERNEL_IMAGE_zynqmp = "Image"
KERNEL_IMAGE_versal = "Image"

KERNEL_BOOTCMD_zynq = "bootm"
KERNEL_BOOTCMD_zynqmp = "booti"
KERNEL_BOOTCMD_versal = "booti"

DEVICETREE_ADDRESS_zynq = "${@append_baseaddr(d,"0x100000")}"
DEVICETREE_ADDRESS_zynqmp = "${@append_baseaddr(d,"0x100000")}"
DEVICETREE_ADDRESS_versal = "${@append_baseaddr(d,"0x1000")}"

KERNEL_LOAD_ADDRESS_zynq = "${@append_baseaddr(d,"0x200000")}"
KERNEL_LOAD_ADDRESS_zynqmp = "${@append_baseaddr(d,"0x200000")}"
KERNEL_LOAD_ADDRESS_versal = "${@append_baseaddr(d,"0x80000")}"

## Below offsets and sizes are based on 32MB QSPI Memory for zynq
## For zynq
## Load boot.scr at 0xFC0000 -> 15MB of QSPI/NAND Memory
QSPI_KERNEL_OFFSET_zynq = "0x1000000"
NAND_KERNEL_OFFSET_zynq = "0x1000000"

QSPI_KERNEL_SIZE_zynq = "0x500000"

NAND_KERNEL_SIZE = "0x3200000"

## Below offsets and sizes are based on 128MB QSPI Memory for zynqmp/versal
## For zynqMP
## Load boot.scr at 0x3E80000 -> 62MB of QSPI/NAND Memory
QSPI_KERNEL_OFFSET = "0xF00000"
QSPI_KERNEL_OFFSET_zynqmpdr = "0x3F00000"
NAND_KERNEL_OFFSET_zynqmp = "0x4100000"

QSPI_KERNEL_SIZE_zynqmp = "0x1D00000"

## For versal
## Load boot.scr at 0x7F80000 -> 127MB of QSPI/NAND Memory
QSPI_KERNEL_OFFSET_versal = "0xF00000"
NAND_KERNEL_OFFSET_versal = "0x4100000"

QSPI_KERNEL_SIZE_versal = "0x1D00000"

QSPI_KERNEL_IMAGE_zynq = "image.ub"
QSPI_KERNEL_IMAGE_zynqmp = "image.ub"
QSPI_KERNEL_IMAGE_versal = "image.ub"

NAND_KERNEL_IMAGE = "image.ub"

FIT_IMAGE_LOAD_ADDRESS = "${@append_baseaddr(d,"0x10000000")}"

QSPI_FIT_IMAGE_LOAD_ADDRESS = "${@append_baseaddr(d,"0x10000000")}"
QSPI_FIT_IMAGE_SIZE = "0x6400000"
QSPI_FIT_IMAGE_SIZE_zynqmpdr = "0x3F00000"
QSPI_FIT_IMAGE_SIZE_zynq = "0xF00000"

NAND_FIT_IMAGE_LOAD_ADDRESS = "${@append_baseaddr(d,"0x10000000")}"
NAND_FIT_IMAGE_SIZE = "0x6400000"

FIT_IMAGE = "image.ub"

python () {
    baseaddr = d.getVar('DDR_BASEADDR') or "0x0"
    if baseaddr == "0x0":
        d.setVar('PRE_BOOTENV','')
    else:
        soc_family = d.getVar('SOC_FAMILY') or ""
        if soc_family == "zynqmp":
            fdt_high = "0x10000000"
        elif soc_family == "zynq":
            fdt_high = "0x20000000"
        elif soc_family == "versal":
            fdt_high = "0x70000000"

        if fdt_high:
            basefdt_high = append_baseaddr(d,fdt_high)
            bootenv = "setenv fdt_high " + basefdt_high
            d.setVar('PRE_BOOTENV',bootenv)
}

def append_baseaddr(d,offset):
    skip_append = d.getVar('SKIP_APPEND_BASEADDR') or ""
    if skip_append == "1":
        return offset
    import subprocess
    baseaddr = d.getVar('DDR_BASEADDR') or "0x0"
    subcmd = "$((%s+%s));" % (baseaddr,offset)
    cmd = "printf '0x%08x' " + str(subcmd)
    output = subprocess.check_output(cmd, shell=True).decode("utf-8")
    return output

do_compile_prepend() {
	sed -e 's/@@QSPI_KERNEL_OFFSET@@/${QSPI_KERNEL_OFFSET}/' \
	    -e 's/@@NAND_KERNEL_OFFSET@@/${NAND_KERNEL_OFFSET}/' \
	    -e 's/@@QSPI_KERNEL_SIZE@@/${QSPI_KERNEL_SIZE}/' \
	    -e 's/@@NAND_KERNEL_SIZE@@/${NAND_KERNEL_SIZE}/' \
	    -e 's/@@KERNEL_IMAGE@@/${KERNEL_IMAGE}/' \
	    -e 's/@@QSPI_KERNEL_IMAGE@@/${QSPI_KERNEL_IMAGE}/' \
	    -e 's/@@NAND_KERNEL_IMAGE@@/${NAND_KERNEL_IMAGE}/' \
	    -e 's/@@QSPI_FIT_IMAGE_LOAD_ADDRESS@@/${QSPI_FIT_IMAGE_LOAD_ADDRESS}/' \
	    -e 's/@@FIT_IMAGE_LOAD_ADDRESS@@/${FIT_IMAGE_LOAD_ADDRESS}/' \
	    -e 's/@@QSPI_FIT_IMAGE_SIZE@@/${QSPI_FIT_IMAGE_SIZE}/' \
	    -e 's/@@NAND_FIT_IMAGE_LOAD_ADDRESS@@/${NAND_FIT_IMAGE_LOAD_ADDRESS}/' \
	    -e 's/@@NAND_FIT_IMAGE_SIZE@@/${NAND_FIT_IMAGE_SIZE}/' \
	    -e 's/@@FIT_IMAGE@@/${FIT_IMAGE}/' \
	    -e 's/@@PRE_BOOTENV@@/${PRE_BOOTENV}/' \
	    "${WORKDIR}/boot.cmd.${BOOTMODE}${BOOTFILE_EXT}" > "${WORKDIR}/boot.cmd.${BOOTMODE}.${SOC_FAMILY}"
}
