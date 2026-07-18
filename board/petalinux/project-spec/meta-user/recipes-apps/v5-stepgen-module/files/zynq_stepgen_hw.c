/*
 * zynq_stepgen_hw - HAL component for Zynq StepGen FPGA slice executor driver (v3.5)
 *
 * Target: Linux v5-rt 5.4.0-rt1-xilinx (armv7l) / Debian 12
 */

#include <rtapi.h>
#include <rtapi_app.h>
#include <hal.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

MODULE_AUTHOR("V5 Project");
MODULE_DESCRIPTION("Zynq StepGen FPGA slice executor driver");
MODULE_LICENSE("GPL");

#define V5_STEPGEN_BUILD_TAG "wrapped-rotary-command-reset-20260426"
#define V5_WRAPPED_ROTARY_RESET_TOLERANCE_DEG 0.01
#define V5_STEPGEN_UIO_PATH "/dev/v5-stepgen-uio"

#define STEPGEN_BASE    0x41240000
#define STEPGEN_SIZE    0x1000
#define MAX_AXES        6

#define mmio_commit_barrier() __asm__ __volatile__ ("dsb st" : : : "memory")

/* Global register offsets (v3.5 slice executor) */
#define REG_ID               0x000
#define REG_VERSION          0x004
#define REG_CLK_FREQ         0x008
#define REG_N_AXES           0x00C
#define REG_EXEC_STATUS      0x010
#define REG_GLOBAL_APPLY     0x014
#define REG_SLICE_TICKS      0x018
#define REG_EVENT_CNT        0x01C
#define REG_GLOBAL_CFG       0x020
#define REG_SLICE_SEQ_SHADOW 0x024

/* Axis registers */
#define AXIS_BASE(axis)      (0x100 + (axis) * 0x20)
#define REG_CONTROL          0x00
#define REG_DELTA_STEPS      0x04
#define REG_STEP_WIDTH       0x08
#define REG_STEP_SPACE       0x0C
#define REG_ENC_COUNT        0x10
#define REG_STATUS           0x14
#define REG_DIR_SETUP        0x18
#define REG_DIR_HOLD         0x1C

/* Control bits */
#define CTRL_ENABLE          (1u << 0)
#define CTRL_DIR             (1u << 1)  /* legacy mode: instantaneous direction output */
#define CTRL_DIR_INVERT      (1u << 1)
#define CTRL_INDEX_EN        (1u << 3)

/* EXEC_STATUS bits */
#define EXEC_BUSY            (1u << 0)
#define EXEC_DONE            (1u << 1)
#define EXEC_FAULT           (1u << 2)
#define EXEC_OVERRUN         (1u << 3)
#define EXEC_APPLY_BUSY      (1u << 4)
#define EXEC_INVALID_SLICE   (1u << 5)

/* GLOBAL_CFG bits */
#define CFG_FIRST_STEP_SYNC  (1u << 0)
#define CFG_ENC_LOOPBACK_DBG (1u << 1)

/* Legacy v2.x compatibility (same offsets, different semantics) */
#define REG_WATCHDOG         REG_EXEC_STATUS
#define REG_GLOBAL_SYNC      REG_GLOBAL_APPLY
#define LEGACY_VERSION_CUTOFF 0x00030000u

typedef struct {
    hal_float_t *pos_cmd;
    hal_float_t *vel_cmd;      /* compatibility input, not used by slice scheduler */
    hal_float_t *pos_fb;
    hal_float_t *scale;
    hal_bit_t   *enable;
    hal_bit_t   *index_enable;
    hal_bit_t   *index_seen;
    hal_s32_t   *enc_count;
    hal_bit_t   *delta_clamped;

    hal_u32_t step_width_ns;
    hal_u32_t step_space_ns;
    hal_u32_t dir_setup_ns;
    hal_u32_t dir_hold_ns;
    hal_u32_t dir_invert;
    hal_u32_t wrapped_rotary;
    uint32_t last_target_dir;

    uint32_t last_step_width_ticks;
    uint32_t last_step_space_ticks;
    uint32_t last_dir_setup_ticks;
    uint32_t last_dir_hold_ticks;

    int64_t last_cmd_steps;
    int64_t pending_steps;
    int64_t fb_steps;
    double old_pos;

    /* Legacy v2 shadow tracking: avoid unnecessary GLOBAL_SYNC retriggers. */
    uint32_t legacy_last_control;
    uint32_t legacy_last_freq_word;
    uint32_t legacy_guard_target_dir;
    uint32_t legacy_dir_guard_state;
    int legacy_shadow_valid;
} axis_data_t;

static int comp_id;
static int fd;
static void *map_base;
static volatile uint32_t *regs;
static axis_data_t *axis_data;
static uint32_t clk_freq = 100000000;
static uint32_t hw_axes = MAX_AXES;
static size_t map_size = STEPGEN_SIZE;
static uint32_t slice_seq_shadow = 0;
static uint32_t hw_version = 0;
static int legacy_mode = 0;

/* Global HAL pins */
static hal_bit_t **watchdog_ok_ptr;
static hal_bit_t **exec_busy_ptr;
static hal_bit_t **exec_done_ptr;
static hal_bit_t **exec_fault_ptr;
static hal_bit_t **exec_overrun_ptr;
static hal_bit_t **exec_apply_busy_ptr;
static hal_bit_t **exec_invalid_slice_ptr;
static hal_bit_t **exec_clear_status_ptr;
static hal_bit_t **first_step_sync_ptr;
static hal_bit_t **enc_loopback_debug_ptr;
static hal_bit_t **exec_delta_clamp_any_ptr;

static int prev_exec_clear_status = 0;

static inline void zynq_write_reg(int offset, uint32_t value)
{
    if (regs)
        regs[offset / 4] = value;
}

static uint32_t ns_to_ticks(uint32_t ns)
{
    uint64_t ticks;

    if (ns == 0)
        return 1;

    ticks = ((uint64_t)ns * (uint64_t)clk_freq + 999999999ULL) / 1000000000ULL;
    if (ticks == 0)
        ticks = 1;
    if (ticks > 0xFFFFFFFFULL)
        ticks = 0xFFFFFFFFULL;

    return (uint32_t)ticks;
}

static int read_hex_file(const char *path, uint32_t *value)
{
    FILE *fp;
    char buf[32];
    char *p;

    fp = fopen(path, "r");
    if (!fp)
        return -1;
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    p = buf;
    if (strncmp(p, "0x", 2) == 0 || strncmp(p, "0X", 2) == 0)
        p += 2;
    *value = (uint32_t)strtoul(p, NULL, 16);
    return 0;
}

static int read_text_file(const char *path, char *value, size_t value_size)
{
    FILE *fp;
    size_t length;

    if (!path || !value || value_size < 2)
        return -1;
    fp = fopen(path, "r");
    if (!fp)
        return -1;
    if (!fgets(value, value_size, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    length = strlen(value);
    while (length > 0 && (value[length - 1] == '\n' || value[length - 1] == '\r'))
        value[--length] = '\0';
    return length > 0 ? 0 : -1;
}

typedef struct {
    dev_t st_dev;
    ino_t st_ino;
    dev_t st_rdev;
} v5_uio_identity_t;

static int validate_stepgen_uio_device(char *node, size_t node_size,
                                       v5_uio_identity_t *identity)
{
    char resolved[128];
    char name_path[160];
    char name[128];
    const char *base;
    const char *digit;
    struct stat link_info;
    struct stat target_info;
    struct group *petalinux;

    if (!node || node_size < 5 || !identity)
        return -1;
    if (lstat(V5_STEPGEN_UIO_PATH, &link_info) != 0 || !S_ISLNK(link_info.st_mode))
        return -1;
    if (stat(V5_STEPGEN_UIO_PATH, &target_info) != 0 || !S_ISCHR(target_info.st_mode))
        return -1;
    petalinux = getgrnam("petalinux");
    if (!petalinux || target_info.st_uid != 0 || target_info.st_gid != petalinux->gr_gid ||
        (target_info.st_mode & 07777) != 0660)
        return -1;
    if (!realpath(V5_STEPGEN_UIO_PATH, resolved))
        return -1;
    base = strrchr(resolved, '/');
    if (!base || base - resolved != 4 || strncmp(resolved, "/dev", 4) != 0)
        return -1;
    base++;
    if (strncmp(base, "uio", 3) != 0 || !base[3])
        return -1;
    for (digit = base + 3; *digit; digit++) {
        if (*digit < '0' || *digit > '9')
            return -1;
    }
    if (snprintf(node, node_size, "%s", base) >= (int)node_size)
        return -1;
    if (snprintf(name_path, sizeof(name_path), "/sys/class/uio/%s/name", node) >= (int)sizeof(name_path))
        return -1;
    if (read_text_file(name_path, name, sizeof(name)) != 0 || strstr(name, "stepgen") == NULL)
        return -1;
    identity->st_dev = target_info.st_dev;
    identity->st_ino = target_info.st_ino;
    identity->st_rdev = target_info.st_rdev;
    return 0;
}

static int validate_stepgen_uio_fd(int device_fd, const v5_uio_identity_t *identity)
{
    struct stat target_info;
    struct group *petalinux;

    if (!identity || fstat(device_fd, &target_info) != 0 || !S_ISCHR(target_info.st_mode))
        return -1;
    petalinux = getgrnam("petalinux");
    if (!petalinux || target_info.st_uid != 0 || target_info.st_gid != petalinux->gr_gid ||
        (target_info.st_mode & 07777) != 0660 ||
        target_info.st_dev != identity->st_dev ||
        target_info.st_ino != identity->st_ino ||
        target_info.st_rdev != identity->st_rdev)
        return -1;
    return 0;
}

static uint32_t period_ns_to_ticks(long period_ns)
{
    uint64_t ticks;

    if (period_ns <= 0)
        return 1;

    ticks = ((uint64_t)period_ns * (uint64_t)clk_freq + 999999999ULL) / 1000000000ULL;
    if (ticks == 0)
        ticks = 1;
    if (ticks > 0xFFFFFFFFULL)
        ticks = 0xFFFFFFFFULL;

    return (uint32_t)ticks;
}

static int64_t units_to_steps(double pos, double scale)
{
    double raw;

    raw = pos * scale;
    if (raw >= 0.0)
        raw += 0.5;
    else
        raw -= 0.5;

    if (raw > 9223372036854774784.0)
        return 9223372036854774784LL;
    if (raw < -9223372036854774784.0)
        return -9223372036854774784LL;

    return (int64_t)raw;
}

static int64_t i64_abs(int64_t v)
{
    if (v < 0)
        return -v;
    return v;
}

static int64_t wrapped_rotary_reset_tolerance_steps(double scale)
{
    int64_t tolerance_steps;

    tolerance_steps = units_to_steps(V5_WRAPPED_ROTARY_RESET_TOLERANCE_DEG, fabs(scale));
    if (tolerance_steps < 4)
        tolerance_steps = 4;
    return tolerance_steps;
}

static int maybe_apply_wrapped_rotary_origin_reset(axis_data_t *ad,
                                                   int64_t cmd_delta,
                                                   int64_t cmd_steps,
                                                   double scale)
{
    int64_t rev_steps;
    int64_t abs_delta;
    int64_t turns;
    int64_t reset_error;
    int64_t tolerance_steps;

    if (!ad || !ad->wrapped_rotary)
        return 0;
    tolerance_steps = wrapped_rotary_reset_tolerance_steps(scale);
    rev_steps = units_to_steps(360.0, fabs(scale));
    if (rev_steps < 0)
        rev_steps = -rev_steps;
    if (rev_steps <= 0)
        return 0;

    abs_delta = i64_abs(cmd_delta);
    if (abs_delta + tolerance_steps < rev_steps)
        return 0;

    turns = (abs_delta + (rev_steps / 2)) / rev_steps;
    if (turns <= 0)
        return 0;

    reset_error = i64_abs(abs_delta - (turns * rev_steps));
    if (reset_error > tolerance_steps)
        return 0;

    /*
     * LinuxCNC motion emits this 360n jump only when it is normalizing the
     * logical origin of a wrapped rotary axis.  Treat it as a position reset
     * regardless of any open-loop FPGA backlog; otherwise the helper can turn
     * a native C=-1710 -> C=90 normalization into a fake multi-turn move and
     * trip following error on the next G53 return block.
     */
    ad->last_cmd_steps = cmd_steps;
    ad->pending_steps = 0;
    ad->fb_steps = cmd_steps;
    return 1;
}

static int32_t clamp_i32(int64_t v)
{
    if (v > 2147483647LL)
        return 2147483647;
    if (v < (-2147483647LL - 1LL))
        return (-2147483647 - 1);
    return (int32_t)v;
}

static uint32_t axis_max_steps_per_slice(const axis_data_t *ad, uint32_t slice_ticks, int first_step_sync)
{
    uint64_t gap;
    uint64_t max_steps;

    if (slice_ticks == 0)
        return 0;

    gap = (uint64_t)ad->last_step_width_ticks + (uint64_t)ad->last_step_space_ticks;
    if (gap == 0)
        gap = 1;

    if (first_step_sync) {
        max_steps = 1ULL + ((uint64_t)(slice_ticks - 1U) / gap);
    } else {
        max_steps = (uint64_t)slice_ticks / gap;
    }

    if (max_steps > 2147483647ULL)
        max_steps = 2147483647ULL;

    return (uint32_t)max_steps;
}

static int alloc_bit_pin(const char *name, int dir, hal_bit_t ***holder)
{
    int rv;

    *holder = hal_malloc(sizeof(hal_bit_t *));
    if (!*holder)
        return -ENOMEM;

    **holder = NULL;
    rv = hal_pin_bit_new(name, dir, *holder, comp_id);
    return rv;
}

static int export_axis_pins(int axis, axis_data_t *ad)
{
    int retval;
    char name[80];

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.pos-cmd", axis);
    retval = hal_pin_float_new(name, HAL_IN, &ad->pos_cmd, comp_id);
    if (retval < 0) return retval;

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.vel-cmd", axis);
    retval = hal_pin_float_new(name, HAL_IN, &ad->vel_cmd, comp_id);
    if (retval < 0) return retval;

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.pos-fb", axis);
    retval = hal_pin_float_new(name, HAL_OUT, &ad->pos_fb, comp_id);
    if (retval < 0) return retval;

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.scale", axis);
    retval = hal_pin_float_new(name, HAL_IN, &ad->scale, comp_id);
    if (retval < 0) return retval;

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.enable", axis);
    retval = hal_pin_bit_new(name, HAL_IN, &ad->enable, comp_id);
    if (retval < 0) return retval;

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.index-enable", axis);
    retval = hal_pin_bit_new(name, HAL_IN, &ad->index_enable, comp_id);
    if (retval < 0) return retval;

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.index-seen", axis);
    retval = hal_pin_bit_new(name, HAL_OUT, &ad->index_seen, comp_id);
    if (retval < 0) return retval;

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.enc-count", axis);
    retval = hal_pin_s32_new(name, HAL_OUT, &ad->enc_count, comp_id);
    if (retval < 0) return retval;

    snprintf(name, sizeof(name), "zynq_stepgen_hw.%d.delta-clamped", axis);
    retval = hal_pin_bit_new(name, HAL_OUT, &ad->delta_clamped, comp_id);
    if (retval < 0) return retval;

    retval = hal_param_u32_newf(HAL_RW, &ad->step_width_ns, comp_id,
        "zynq_stepgen_hw.%d.step-width-ns", axis);
    if (retval < 0) return retval;

    retval = hal_param_u32_newf(HAL_RW, &ad->step_space_ns, comp_id,
        "zynq_stepgen_hw.%d.step-space-ns", axis);
    if (retval < 0) return retval;

    retval = hal_param_u32_newf(HAL_RW, &ad->dir_setup_ns, comp_id,
        "zynq_stepgen_hw.%d.dir-setup-ns", axis);
    if (retval < 0) return retval;

    retval = hal_param_u32_newf(HAL_RW, &ad->dir_hold_ns, comp_id,
        "zynq_stepgen_hw.%d.dir-hold-ns", axis);
    if (retval < 0) return retval;

    retval = hal_param_u32_newf(HAL_RW, &ad->dir_invert, comp_id,
        "zynq_stepgen_hw.%d.dir-invert", axis);
    if (retval < 0) return retval;

    retval = hal_param_u32_newf(HAL_RW, &ad->wrapped_rotary, comp_id,
        "zynq_stepgen_hw.%d.wrapped-rotary", axis);
    if (retval < 0) return retval;

    return 0;
}

static int export_global_pins(void)
{
    int rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.watchdog-ok", HAL_OUT, &watchdog_ok_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-busy", HAL_OUT, &exec_busy_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-done", HAL_OUT, &exec_done_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-fault", HAL_OUT, &exec_fault_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-overrun", HAL_OUT, &exec_overrun_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-apply-while-busy", HAL_OUT, &exec_apply_busy_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-invalid-slice", HAL_OUT, &exec_invalid_slice_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-clear-status", HAL_IN, &exec_clear_status_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-first-step-sync", HAL_IN, &first_step_sync_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-enc-loopback-debug", HAL_IN, &enc_loopback_debug_ptr);
    if (rv < 0) return rv;

    rv = alloc_bit_pin("zynq_stepgen_hw.exec-delta-clamp-any", HAL_OUT, &exec_delta_clamp_any_ptr);
    if (rv < 0) return rv;

    return 0;
}

static void update_legacy(void *arg, long period)
{
    int i;
    double dt;
    int sync_needed;

    (void)arg;
    dt = period * 1e-9;
    if (!regs)
        return;

    zynq_write_reg(REG_WATCHDOG, 1);
    if (watchdog_ok_ptr && *watchdog_ok_ptr)
        **watchdog_ok_ptr = ((regs[REG_WATCHDOG / 4] & 0x1u) == 0u) ? 1 : 0;

    if (exec_busy_ptr && *exec_busy_ptr)            **exec_busy_ptr = 0;
    if (exec_done_ptr && *exec_done_ptr)            **exec_done_ptr = 0;
    if (exec_fault_ptr && *exec_fault_ptr)          **exec_fault_ptr = 0;
    if (exec_overrun_ptr && *exec_overrun_ptr)      **exec_overrun_ptr = 0;
    if (exec_apply_busy_ptr && *exec_apply_busy_ptr)**exec_apply_busy_ptr = 0;
    if (exec_invalid_slice_ptr && *exec_invalid_slice_ptr)
        **exec_invalid_slice_ptr = 0;
    if (exec_delta_clamp_any_ptr && *exec_delta_clamp_any_ptr)
        **exec_delta_clamp_any_ptr = 0;

    sync_needed = 0;

    for (i = 0; i < MAX_AXES; i++) {
        axis_data_t *ad;
        double scale;
        double pos_cmd;
        double velocity;
        double phys_limit;
        uint32_t width_ticks;
        uint32_t space_ticks;
        uint32_t dsetup_ticks;
        uint32_t dhold_ticks;
        uint32_t axis_status;
        uint32_t control;
        uint32_t freq_word;
        uint32_t desired_dir;
        int axis_changed;
        int axis_clamped;
        int loopback_feedback;
        int64_t cmd_steps;
        int64_t cmd_delta;
        int64_t send_steps;

        ad = &axis_data[i];
        if (i >= (int)hw_axes) {
            if (ad->enc_count) *ad->enc_count = 0;
            if (ad->index_seen) *ad->index_seen = 0;
            if (ad->pos_fb) *ad->pos_fb = 0.0;
            if (ad->delta_clamped) *ad->delta_clamped = 0;
            continue;
        }

        if (!ad->pos_cmd || !ad->pos_fb || !ad->scale || !ad->enable ||
            !ad->index_enable || !ad->index_seen || !ad->enc_count) {
            zynq_write_reg(AXIS_BASE(i) + REG_CONTROL, 0);
            zynq_write_reg(AXIS_BASE(i) + REG_DELTA_STEPS, 0);
            if (ad->delta_clamped) *ad->delta_clamped = 0;
            continue;
        }

        scale = *ad->scale;
        if (scale == 0.0)
            scale = 1.0;
        pos_cmd = *ad->pos_cmd;

        cmd_steps = units_to_steps(pos_cmd, scale);
        cmd_delta = cmd_steps - ad->last_cmd_steps;
        if (!maybe_apply_wrapped_rotary_origin_reset(ad, cmd_delta, cmd_steps, scale)) {
            ad->last_cmd_steps = cmd_steps;
            ad->pending_steps += cmd_delta;
        }

        width_ticks = ns_to_ticks(ad->step_width_ns);
        space_ticks = ns_to_ticks(ad->step_space_ns);
        dsetup_ticks = ns_to_ticks(ad->dir_setup_ns);
        dhold_ticks = ns_to_ticks(ad->dir_hold_ns);

        if (width_ticks != ad->last_step_width_ticks) {
            zynq_write_reg(AXIS_BASE(i) + REG_STEP_WIDTH, width_ticks);
            ad->last_step_width_ticks = width_ticks;
            sync_needed = 1;
        }
        if (space_ticks != ad->last_step_space_ticks) {
            zynq_write_reg(AXIS_BASE(i) + REG_STEP_SPACE, space_ticks);
            ad->last_step_space_ticks = space_ticks;
            sync_needed = 1;
        }
        if (dsetup_ticks != ad->last_dir_setup_ticks) {
            zynq_write_reg(AXIS_BASE(i) + REG_DIR_SETUP, dsetup_ticks);
            ad->last_dir_setup_ticks = dsetup_ticks;
            sync_needed = 1;
        }
        if (dhold_ticks != ad->last_dir_hold_ticks) {
            zynq_write_reg(AXIS_BASE(i) + REG_DIR_HOLD, dhold_ticks);
            ad->last_dir_hold_ticks = dhold_ticks;
            sync_needed = 1;
        }

        *ad->enc_count = (int32_t)regs[(AXIS_BASE(i) + REG_ENC_COUNT) / 4];
        axis_status = regs[(AXIS_BASE(i) + REG_STATUS) / 4];
        *ad->index_seen = (axis_status >> 1) & 0x1;
        loopback_feedback = (
            enc_loopback_debug_ptr &&
            *enc_loopback_debug_ptr &&
            **enc_loopback_debug_ptr
        ) ? 1 : 0;
        /*
         * Legacy PL v2 has no readable step-output counter. The production board
         * also has no wired encoders, so default pos-fb must represent the exact
         * step count this driver sends to the FPGA. Debug encoder loopback remains
         * available as an explicit opt-in diagnostic path.
         */
        if (loopback_feedback)
            ad->fb_steps = (int64_t)(*ad->enc_count);
        *ad->pos_fb = (double)ad->fb_steps / scale;

        if (*ad->enable == 0) {
            control = 0;
            freq_word = 0;
            axis_changed = (!ad->legacy_shadow_valid) ||
                           (ad->legacy_last_control != control) ||
                           (ad->legacy_last_freq_word != freq_word);
            if (axis_changed) {
                zynq_write_reg(AXIS_BASE(i) + REG_CONTROL, control);
                zynq_write_reg(AXIS_BASE(i) + REG_DELTA_STEPS, freq_word);
                ad->legacy_last_control = control;
                ad->legacy_last_freq_word = freq_word;
                ad->legacy_shadow_valid = 1;
                sync_needed = 1;
            }
            ad->old_pos = pos_cmd;
            ad->pending_steps = 0;
            ad->fb_steps = cmd_steps;
            ad->last_target_dir = 0;
            ad->legacy_guard_target_dir = 0;
            ad->legacy_dir_guard_state = 0;
            *ad->pos_fb = (double)ad->fb_steps / scale;
            if (ad->delta_clamped) *ad->delta_clamped = 0;
            continue;
        }

        send_steps = ad->pending_steps;
        axis_clamped = 0;
        desired_dir = ad->last_target_dir;
        phys_limit = 0.0;

        if (send_steps != 0) {
            desired_dir = ((send_steps < 0) ^ (ad->dir_invert != 0)) ? 1u : 0u;
        }

        {
            uint32_t width_now;
            uint32_t space_now;
            int64_t max_steps;

            width_now = ad->last_step_width_ticks ? ad->last_step_width_ticks : 1;
            space_now = ad->last_step_space_ticks ? ad->last_step_space_ticks : 1;
            phys_limit = (double)clk_freq / (double)(width_now + space_now);
            max_steps = (int64_t)(phys_limit * dt);
            if ((max_steps <= 0) && (send_steps != 0))
                max_steps = 1;
            if ((max_steps > 0) && (i64_abs(send_steps) > max_steps)) {
                send_steps = (send_steps < 0) ? -max_steps : max_steps;
                axis_clamped = 1;
            }
        }

        if (send_steps != 0)
            desired_dir = ((send_steps < 0) ^ (ad->dir_invert != 0)) ? 1u : 0u;

        control = CTRL_ENABLE;
        if (ad->legacy_dir_guard_state == 0 &&
            send_steps != 0 &&
            desired_dir != ad->last_target_dir) {
            ad->legacy_guard_target_dir = desired_dir;
            ad->legacy_dir_guard_state = 1;
        }

        if (ad->legacy_dir_guard_state == 1) {
            /* Hold old direction with pulses disabled for one servo slice. */
            send_steps = 0;
            freq_word = 0;
            if (ad->last_target_dir)
                control |= CTRL_DIR;
            ad->legacy_dir_guard_state = 2;
            axis_clamped = 0;
        } else if (ad->legacy_dir_guard_state == 2) {
            /* Switch to new direction, still with pulses disabled for setup. */
            send_steps = 0;
            freq_word = 0;
            ad->last_target_dir = ad->legacy_guard_target_dir;
            if (ad->last_target_dir)
                control |= CTRL_DIR;
            ad->legacy_dir_guard_state = 0;
            axis_clamped = 0;
        } else {
            if (send_steps != 0)
                ad->last_target_dir = desired_dir;
            if (ad->last_target_dir)
                control |= CTRL_DIR;

            /*
             * Legacy v2 hardware consumes a frequency word. Using integer
             * send_steps/dt turns constant feeds such as 1.65 steps/ms into
             * 1/2/1/2 kHz updates, which produces visible pulse-spacing jitter.
             * Prefer LinuxCNC's continuous velocity command and keep send_steps
             * for direction, clamping, and open-loop feedback bookkeeping.
             */
            velocity = 0.0;
            if (ad->vel_cmd)
                velocity = fabs(*ad->vel_cmd * scale);
            if ((velocity <= 0.0) && (dt > 0.0))
                velocity = fabs((pos_cmd - ad->old_pos) * scale / dt);
            if (send_steps == 0)
                velocity = 0.0;
            if ((phys_limit > 0.0) && (velocity > phys_limit)) {
                velocity = phys_limit;
                axis_clamped = 1;
            }
            freq_word = 0;
            if (velocity > 0.0) {
                uint64_t fw64;
                fw64 = ((uint64_t)(velocity * 4294967296.0)) / clk_freq;
                freq_word = (fw64 > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)fw64;
            }
        }

        if (*ad->index_enable)
            control |= CTRL_INDEX_EN;

        axis_changed = (!ad->legacy_shadow_valid) ||
                       (ad->legacy_last_control != control) ||
                       (ad->legacy_last_freq_word != freq_word);
        if (axis_changed) {
            zynq_write_reg(AXIS_BASE(i) + REG_DELTA_STEPS, freq_word);
            zynq_write_reg(AXIS_BASE(i) + REG_CONTROL, control);
            ad->legacy_last_control = control;
            ad->legacy_last_freq_word = freq_word;
            ad->legacy_shadow_valid = 1;
            sync_needed = 1;
        }
        ad->pending_steps -= send_steps;
        if (!loopback_feedback) {
            ad->fb_steps += send_steps;
            *ad->pos_fb = (double)ad->fb_steps / scale;
        }
        ad->old_pos = pos_cmd;
        if (ad->delta_clamped)
            *ad->delta_clamped = axis_clamped ? 1 : 0;
    }

    if (sync_needed) {
        mmio_commit_barrier();
        zynq_write_reg(REG_GLOBAL_SYNC, 1);
        mmio_commit_barrier();
    }
}

static void update(void *arg, long period)
{
    int i;
    uint32_t exec_status;
    uint32_t slice_ticks;
    uint32_t global_cfg;
    int first_step_sync;
    int can_apply;
    int any_clamp;
    int clear_status;
    int32_t delta_send[MAX_AXES];

    if (legacy_mode) {
        update_legacy(arg, period);
        return;
    }

    if (!regs)
        return;

    exec_status = regs[REG_EXEC_STATUS / 4];

    if (exec_busy_ptr && *exec_busy_ptr)            **exec_busy_ptr = (exec_status & EXEC_BUSY) ? 1 : 0;
    if (exec_done_ptr && *exec_done_ptr)            **exec_done_ptr = (exec_status & EXEC_DONE) ? 1 : 0;
    if (exec_fault_ptr && *exec_fault_ptr)          **exec_fault_ptr = (exec_status & EXEC_FAULT) ? 1 : 0;
    if (exec_overrun_ptr && *exec_overrun_ptr)      **exec_overrun_ptr = (exec_status & EXEC_OVERRUN) ? 1 : 0;
    if (exec_apply_busy_ptr && *exec_apply_busy_ptr)**exec_apply_busy_ptr = (exec_status & EXEC_APPLY_BUSY) ? 1 : 0;
    if (exec_invalid_slice_ptr && *exec_invalid_slice_ptr)
        **exec_invalid_slice_ptr = (exec_status & EXEC_INVALID_SLICE) ? 1 : 0;

    if (watchdog_ok_ptr && *watchdog_ok_ptr) {
        int bad;
        bad = ((exec_status & (EXEC_FAULT | EXEC_OVERRUN | EXEC_INVALID_SLICE | EXEC_APPLY_BUSY)) != 0);
        **watchdog_ok_ptr = bad ? 0 : 1;
    }

    clear_status = 0;
    if (exec_clear_status_ptr && *exec_clear_status_ptr && **exec_clear_status_ptr)
        clear_status = 1;

    if (clear_status && !prev_exec_clear_status)
        zynq_write_reg(REG_EXEC_STATUS, 1);

    prev_exec_clear_status = clear_status;

    first_step_sync = 1;
    if (first_step_sync_ptr && *first_step_sync_ptr)
        first_step_sync = (**first_step_sync_ptr) ? 1 : 0;

    global_cfg = 0;
    if (first_step_sync)
        global_cfg |= CFG_FIRST_STEP_SYNC;
    if (enc_loopback_debug_ptr && *enc_loopback_debug_ptr && **enc_loopback_debug_ptr)
        global_cfg |= CFG_ENC_LOOPBACK_DBG;

    slice_ticks = period_ns_to_ticks(period);
    can_apply = ((exec_status & EXEC_BUSY) == 0) && ((exec_status & EXEC_FAULT) == 0);
    any_clamp = 0;

    for (i = 0; i < MAX_AXES; i++) {
        axis_data_t *ad;
        double scale;
        double pos_cmd;
        int64_t cmd_steps;
        int64_t cmd_delta;
        uint32_t axis_status;
        uint32_t width_ticks;
        uint32_t space_ticks;
        uint32_t dsetup_ticks;
        uint32_t dhold_ticks;

        ad = &axis_data[i];
        delta_send[i] = 0;

        if (i >= (int)hw_axes) {
            if (ad->enc_count) *ad->enc_count = 0;
            if (ad->index_seen) *ad->index_seen = 0;
            if (ad->pos_fb) *ad->pos_fb = 0.0;
            if (ad->delta_clamped) *ad->delta_clamped = 0;
            continue;
        }

        if (!ad->pos_cmd || !ad->pos_fb || !ad->scale || !ad->enable ||
            !ad->index_enable || !ad->index_seen || !ad->enc_count || !ad->delta_clamped) {
            if (can_apply) {
                zynq_write_reg(AXIS_BASE(i) + REG_CONTROL, 0);
                zynq_write_reg(AXIS_BASE(i) + REG_DELTA_STEPS, 0);
            }
            continue;
        }

        scale = *ad->scale;
        if (scale == 0.0)
            scale = 1.0;

        pos_cmd = *ad->pos_cmd;
        cmd_steps = units_to_steps(pos_cmd, scale);
        cmd_delta = cmd_steps - ad->last_cmd_steps;
        if (!maybe_apply_wrapped_rotary_origin_reset(ad, cmd_delta, cmd_steps, scale)) {
            ad->last_cmd_steps = cmd_steps;
            ad->pending_steps += cmd_delta;
        }

        *ad->enc_count = (int32_t)regs[(AXIS_BASE(i) + REG_ENC_COUNT) / 4];
        axis_status = regs[(AXIS_BASE(i) + REG_STATUS) / 4];
        *ad->index_seen = (axis_status >> 6) & 0x1;

        if (*ad->enable == 0) {
            ad->pending_steps = 0;
            ad->fb_steps = cmd_steps;
            if (ad->delta_clamped) *ad->delta_clamped = 0;
            if (can_apply) {
                zynq_write_reg(AXIS_BASE(i) + REG_CONTROL, 0);
                zynq_write_reg(AXIS_BASE(i) + REG_DELTA_STEPS, 0);
            }
            ad->last_target_dir = 0;
            *ad->pos_fb = (double)ad->fb_steps / scale;
            continue;
        }

        width_ticks = ns_to_ticks(ad->step_width_ns);
        space_ticks = ns_to_ticks(ad->step_space_ns);
        dsetup_ticks = ns_to_ticks(ad->dir_setup_ns);
        dhold_ticks = ns_to_ticks(ad->dir_hold_ns);

        if (width_ticks != ad->last_step_width_ticks) {
            zynq_write_reg(AXIS_BASE(i) + REG_STEP_WIDTH, width_ticks);
            ad->last_step_width_ticks = width_ticks;
        }
        if (space_ticks != ad->last_step_space_ticks) {
            zynq_write_reg(AXIS_BASE(i) + REG_STEP_SPACE, space_ticks);
            ad->last_step_space_ticks = space_ticks;
        }
        if (dsetup_ticks != ad->last_dir_setup_ticks) {
            zynq_write_reg(AXIS_BASE(i) + REG_DIR_SETUP, dsetup_ticks);
            ad->last_dir_setup_ticks = dsetup_ticks;
        }
        if (dhold_ticks != ad->last_dir_hold_ticks) {
            zynq_write_reg(AXIS_BASE(i) + REG_DIR_HOLD, dhold_ticks);
            ad->last_dir_hold_ticks = dhold_ticks;
        }

        if (can_apply) {
            int64_t pending;
            int64_t send64;
            uint32_t max_steps;
            uint32_t control;
            int axis_clamped;

            pending = ad->pending_steps;
            send64 = pending;
            axis_clamped = 0;

            max_steps = axis_max_steps_per_slice(ad, slice_ticks, first_step_sync);
            if ((max_steps > 0) && (i64_abs(send64) > (int64_t)max_steps)) {
                send64 = (send64 < 0) ? -(int64_t)max_steps : (int64_t)max_steps;
                axis_clamped = 1;
            }

            if (send64 > 2147483647LL) {
                send64 = 2147483647LL;
                axis_clamped = 1;
            } else if (send64 < (-2147483647LL - 1LL)) {
                send64 = (-2147483647LL - 1LL);
                axis_clamped = 1;
            }

            delta_send[i] = clamp_i32(send64);
            control = CTRL_ENABLE;
            if (delta_send[i] != 0) {
                ad->last_target_dir = ((delta_send[i] < 0) ^ (ad->dir_invert != 0)) ? 1u : 0u;
            }
            /*
             * Slice-v3 uses CONTROL[1] as direction invert, so a zero-delta
             * slice would otherwise decode to the default direction and flip
             * DIR between sparse low-speed steps. For zero-delta slices, drive
             * CONTROL[1] to the already-emitted physical direction so FPGA
             * keeps eff_dir stable while no pulse is generated.
             */
            if (delta_send[i] == 0) {
                if (ad->last_target_dir)
                    control |= CTRL_DIR_INVERT;
            } else if (ad->dir_invert) {
                control |= CTRL_DIR_INVERT;
            }
            if (*ad->index_enable)
                control |= CTRL_INDEX_EN;

            zynq_write_reg(AXIS_BASE(i) + REG_CONTROL, control);
            zynq_write_reg(AXIS_BASE(i) + REG_DELTA_STEPS, (uint32_t)delta_send[i]);

            if (ad->delta_clamped)
                *ad->delta_clamped = axis_clamped ? 1 : 0;
            if (axis_clamped)
                any_clamp = 1;
        } else {
            if (ad->delta_clamped)
                *ad->delta_clamped = 0;
        }

        *ad->pos_fb = (double)ad->fb_steps / scale;
    }

    if (exec_delta_clamp_any_ptr && *exec_delta_clamp_any_ptr)
        **exec_delta_clamp_any_ptr = any_clamp ? 1 : 0;

    /* Always apply when idle to latch latest params; delta can be zero on all axes. */
    if (can_apply) {
        zynq_write_reg(REG_GLOBAL_CFG, global_cfg);
        zynq_write_reg(REG_SLICE_TICKS, slice_ticks);
        zynq_write_reg(REG_SLICE_SEQ_SHADOW, ++slice_seq_shadow);

        mmio_commit_barrier();
        zynq_write_reg(REG_GLOBAL_APPLY, 1);
        mmio_commit_barrier();

        for (i = 0; i < (int)hw_axes; i++) {
            double scale;
            axis_data_t *ad;

            ad = &axis_data[i];
            if (!ad->pos_fb || !ad->scale)
                continue;

            ad->pending_steps -= (int64_t)delta_send[i];
            ad->fb_steps += (int64_t)delta_send[i];

            scale = *ad->scale;
            if (scale == 0.0)
                scale = 1.0;
            *ad->pos_fb = (double)ad->fb_steps / scale;
        }
    }
}

int rtapi_app_main(void)
{
    int i;
    int retval;
    uint32_t uio_addr;
    uint32_t uio_size;
    const char *uio_path;
    char uio_node[32];
    char uio_addr_path[128];
    char uio_size_path[128];
    v5_uio_identity_t uio_identity;

    fd = -1;
    map_base = NULL;
    regs = NULL;
    map_size = STEPGEN_SIZE;

    comp_id = hal_init("zynq_stepgen_hw");
    if (comp_id < 0)
        return comp_id;

    axis_data = hal_malloc(MAX_AXES * sizeof(axis_data_t));
    if (!axis_data) {
        hal_exit(comp_id);
        return -ENOMEM;
    }
    memset(axis_data, 0, MAX_AXES * sizeof(axis_data_t));

    for (i = 0; i < MAX_AXES; i++) {
        axis_data_t *ad;
        ad = &axis_data[i];

        retval = export_axis_pins(i, ad);
        if (retval < 0)
            goto error;

        ad->step_width_ns = 5000;
        ad->step_space_ns = 5000;
        ad->dir_setup_ns = 10000;
        ad->dir_hold_ns = 10000;
        ad->dir_invert = 0;
        ad->wrapped_rotary = 0;
        ad->last_step_width_ticks = 0;
        ad->last_step_space_ticks = 0;
        ad->last_dir_setup_ticks = 0;
        ad->last_dir_hold_ticks = 0;
        ad->last_target_dir = 0;
        ad->last_cmd_steps = 0;
        ad->pending_steps = 0;
        ad->fb_steps = 0;
        ad->legacy_last_control = 0;
        ad->legacy_last_freq_word = 0;
        ad->legacy_guard_target_dir = 0;
        ad->legacy_dir_guard_state = 0;
        ad->legacy_shadow_valid = 0;

        if (ad->scale)
            *ad->scale = 1000.0;
        if (ad->index_seen)
            *ad->index_seen = 0;
        if (ad->enc_count)
            *ad->enc_count = 0;
        if (ad->delta_clamped)
            *ad->delta_clamped = 0;
    }

    retval = export_global_pins();
    if (retval < 0)
        goto error;

    if (first_step_sync_ptr && *first_step_sync_ptr)
        **first_step_sync_ptr = 1;
    if (enc_loopback_debug_ptr && *enc_loopback_debug_ptr)
        **enc_loopback_debug_ptr = 0;
    if (exec_clear_status_ptr && *exec_clear_status_ptr)
        **exec_clear_status_ptr = 0;

    if (watchdog_ok_ptr && *watchdog_ok_ptr)
        **watchdog_ok_ptr = 1;
    if (exec_busy_ptr && *exec_busy_ptr)
        **exec_busy_ptr = 0;
    if (exec_done_ptr && *exec_done_ptr)
        **exec_done_ptr = 0;
    if (exec_fault_ptr && *exec_fault_ptr)
        **exec_fault_ptr = 0;
    if (exec_overrun_ptr && *exec_overrun_ptr)
        **exec_overrun_ptr = 0;
    if (exec_apply_busy_ptr && *exec_apply_busy_ptr)
        **exec_apply_busy_ptr = 0;
    if (exec_invalid_slice_ptr && *exec_invalid_slice_ptr)
        **exec_invalid_slice_ptr = 0;
    if (exec_delta_clamp_any_ptr && *exec_delta_clamp_any_ptr)
        **exec_delta_clamp_any_ptr = 0;

    uio_path = V5_STEPGEN_UIO_PATH;
    if (validate_stepgen_uio_device(uio_node, sizeof(uio_node), &uio_identity) != 0) {
        retval = -ENODEV;
        rtapi_print_msg(RTAPI_MSG_ERR,
            "zynq_stepgen_hw: invalid UIO device path: %s\n", uio_path);
        goto error;
    }
    snprintf(uio_addr_path, sizeof(uio_addr_path), "/sys/class/uio/%s/maps/map0/addr", uio_node);
    snprintf(uio_size_path, sizeof(uio_size_path), "/sys/class/uio/%s/maps/map0/size", uio_node);
    if (read_hex_file(uio_addr_path, &uio_addr) != 0 ||
        read_hex_file(uio_size_path, &uio_size) != 0 ||
        uio_addr != STEPGEN_BASE || uio_size < STEPGEN_SIZE) {
        retval = -ENODEV;
        rtapi_print_msg(RTAPI_MSG_ERR,
            "zynq_stepgen_hw: required UIO stepgen device unavailable or invalid: %s\n",
            uio_path);
        goto error;
    }

    fd = open(uio_path, O_RDWR | O_SYNC);
    if (fd < 0) {
        retval = (errno != 0) ? -errno : -1;
        rtapi_print_msg(RTAPI_MSG_ERR,
            "zynq_stepgen_hw: open(%s) failed: %s\n", uio_path, strerror(errno));
        goto error;
    }
    if (validate_stepgen_uio_fd(fd, &uio_identity) != 0) {
        retval = -EACCES;
        rtapi_print_msg(RTAPI_MSG_ERR,
            "zynq_stepgen_hw: opened UIO device identity or permissions changed: %s\n",
            uio_path);
        goto error;
    }
    map_size = uio_size;
    map_base = mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map_base == MAP_FAILED) {
        retval = (errno != 0) ? -errno : -1;
        rtapi_print_msg(RTAPI_MSG_ERR,
            "zynq_stepgen_hw: mmap(%s, 0x%X) failed: %s\n",
            uio_path, (unsigned int)map_size, strerror(errno));
        map_base = NULL;
        regs = NULL;
        goto error;
    }

    regs = (volatile uint32_t *)map_base;
    hw_version = regs[REG_VERSION / 4];
    legacy_mode = (hw_version < LEGACY_VERSION_CUTOFF) ? 1 : 0;
    clk_freq = regs[REG_CLK_FREQ / 4] ? regs[REG_CLK_FREQ / 4] : 100000000;
    hw_axes = regs[REG_N_AXES / 4];
    if (hw_axes == 0 || hw_axes > MAX_AXES) {
        rtapi_print_msg(RTAPI_MSG_WARN,
            "zynq_stepgen_hw: invalid N_AXES=%u, clamping to %u\n", hw_axes, MAX_AXES);
        hw_axes = MAX_AXES;
    }

    if (!legacy_mode) {
        /* Clear sticky status from previous runs. */
        zynq_write_reg(REG_EXEC_STATUS, 1);
    }

    for (i = 0; i < (int)hw_axes; i++) {
        axis_data_t *ad;
        ad = &axis_data[i];

        ad->last_step_width_ticks = ns_to_ticks(ad->step_width_ns);
        ad->last_step_space_ticks = ns_to_ticks(ad->step_space_ns);
        ad->last_dir_setup_ticks = ns_to_ticks(ad->dir_setup_ns);
        ad->last_dir_hold_ticks = ns_to_ticks(ad->dir_hold_ns);

        zynq_write_reg(AXIS_BASE(i) + REG_CONTROL, 0);
        zynq_write_reg(AXIS_BASE(i) + REG_DELTA_STEPS, 0);
        zynq_write_reg(AXIS_BASE(i) + REG_STEP_WIDTH, ad->last_step_width_ticks);
        zynq_write_reg(AXIS_BASE(i) + REG_STEP_SPACE, ad->last_step_space_ticks);
        zynq_write_reg(AXIS_BASE(i) + REG_DIR_SETUP, ad->last_dir_setup_ticks);
        zynq_write_reg(AXIS_BASE(i) + REG_DIR_HOLD, ad->last_dir_hold_ticks);
    }

    if (!legacy_mode) {
        zynq_write_reg(REG_GLOBAL_CFG, CFG_FIRST_STEP_SYNC);
        zynq_write_reg(REG_SLICE_TICKS, period_ns_to_ticks(1000000));
        zynq_write_reg(REG_SLICE_SEQ_SHADOW, ++slice_seq_shadow);
        mmio_commit_barrier();
        zynq_write_reg(REG_GLOBAL_APPLY, 1);
        mmio_commit_barrier();
    } else {
        mmio_commit_barrier();
        zynq_write_reg(REG_GLOBAL_SYNC, 1);
        mmio_commit_barrier();
    }

    rtapi_print_msg(RTAPI_MSG_INFO,
        "zynq_stepgen_hw: id=0x%08x ver=0x%08x clk=%u axes=%u mode=%s tag=%s\n",
        regs[REG_ID / 4], regs[REG_VERSION / 4], clk_freq, hw_axes,
        legacy_mode ? "legacy-v2-freqword" : "slice-v3", V5_STEPGEN_BUILD_TAG);

    retval = hal_export_funct("zynq_stepgen_hw.update", update, NULL, 0, 0, comp_id);
    if (retval < 0)
        goto error;

    hal_ready(comp_id);
    return 0;

error:
    if (regs) {
        munmap(map_base, map_size);
        regs = NULL;
        map_base = NULL;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    hal_exit(comp_id);
    return retval;
}

void rtapi_app_exit(void)
{
    int i;

    if (regs) {
        uint32_t exec_status;
        exec_status = regs[REG_EXEC_STATUS / 4];

        if (legacy_mode || ((exec_status & EXEC_BUSY) == 0)) {
            for (i = 0; i < (int)hw_axes; i++) {
                zynq_write_reg(AXIS_BASE(i) + REG_CONTROL, 0);
                zynq_write_reg(AXIS_BASE(i) + REG_DELTA_STEPS, 0);
            }
            if (!legacy_mode) {
                zynq_write_reg(REG_SLICE_TICKS, period_ns_to_ticks(1000000));
                zynq_write_reg(REG_SLICE_SEQ_SHADOW, ++slice_seq_shadow);
                mmio_commit_barrier();
                zynq_write_reg(REG_GLOBAL_APPLY, 1);
                mmio_commit_barrier();
            } else {
                mmio_commit_barrier();
                zynq_write_reg(REG_GLOBAL_SYNC, 1);
                mmio_commit_barrier();
            }
        }

        munmap(map_base, map_size);
        regs = NULL;
        map_base = NULL;
    }

    if (fd >= 0) {
        close(fd);
        fd = -1;
    }

    hal_exit(comp_id);
}
