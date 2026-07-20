#include "v5_native_home.h"

#include "v5_native_hal_owner_client.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define V5_ALL_HOME_WAIT_ATTEMPTS 2400U
#define V5_ALL_HOME_WAIT_US 50000U

static void result_init(V5NativeHomeResult *result)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    snprintf(result->mode, sizeof(result->mode), "%s", "ALL_HOME");
    snprintf(result->code, sizeof(result->code), "%s", "ALL_HOME_NOT_SUBMITTED");
}

static void result_code(V5NativeHomeResult *result, const char *code)
{
    if (!result) return;
    snprintf(result->code, sizeof(result->code), "%s", code ? code : "ALL_HOME_FAILED");
}

static const char *home_entry_result_code(V5LinuxcncrshHomeEntryStatus status)
{
    switch (status) {
    case V5_LINUXCNCRSH_HOME_ENTRY_CONTEXT_UNAVAILABLE:
    case V5_LINUXCNCRSH_HOME_ENTRY_UNAVAILABLE:
        return "ALL_HOME_INTERPRETER_CONTEXT_UNAVAILABLE";
    case V5_LINUXCNCRSH_HOME_ENTRY_ABORT_NOT_CONFIRMED:
        return "ALL_HOME_ABORT_NOT_CONFIRMED";
    case V5_LINUXCNCRSH_HOME_ENTRY_MANUAL_NOT_CONFIRMED:
        return "ALL_HOME_MANUAL_MODE_NOT_CONFIRMED";
    case V5_LINUXCNCRSH_HOME_ENTRY_JOINT_MODE_NOT_CONFIRMED:
        return "ALL_HOME_JOINT_MODE_NOT_CONFIRMED";
    case V5_LINUXCNCRSH_HOME_ENTRY_PROGRAM_CHANGED:
        return "ALL_HOME_PROGRAM_IDENTITY_CHANGED";
    case V5_LINUXCNCRSH_HOME_ENTRY_READY:
    default:
        return "ALL_HOME_SEND_FAILED";
    }
}

static void publish_progress(
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomePhase phase,
    const char *reason,
    int terminal,
    int cancelled,
    V5NativeHomeProgressCallback callback,
    void *user_data)
{
    V5NativeHomeProgress progress;
    if (!callback) return;
    memset(&progress, 0, sizeof(progress));
    progress.run_id = run_id;
    progress.generation = generation;
    progress.phase = (unsigned int)phase;
    progress.terminal = terminal ? 1 : 0;
    progress.cancelled = cancelled ? 1 : 0;
    snprintf(progress.mode, sizeof(progress.mode), "%s", "all");
    snprintf(progress.current_axes, sizeof(progress.current_axes), "%s", "ALL");
    snprintf(progress.direct_reason, sizeof(progress.direct_reason), "%s", reason ? reason : "");
    callback(&progress, user_data);
}

const char *v5_native_home_failure_code(
    unsigned int failure,
    unsigned int current_joint,
    const unsigned int *axis_code_by_joint)
{
    switch (failure) {
    case 1U:
        if (axis_code_by_joint && current_joint < V5_NATIVE_HOME_JOINT_COUNT) {
            switch (axis_code_by_joint[current_joint]) {
            case (unsigned int)'X': return "ALL_HOME_AXIS_X_CONFIG_INVALID";
            case (unsigned int)'Y': return "ALL_HOME_AXIS_Y_CONFIG_INVALID";
            case (unsigned int)'Z': return "ALL_HOME_AXIS_Z_CONFIG_INVALID";
            case (unsigned int)'A': return "ALL_HOME_AXIS_A_CONFIG_INVALID";
            case (unsigned int)'B': return "ALL_HOME_AXIS_B_CONFIG_INVALID";
            case (unsigned int)'C': return "ALL_HOME_AXIS_C_CONFIG_INVALID";
            default: break;
            }
        }
        return "ALL_HOME_NATIVE_CONFIG_INVALID";
    case 2U: return "ALL_HOME_COUNT_READBACK_INVALID";
    case 3U: return "ALL_HOME_LIMIT_ACTIVE";
    case 4U: return "HOME_CANCELLED";
    case 8U: return "HOME_PRECONDITION_ESTOP";
    case 9U: return "HOME_PRECONDITION_DISABLED";
    case 10U: return "ALL_HOME_DRIVE_FAULT";
    case 11U: return "ALL_HOME_RTCP_FORCE_OFF_NOT_CONFIRMED";
    case 12U: return "HOME_PRECONDITION_MOVING";
    case 13U: return "HOME_PRECONDITION_SAFETY_UNAVAILABLE";
    case 14U: return "ALL_HOME_AXIS_SLAVE_MAPPING_INVALID";
    case 15U: return "HOME_PRECONDITION_RTCP_STATUS_UNAVAILABLE";
    case 16U: return "ALL_HOME_MOTION_NOT_STARTED";
    case 17U: return "ALL_HOME_MOTION_NOT_COMPLETE";
    case 18U: return "ALL_HOME_SEQUENCE_NOT_STARTED";
    case 19U: return "ALL_HOME_DIRECT_SINGLE_JOINT_UNSUPPORTED";
    case 20U: return "ALL_HOME_ZERO_DELTA_MOVEMENT_UNPROVEN";
    case 21U: return "ALL_HOME_PLAN_STALE";
    default: return "ALL_HOME_NATIVE_FAILED";
    }
}

