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

int v5_native_home_format_increment(
    const V5NativeMotionAxisParameters *axis,
    double delta,
    char *line,
    size_t line_size);

int v5_native_home_joint_needs_sync(int homed_status_available, int homed);

int v5_native_home_positions_still(
    const double *previous,
    const double *current,
    unsigned int axis_count);

V5LinuxcncrshSendStatus v5_native_home_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    V5NativeHomeResult *result);

#ifdef __cplusplus
}
#endif

#endif
