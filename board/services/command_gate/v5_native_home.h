#ifndef V5_NATIVE_HOME_H
#define V5_NATIVE_HOME_H

#include "v5_linuxcncrsh_client.h"
#include "v5_native_motion_parameters.h"

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

V5LinuxcncrshSendStatus v5_native_home_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    V5NativeHomeResult *result);

#ifdef __cplusplus
}
#endif

#endif
