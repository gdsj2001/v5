SUMMARY = "V5 board base filesystem overlay"
SECTION = "v5/base"
LICENSE = "CLOSED"
PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = " \
    file://init/S20v5-sd-mount-policy \
    file://network/S99v5-net \
    file://network/v5_net_core.sh \
    file://network/v5_wifi_core.sh \
    file://network/v5_usb_wifi_apply.sh \
    file://network/rtl8188eufw.bin \
    file://network/authorized_keys \
    file://udev/99-uio.rules \
    file://udev/99-touchscreen.rules \
    file://rootfs-overlay/etc/6x-cnc/vps_endpoints.json \
"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/init/S20v5-sd-mount-policy ${D}${sysconfdir}/init.d/S20v5-sd-mount-policy
    install -m 0755 ${WORKDIR}/network/S99v5-net ${D}${sysconfdir}/init.d/S99v5-net

    install -d ${D}/usr/local/sbin
    install -m 0644 ${WORKDIR}/network/v5_net_core.sh ${D}/usr/local/sbin/v5_net_core.sh
    install -m 0644 ${WORKDIR}/network/v5_wifi_core.sh ${D}/usr/local/sbin/v5_wifi_core.sh
    install -m 0755 ${WORKDIR}/network/v5_usb_wifi_apply.sh ${D}/usr/local/sbin/v5_usb_wifi_apply.sh

    install -d ${D}/lib/firmware/rtlwifi
    install -m 0644 ${WORKDIR}/network/rtl8188eufw.bin ${D}/lib/firmware/rtlwifi/rtl8188eufw.bin

    install -d -m 0700 ${D}/home/root/.ssh
    install -m 0600 ${WORKDIR}/network/authorized_keys ${D}/home/root/.ssh/authorized_keys

    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/udev/99-uio.rules ${D}${sysconfdir}/udev/rules.d/99-uio.rules
    install -m 0644 ${WORKDIR}/udev/99-touchscreen.rules ${D}${sysconfdir}/udev/rules.d/99-touchscreen.rules

    install -d ${D}${sysconfdir}/6x-cnc
    install -m 0644 ${WORKDIR}/rootfs-overlay/etc/6x-cnc/vps_endpoints.json ${D}${sysconfdir}/6x-cnc/vps_endpoints.json
}

FILES_${PN} += " \
    ${sysconfdir}/init.d/S20v5-sd-mount-policy \
    ${sysconfdir}/init.d/S99v5-net \
    /usr/local/sbin/v5_net_core.sh \
    /usr/local/sbin/v5_wifi_core.sh \
    /usr/local/sbin/v5_usb_wifi_apply.sh \
    /lib/firmware/rtlwifi/rtl8188eufw.bin \
    /home/root/.ssh/authorized_keys \
    ${sysconfdir}/udev/rules.d/99-uio.rules \
    ${sysconfdir}/udev/rules.d/99-touchscreen.rules \
    ${sysconfdir}/6x-cnc/vps_endpoints.json \
"

RDEPENDS_${PN} += "dropbear wpa-supplicant wpa-supplicant-cli python3-typing e2fsprogs-resize2fs"
