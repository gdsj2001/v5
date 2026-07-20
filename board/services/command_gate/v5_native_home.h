#ifndef V5_NATIVE_HOME_H
#define V5_NATIVE_HOME_H

#include "v5_linuxcncrsh_client.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeHomeResult {
    int homed_confirmed;
    char mode[32];
    char code[64];
} V5NativeHomeResult;

typedef enum V5NativeHomePhase {
    V5_NATIVE_HOME_PHASE_IDLE = 0,
    V5_NATIVE_HOME_PHASE_PREPARING = 1,
    V5_NATIVE_HOME_PHASE_RTCP_FORCE_OFF = 2,
    V5_NATIVE_HOME_PHASE_NATIVE_HOME = 3,
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
    char direct_reason[64];
} V5NativeHomeProgress;

typedef void (*V5NativeHomeProgressCallback)(
    const V5NativeHomeProgress *progress,
    void *user_data);

const char *v5_native_home_failure_code(
    unsigned int failure,
    unsigned int current_joint,
    const unsigned int *axis_code_by_joint);

V5LinuxcncrshSendStatus v5_native_home_send(
    const V5LinuxcncrshConfig *config,
    V5NativeHomeResult *result,
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeProgressCallback progress_cb,
    void *progress_user_data);

#ifdef __cplusplus
}
#endif

#endif
