#ifndef V5_NATIVE_WORK_ZERO_H
#define V5_NATIVE_WORK_ZERO_H

#include "v5_linuxcncrsh_client.h"
#include "v5_native_motion_parameters.h"
#include "v5_native_readback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeWorkZeroResult {
    int persistent_owner_submitted;
    int native_readback_confirmed;
    int machine_position_unchanged;
    int mode_restored;
    unsigned int before_generation;
    unsigned int after_generation;
    char code[64];
} V5NativeWorkZeroResult;

int v5_native_work_zero_preflight(
    const V5NativeReadback *modal_tool,
    const V5NativeReadback *rtcp,
    char *code,
    size_t code_cap);
int v5_native_work_zero_coordinate_frame_clear(
    double machine_position,
    double relative_position,
    double active_wcs_offset);

V5LinuxcncrshSendStatus v5_native_work_zero_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5NativeWorkZeroResult *result);

#ifdef __cplusplus
}
#endif

#endif