#ifndef _WIN32
static V5NativeHomePhase native_home_phase(unsigned int phase)
{
    switch (phase) {
    case 1U:
    case 2U: return V5_NATIVE_HOME_PHASE_NATIVE_HOME;
    case 3U: return V5_NATIVE_HOME_PHASE_HOMED_SYNC;
    case 5U: return V5_NATIVE_HOME_PHASE_CANCELLED;
    case 6U: return V5_NATIVE_HOME_PHASE_PREPARING;
    case 7U: return V5_NATIVE_HOME_PHASE_RTCP_FORCE_OFF;
    default: return V5_NATIVE_HOME_PHASE_FAILED;
    }
}

static int native_home_axis_code_valid(unsigned int axis)
{
    return axis == (unsigned int)'X' || axis == (unsigned int)'Y' ||
           axis == (unsigned int)'Z' || axis == (unsigned int)'A' ||
           axis == (unsigned int)'B' || axis == (unsigned int)'C';
}

static void native_home_axes_text(
    const V5NativeHalOwnerResponse *status,
    char *text,
    size_t text_cap)
{
    unsigned int joint;
    size_t used = 0U;
    if (!text || !text_cap) return;
    text[0] = '\0';
    if (!status) return;
    for (joint = 0U; joint < V5_NATIVE_HOME_JOINT_COUNT; ++joint) {
        unsigned int axis;
        int written;
        if (!(status->home_current_mask & (1U << joint))) continue;
        axis = status->home_axis_code_by_joint[joint];
        if (!native_home_axis_code_valid(axis)) continue;
        written = snprintf(text + used, text_cap - used, "%s%c", used ? "/" : "", (char)axis);
        if (written < 0 || (size_t)written >= text_cap - used) break;
        used += (size_t)written;
    }
}

static void publish_native_status(
    unsigned long long run_id,
    unsigned int generation,
    const V5NativeHalOwnerResponse *status,
    const char *reason,
    int terminal,
    int cancelled,
    V5NativeHomeProgressCallback callback,
    void *user_data)
{
    V5NativeHomeProgress progress;
    if (!status || !callback) return;
    memset(&progress, 0, sizeof(progress));
    progress.run_id = run_id;
    progress.generation = generation;
    progress.phase = terminal ? (cancelled ? V5_NATIVE_HOME_PHASE_CANCELLED : V5_NATIVE_HOME_PHASE_FAILED)
                              : native_home_phase(status->home_phase);
    progress.failure_phase = terminal ? native_home_phase(status->status_home_failure_phase) : 0U;
    progress.terminal = terminal ? 1 : 0;
    progress.cancelled = cancelled ? 1 : 0;
    progress.current_axis_mask = status->home_current_mask;
    native_home_axes_text(status, progress.current_axes, sizeof(progress.current_axes));
    progress.detail_valid =
        status->home_current_joint < V5_NATIVE_HOME_JOINT_COUNT &&
        status->home_detail_readback_valid;
    if (progress.detail_valid) {
        progress.actual = status->home_actual_counts;
        progress.target = status->home_target_counts;
    }
    snprintf(progress.mode, sizeof(progress.mode), "%s", "all");
    snprintf(progress.direct_reason, sizeof(progress.direct_reason), "%s", reason ? reason : "");
    callback(&progress, user_data);
}
#endif

