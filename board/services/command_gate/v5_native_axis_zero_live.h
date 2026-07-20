#ifndef V5_NATIVE_AXIS_ZERO_LIVE_H
#define V5_NATIVE_AXIS_ZERO_LIVE_H

#include "v5_linuxcncrsh_client.h"
#include "v5_native_motion_parameters.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeAxisZeroLiveResult {
    int applied;
    int display_verified;
    unsigned int commit_seq;
    double previous_mcs_position;
    double mcs_position;
    double tolerance_units;
    char code[64];
} V5NativeAxisZeroLiveResult;

void v5_native_axis_zero_live_result_init(V5NativeAxisZeroLiveResult *result);
int v5_native_axis_zero_live_apply(
    const V5LinuxcncrshConfig *linuxcncrsh,
    const char *ini_path,
    const char *settings_project_root,
    const char *settings_runtime_path,
    const char *pulse_contract_path,
    V5NativeMotionParameters *resident_parameters,
    char axis,
    unsigned int expected_slave_position,
    double expected_mcs_position,
    V5NativeAxisZeroLiveResult *result);

#ifdef __cplusplus
}
#endif

#endif
