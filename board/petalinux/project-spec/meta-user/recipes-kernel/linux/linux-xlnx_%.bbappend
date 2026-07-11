FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"

LINUX_KERNEL_TYPE = "preempt-rt"
LINUX_VERSION_EXTENSION = "-rt1-xilinx-v2020.2"
KERNEL_FEATURES_append = " features/rt/rt.scc"

SRC_URI += " \
    file://display.cfg \
    file://v5-rt.cfg \
    file://xlnx_atk_lcd.c \
    file://pwm-dglnt.c \
    file://0001-drm-xlnx-add-atk-lcd.patch \
    file://0003-v5-usb-genesys-hub-slow-enable-wait.patch \
"

do_patch_append() {
    install -m 0644 ${WORKDIR}/xlnx_atk_lcd.c ${S}/drivers/gpu/drm/xlnx/xlnx_atk_lcd.c
    install -m 0644 ${WORKDIR}/pwm-dglnt.c ${S}/drivers/pwm/pwm-dglnt.c

    if ! grep -q '^config PWM_DGLNT' ${S}/drivers/pwm/Kconfig; then
        awk '
            BEGIN { added=0 }
            {
                if (!added && $0 ~ /^config PWM_SYSFS$/) {
                    print "config PWM_DGLNT";
                    print "\ttristate \"Digilent AXI PWM driver support\"";
                    print "\tdepends on COMMON_CLK";
                    print "\thelp";
                    print "\t  Simple Driver for Digilent AXI_PWM IP Core.";
                    print "";
                    print "\t  To compile this driver as a module, choose M here: the module";
                    print "\t  will be called pwm-dglnt.c";
                    print "";
                    added=1;
                }
                print $0;
            }
        ' ${S}/drivers/pwm/Kconfig > ${S}/drivers/pwm/Kconfig.v5 && \
        mv ${S}/drivers/pwm/Kconfig.v5 ${S}/drivers/pwm/Kconfig
    fi

    if ! grep -q 'CONFIG_PWM_DGLNT' ${S}/drivers/pwm/Makefile; then
        sed -i '/CONFIG_PWM_SYSFS/a obj-$(CONFIG_PWM_DGLNT)\t\t+= pwm-dglnt.o' ${S}/drivers/pwm/Makefile
    fi

    # The RT workqueue raw-lock patch has one rescuer_thread hunk whose
    # context differs in Xilinx 2020.2.  Apply the same raw-lock conversion to
    # any remaining pool/mayday workqueue locks after the RT patch stack lands.
    if [ -f ${S}/kernel/workqueue.c ]; then
        sed -i \
            -e 's/spin_lock_irq(&wq_mayday_lock)/raw_spin_lock_irq(\&wq_mayday_lock)/g' \
            -e 's/spin_unlock_irq(&wq_mayday_lock)/raw_spin_unlock_irq(\&wq_mayday_lock)/g' \
            -e 's/spin_lock(&wq_mayday_lock)/raw_spin_lock(\&wq_mayday_lock)/g' \
            -e 's/spin_unlock(&wq_mayday_lock)/raw_spin_unlock(\&wq_mayday_lock)/g' \
            -e 's/spin_lock_irq(&pool->lock)/raw_spin_lock_irq(\&pool->lock)/g' \
            -e 's/spin_unlock_irq(&pool->lock)/raw_spin_unlock_irq(\&pool->lock)/g' \
            -e 's/spin_lock_irqsave(&pool->lock,/raw_spin_lock_irqsave(\&pool->lock,/g' \
            -e 's/spin_unlock_irqrestore(&pool->lock,/raw_spin_unlock_irqrestore(\&pool->lock,/g' \
            -e 's/spin_lock_irqsave(&pwq->pool->lock,/raw_spin_lock_irqsave(\&pwq->pool->lock,/g' \
            -e 's/spin_unlock_irqrestore(&pwq->pool->lock,/raw_spin_unlock_irqrestore(\&pwq->pool->lock,/g' \
            ${S}/kernel/workqueue.c
        sed -i 's/raw_raw_spin/raw_spin/g' ${S}/kernel/workqueue.c
    fi
}

do_kernel_metadata_append() {
    # Xilinx' ARM IPI handler already lacks printk_nmi_enter/exit in this tree,
    # so the upstream RT patch is an equivalent no-op and fails only on context.
    if [ -f ${S}/.kernel-meta/patch.queue ]; then
        sed -i '\#features/rt/arm-remove-printk_nmi_.patch#d' ${S}/.kernel-meta/patch.queue
        # Xilinx 2020.2 keeps the older PCP free path signature; this RT
        # latency optimization patch targets a later 5.4 page_alloc shape.
        sed -i '\#features/rt/Split-IRQ-off-and-zone-lock-while-freeing-pages-from.patch#d' ${S}/.kernel-meta/patch.queue
        sed -i '\#features/rt/mm-page_alloc-rt-friendly-per-cpu-pages.patch#d' ${S}/.kernel-meta/patch.queue
    fi

    # The PREEMPTION rename patch is still needed for the RT tree, but its
    # PowerPC-only oops banner hunk does not match Xilinx' 2020.2 source.  This
    # target is ARM/Zynq, so keep the generic/ARM RT changes and drop only that
    # non-target hunk.
    rt_preempt_patch=${S}/.kernel-meta/patches/features/rt/Use-CONFIG_PREEMPTION.patch
    if [ -f ${rt_preempt_patch} ]; then
        awk '
            /^diff --git a\/arch\/powerpc\/kernel\/traps.c / { skip=1; next }
            /^diff --git / { skip=0 }
            !skip { print }
        ' ${rt_preempt_patch} > ${rt_preempt_patch}.v5 && \
        mv ${rt_preempt_patch}.v5 ${rt_preempt_patch}
    fi

    rt_wq_patch=${S}/.kernel-meta/patches/features/rt/workqueue-Convert-the-locks-to-raw-type.patch
    if [ -f ${rt_wq_patch} ]; then
        awk '
            /^@@ .*rescuer_thread/ { skip=1; next }
            /^@@ .*destroy_workqueue/ { skip=1; next }
            /^@@ / { skip=0 }
            !skip { print }
        ' ${rt_wq_patch} > ${rt_wq_patch}.v5 && \
        mv ${rt_wq_patch}.v5 ${rt_wq_patch}
    fi

}
