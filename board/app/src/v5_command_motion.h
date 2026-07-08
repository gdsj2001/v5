#ifndef V5_COMMAND_MOTION_H
#define V5_COMMAND_MOTION_H

#include "v5_command_gate.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_command_pause_prepare(V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_home_prepare(V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_resume_prepare(V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_rotary_equiv_zero_prepare(char axis, V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_jog_increment_prepare(char axis, double step, int positive, V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_jog_continuous_prepare(char axis, double velocity, int positive, V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_jog_stop_prepare(char axis, V5CommandPrepared *prepared, V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
