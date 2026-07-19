#ifndef V5_NATIVE_MODAL_TOOL_STATUS_H
#define V5_NATIVE_MODAL_TOOL_STATUS_H

#include "v5_native_readback.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_PATH "/dev/shm/v5_native_modal_tool_status.bin"
#define V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS 1000U
#define V5_NATIVE_MODAL_TOOL_STATUS_TEXT_CAP 128U

int v5_native_modal_tool_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback);
size_t v5_native_modal_tool_status_block_size(void);
int v5_native_modal_tool_status_read_from_memory(
    const void *memory,
    size_t size,
    unsigned int max_age_ms,
    V5NativeReadback *readback);
int v5_native_modal_tool_status_write(
    const char *path,
    int valid,
    const char *modal_text,
    int tool_valid,
    int tool_number,
    int tool_length_valid,
    double tool_length_mm);
int v5_native_modal_tool_status_write_ex(
    const char *path,
    int valid,
    const char *modal_text,
    int tool_valid,
    int tool_number,
    int tool_length_valid,
    double tool_length_mm,
    int interpreter_idle_valid,
    int interpreter_idle,
    int interpreter_paused_valid,
    int interpreter_paused,
    int all_homed_valid,
    int all_homed,
    int current_line_valid,
    int current_line,
    int motion_line_valid,
    int motion_line,
    int mdi_run_valid,
    int mdi_run_active,
    int mdi_run_line,
    const char *mdi_run_command);

#ifdef __cplusplus
}
#endif

#endif
