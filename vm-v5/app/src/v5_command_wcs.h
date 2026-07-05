#ifndef V5_COMMAND_WCS_H
#define V5_COMMAND_WCS_H

#include "v5_command_gate.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_command_wcs_select_prepare(int wcs_index, V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_work_zero_prepare(int wcs_index, char axis, V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_g92_clear_prepare(V5CommandPrepared *prepared, V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
