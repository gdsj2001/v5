#ifndef V5_DRIVE_WRITE_WINDOW_H
#define V5_DRIVE_WRITE_WINDOW_H

#include "v5_command_gate.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_DRIVE_WRITE_RUN_ID_CAP 65U

typedef struct V5DriveWriteSafetyActual {
    int safety_estop_known;
    int safety_estop_active;
    int machine_enable_known;
    int machine_enabled;
} V5DriveWriteSafetyActual;

typedef struct V5DriveWriteWindowOps {
    void *context;
    int (*read_safety)(void *context, V5DriveWriteSafetyActual *actual);
    int (*set_machine_off)(void *context);
    int (*set_machine_on)(void *context);
} V5DriveWriteWindowOps;

typedef struct V5DriveWriteWindowResult {
    int ok;
    int initial_machine_enabled;
    int final_machine_enable_known;
    int final_machine_enabled;
    char code[64];
} V5DriveWriteWindowResult;

void v5_drive_write_window_result_init(V5DriveWriteWindowResult *result);
int v5_drive_write_window_begin(
    const char *run_id,
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteWindowResult *result);
int v5_drive_write_window_finish(
    const char *run_id,
    int allow_restore,
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteWindowResult *result);
int v5_drive_write_window_abort(
    const char *run_id,
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteWindowResult *result);
int v5_drive_write_window_blocks_kind(V5CommandKind kind);
void v5_drive_write_window_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif
