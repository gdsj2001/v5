ROOTFS_POSTPROCESS_COMMAND += " v5_sd_rootfs_fixups; "

v5_sd_rootfs_fixups () {
    if [ -f ${IMAGE_ROOTFS}/etc/inittab ]; then
        sed -i 's#^PS0:.*#PS0:12345:respawn:/bin/start_getty 115200 ttyPS0 vt102#' ${IMAGE_ROOTFS}/etc/inittab || true
    fi

    rm -f ${IMAGE_ROOTFS}/etc/rpm-postinsts/100-sysvinit-inittab || true
    rm -f ${IMAGE_ROOTFS}/etc/rcS.d/S99run-postinsts || true

    if [ -x ${IMAGE_ROOTFS}/etc/init.d/S20v5-sd-mount-policy ]; then
        ln -snf ../init.d/S20v5-sd-mount-policy ${IMAGE_ROOTFS}/etc/rcS.d/S20v5-sd-mount-policy
        ln -snf ../init.d/S20v5-sd-mount-policy ${IMAGE_ROOTFS}/etc/rc5.d/S20v5-sd-mount-policy
    fi

    if [ -x ${IMAGE_ROOTFS}/etc/init.d/S99v5-net ]; then
        ln -snf ../init.d/S99v5-net ${IMAGE_ROOTFS}/etc/rcS.d/S40v5-net
        ln -snf ../init.d/S99v5-net ${IMAGE_ROOTFS}/etc/rc5.d/S99v5-net
    fi
}
