#ifndef V5_NATIVE_READBACK_H
#define V5_NATIVE_READBACK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_READBACK_WCS_COUNT 9U
#define V5_NATIVE_READBACK_WCS_AXIS_COUNT 5U
#define V5_NATIVE_READBACK_WCS_OFFSET_COUNT V5_NATIVE_READBACK_WCS_AXIS_COUNT
#define V5_NATIVE_READBACK_G53_CENTER_COUNT 3U
#define V5_NATIVE_READBACK_G53_AXIS_COUNT 3U
#define V5_NATIVE_READBACK_G53_CENTER_A 0U
#define V5_NATIVE_READBACK_G53_CENTER_B 1U
#define V5_NATIVE_READBACK_G53_CENTER_C 2U
#define V5_NATIVE_READBACK_MODAL_TEXT_CAP 128U
#define V5_NATIVE_READBACK_MDI_COMMAND_CAP 128U

typedef struct V5NativeReadback {
    int rtcp_actual_available;
    int rtcp_enabled;
    int wcs_actual_available;
    int wcs_index;
    int wcs_offset_available;
    int wcs_table_available;
    unsigned int wcs_offsets_epoch;
    double wcs_offsets[V5_NATIVE_READBACK_WCS_COUNT][V5_NATIVE_READBACK_WCS_AXIS_COUNT];
    int g53_geometry_available;
    unsigned int g53_geometry_epoch;
    double g53_centers[V5_NATIVE_READBACK_G53_CENTER_COUNT][V5_NATIVE_READBACK_G53_AXIS_COUNT];
    int interpreter_state_available;
    int interpreter_paused;
    int interpreter_idle_available;
    int interpreter_idle;
    int current_line_available;
    int current_line;
    int motion_line_available;
    int motion_line;
    int mdi_run_available;
    int mdi_run_active;
    int mdi_run_line;
    char mdi_run_command[V5_NATIVE_READBACK_MDI_COMMAND_CAP];
    int homed_available;
    int all_homed;
    int safety_estop_available;
    int safety_estop_active;
    int machine_enable_available;
    int machine_enabled;
    int modal_actual_available;
    char modal_text[V5_NATIVE_READBACK_MODAL_TEXT_CAP];
    int tool_actual_available;
    int tool_number;
    int tool_length_available;
    double tool_length_mm;
    char unavailable_reason[96];
} V5NativeReadback;

void v5_native_readback_init(V5NativeReadback *readback);
void v5_native_readback_set_unavailable(V5NativeReadback *readback, const char *reason);
void v5_native_readback_set_rtcp_actual(V5NativeReadback *readback, int enabled);
void v5_native_readback_set_wcs_actual(V5NativeReadback *readback, int wcs_index);
void v5_native_readback_set_wcs_actual_offsets(
    V5NativeReadback *readback,
    int wcs_index,
    const double *offsets,
    size_t offset_count);
void v5_native_readback_set_wcs_table(
    V5NativeReadback *readback,
    int wcs_index,
    const double *offsets,
    size_t wcs_count,
    size_t axis_count,
    unsigned int epoch);
const double *v5_native_readback_active_wcs_offsets(const V5NativeReadback *readback);
void v5_native_readback_set_g53_geometry(
    V5NativeReadback *readback,
    const double *centers,
    size_t center_count,
    size_t axis_count,
    unsigned int epoch);
const double *v5_native_readback_g53_center(const V5NativeReadback *readback, unsigned int center_index);
void v5_native_readback_set_interpreter_paused(V5NativeReadback *readback, int paused);
void v5_native_readback_set_interpreter_idle(V5NativeReadback *readback, int idle);
void v5_native_readback_set_current_line(V5NativeReadback *readback, int line);
void v5_native_readback_set_motion_line(V5NativeReadback *readback, int line);
void v5_native_readback_set_mdi_run_actual(
    V5NativeReadback *readback,
    int active,
    int line,
    const char *command);
void v5_native_readback_set_all_homed(V5NativeReadback *readback, int all_homed);
void v5_native_readback_set_safety_estop(V5NativeReadback *readback, int active);
void v5_native_readback_set_machine_enabled(V5NativeReadback *readback, int enabled);
void v5_native_readback_set_modal_actual(V5NativeReadback *readback, const char *modal_text);
void v5_native_readback_set_tool_actual(
    V5NativeReadback *readback,
    int tool_number,
    int tool_length_available,
    double tool_length_mm);
int v5_native_readback_rtcp_known(const V5NativeReadback *readback);
int v5_native_readback_wcs_known(const V5NativeReadback *readback);
int v5_native_readback_wcs_offset_known(const V5NativeReadback *readback);
int v5_native_readback_wcs_table_known(const V5NativeReadback *readback);
int v5_native_readback_g53_geometry_known(const V5NativeReadback *readback);
int v5_native_readback_interpreter_known(const V5NativeReadback *readback);
int v5_native_readback_interpreter_idle_known(const V5NativeReadback *readback);
int v5_native_readback_current_line_known(const V5NativeReadback *readback);
int v5_native_readback_motion_line_known(const V5NativeReadback *readback);
int v5_native_readback_mdi_run_known(const V5NativeReadback *readback);
int v5_native_readback_all_homed_known(const V5NativeReadback *readback);
int v5_native_readback_safety_estop_known(const V5NativeReadback *readback);
int v5_native_readback_machine_enable_known(const V5NativeReadback *readback);
int v5_native_readback_modal_known(const V5NativeReadback *readback);
int v5_native_readback_tool_known(const V5NativeReadback *readback);
int v5_native_readback_tool_length_known(const V5NativeReadback *readback);

#ifdef __cplusplus
}
#endif

#endif
