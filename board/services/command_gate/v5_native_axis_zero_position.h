#ifndef V5_NATIVE_AXIS_ZERO_POSITION_H
#define V5_NATIVE_AXIS_ZERO_POSITION_H

#include "v5_linuxcncrsh_client.h"
#include "v5_native_home.h"
#include "v5_native_motion_parameters.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeAxisZeroPositionResult {
    int movement_confirmed;
    int arrival_confirmed;
    int wcs_offset_unchanged;
    double before_position;
    double after_position;
    char code[64];
} V5NativeAxisZeroPositionResult;

int v5_native_axis_zero_position_format_report(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size);
V5LinuxcncrshSendStatus v5_native_axis_zero_position_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5NativeAxisZeroPositionResult *result,
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeProgressCallback progress_cb,
    void *progress_user_data);

#ifdef __cplusplus
}
#endif

#endif