static int wait_native_all_homed(
    unsigned int baseline_transaction,
    V5NativeHomeResult *result,
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeProgressCallback callback,
    void *user_data)
{
#ifdef _WIN32
    (void)baseline_transaction;
    (void)result;
    (void)run_id;
    (void)generation;
    (void)callback;
    (void)user_data;
    return 0;
#else
    unsigned int attempt;
    unsigned int transaction = 0U;
    for (attempt = 0U; attempt < V5_ALL_HOME_WAIT_ATTEMPTS; ++attempt) {
        V5NativeHalOwnerResponse native_status;
        if (v5_native_hal_owner_exchange(
                V5_NATIVE_HAL_OWNER_OP_HOME_STATUS, 0U, 100U, &native_status) ==
            V5_NATIVE_HAL_OWNER_CLIENT_OK) {
            if (!transaction && native_status.home_transaction != 0U &&
                native_status.home_transaction != baseline_transaction) {
                transaction = native_status.home_transaction;
            }
            if (transaction && native_status.home_transaction != transaction) {
                result_code(result, "ALL_HOME_TRANSACTION_SUPERSEDED");
                publish_native_status(
                    run_id, generation, &native_status, result->code, 1, 0, callback, user_data);
                return -2;
            }
            /* A matching native failure/cancellation is already terminal for
             * this Home transaction.  Do not wait for the aggregate status to
             * become fully consistent: sibling axes may still be publishing
             * their terminal transition while the first direct error is
             * already visible to the operator. */
            if (transaction && native_status.home_transaction == transaction &&
                (native_status.home_failed_mask || native_status.home_cancelled_mask)) {
                const char *failure = v5_native_home_failure_code(
                    native_status.home_failure,
                    native_status.home_current_joint,
                    native_status.home_axis_code_by_joint);
                int cancelled = native_status.home_cancelled_mask != 0U &&
                                native_status.home_phase == 5U &&
                                native_status.home_failure == 4U;
                result_code(result, failure);
                publish_native_status(
                    run_id, generation, &native_status, failure, 1, cancelled, callback, user_data);
                return cancelled ? -1 : -2;
            }
            if (transaction &&
                (!native_status.home_status_consistent ||
                 native_status.home_joint_transaction != transaction)) {
                usleep(V5_ALL_HOME_WAIT_US);
                continue;
            }
            if (transaction && native_status.home_active_mask) {
                publish_native_status(
                    run_id, generation, &native_status, native_status.code, 0, 0, callback, user_data);
            }
            if (transaction && native_status.status_home_terminal_mask != 0U &&
                native_status.home_active_mask == 0U &&
                native_status.home_complete_mask != 0U) {
                return 1;
            }
        }
        usleep(V5_ALL_HOME_WAIT_US);
    }
    return 0;
#endif
}

V5LinuxcncrshSendStatus v5_native_home_send(
    const V5LinuxcncrshConfig *config,
    V5NativeHomeResult *result,
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeProgressCallback progress_cb,
    void *progress_user_data)
{
    char command[32];
    int wait_status;
    V5LinuxcncrshSendStatus status;
    V5LinuxcncrshHomeEntryStatus home_entry_status;
    V5NativeHalOwnerResponse native_status;
    unsigned int baseline_transaction;

    result_init(result);
    if (!config || !result || !run_id || !generation ||
        !v5_linuxcncrsh_format_all_home(command, sizeof(command))) {
        result_code(result, "ALL_HOME_REQUEST_INVALID");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (v5_native_hal_owner_exchange(
            V5_NATIVE_HAL_OWNER_OP_HOME_STATUS, 0U, 100U, &native_status) !=
        V5_NATIVE_HAL_OWNER_CLIENT_OK) {
        result_code(result, "ALL_HOME_NATIVE_STATUS_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    baseline_transaction = native_status.home_transaction;

    publish_progress(
        run_id, generation, V5_NATIVE_HOME_PHASE_PREPARING,
        "ALL_HOME_SUBMITTING", 0, 0, progress_cb, progress_user_data);
    home_entry_status = v5_linuxcncrsh_prepare_home_entry(config);
    if (home_entry_status != V5_LINUXCNCRSH_HOME_ENTRY_READY) {
        result_code(result, home_entry_result_code(home_entry_status));
        return home_entry_status == V5_LINUXCNCRSH_HOME_ENTRY_PROGRAM_CHANGED
            ? V5_LINUXCNCRSH_SEND_INVALID
            : V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    status = v5_linuxcncrsh_send_line(config, command);
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "ALL_HOME_SEND_FAILED");
        return status;
    }

    publish_progress(
        run_id, generation, V5_NATIVE_HOME_PHASE_NATIVE_HOME,
        "ALL_HOME_ACCEPTED", 0, 0, progress_cb, progress_user_data);
    wait_status = wait_native_all_homed(
        baseline_transaction, result,
        run_id, generation, progress_cb, progress_user_data);
    if (wait_status < 0) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (wait_status == 0) {
        result_code(result, "ALL_HOME_NATIVE_RESULT_TIMEOUT");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }

    result->homed_confirmed = 1;
    result_code(result, "ALL_HOME_CONFIRMED");
    publish_progress(
        run_id, generation, V5_NATIVE_HOME_PHASE_HOMED_SYNC,
        result->code, 0, 0, progress_cb, progress_user_data);
    return V5_LINUXCNCRSH_SEND_SENT;
}
