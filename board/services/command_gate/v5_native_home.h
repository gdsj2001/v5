#ifndef V5_NATIVE_HOME_H
#define V5_NATIVE_HOME_H

#include "v5_linuxcncrsh_client.h"
#include "v5_native_motion_parameters.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeHomeResult {
    int movement_confirmed;
    int arrival_confirmed;
    int homed_confirmed;
    char mode[32];
    char code[64];
} V5NativeHomeResult;

typedef enum V5NativeHomePhase {
    V5_NATIVE_HOME_PHASE_IDLE = 0,
    V5_NATIVE_HOME_PHASE_PREPARING = 1,
    V5_NATIVE_HOME_PHASE_RTCP_FORCE_OFF = 2,
    V5_NATIVE_HOME_PHASE_PROOF_MOVE = 3,
    V5_NATIVE_HOME_PHASE_ZERO_RETURN = 4,
    V5_NATIVE_HOME_PHASE_HOMED_SYNC = 5,
    V5_NATIVE_HOME_PHASE_COMPLETE = 6,
    V5_NATIVE_HOME_PHASE_CANCELLED = 7,
    V5_NATIVE_HOME_PHASE_FAILED = 8
} V5NativeHomePhase;

typedef struct V5NativeHomeProgress {
    unsigned long long run_id;
    unsigned int generation;
    unsigned int phase;
    unsigned int failure_phase;
    int terminal;
    int cancelled;
    int detail_valid;
    unsigned int current_axis_mask;
    char mode[8];
    char current_axes[16];
    double actual;
    double target;
    double tolerance;
    char direct_reason[64];
} V5NativeHomeProgress;

typedef void (*V5NativeHomeProgressCallback)(
    const V5NativeHomeProgress *progress,
    void *user_data);

int v5_native_home_format_increment(
    const V5NativeMotionAxisParameters *axis,
    double delta,
    char *line,
    size_t line_size);

double v5_native_home_target_delta(char axis, double current, double target);

int v5_native_home_joint_needs_sync(int homed_status_available, int homed);

int v5_native_home_positions_still(
    const double *previous,
    const double *current,
    unsigned int axis_count);

const char *v5_native_home_safety_reject_code(
    int estop_known,
    int estop_active,
    int machine_known,
    int machine_enabled);

V5LinuxcncrshSendStatus v5_native_home_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    V5NativeHomeResult *result,
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeProgressCallback progress_cb,
    void *progress_user_data);

#ifdef __cplusplus
}
#endif

#endif
