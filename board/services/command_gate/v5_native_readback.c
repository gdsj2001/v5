#include "v5_native_readback.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void v5_native_readback_init(V5NativeReadback *readback)
{
    if (!readback) {
        return;
    }
    memset(readback, 0, sizeof(*readback));
}

void v5_native_readback_set_unavailable(V5NativeReadback *readback, const char *reason)
{
    if (!readback) {
        return;
    }
    readback->rtcp_actual_available = 0;
    readback->wcs_actual_available = 0;
    readback->wcs_offset_available = 0;
    readback->wcs_table_available = 0;
    readback->wcs_offsets_epoch = 0U;
    readback->g53_geometry_available = 0;
    readback->g53_geometry_epoch = 0U;
    memset(readback->g53_centers, 0, sizeof(readback->g53_centers));
    readback->interpreter_state_available = 0;
    readback->interpreter_paused = 0;
    readback->interpreter_idle_available = 0;
    readback->interpreter_idle = 0;
    readback->current_line_available = 0;
    readback->current_line = 0;
    readback->motion_line_available = 0;
    readback->motion_line = 0;
    readback->mdi_run_available = 0;
    readback->mdi_run_active = 0;
    readback->mdi_run_line = 0;
    readback->mdi_run_command[0] = '\0';
    readback->homed_available = 0;
    readback->all_homed = 0;
    readback->safety_estop_available = 0;
    readback->machine_enable_available = 0;
    readback->modal_actual_available = 0;
    readback->modal_text[0] = '\0';
    readback->tool_actual_available = 0;
    readback->tool_number = 0;
    readback->tool_length_available = 0;
    readback->tool_length_mm = 0.0;
    snprintf(
        readback->unavailable_reason,
        sizeof(readback->unavailable_reason),
        "%s",
        reason ? reason : "native_readback_unavailable");
}

void v5_native_readback_set_rtcp_actual(V5NativeReadback *readback, int enabled)
{
    if (!readback) {
        return;
    }
    readback->rtcp_actual_available = 1;
    readback->rtcp_enabled = enabled ? 1 : 0;
}

void v5_native_readback_set_wcs_actual(V5NativeReadback *readback, int wcs_index)
{
    if (!readback) {
        return;
    }
    if (wcs_index < 0 || wcs_index > 8) {
        readback->wcs_actual_available = 0;
        readback->wcs_offset_available = 0;
        readback->wcs_table_available = 0;
        readback->wcs_offsets_epoch = 0U;
        memset(readback->wcs_offsets, 0, sizeof(readback->wcs_offsets));
        return;
    }
    readback->wcs_actual_available = 1;
    readback->wcs_index = wcs_index;
    readback->wcs_offset_available = 0;
    readback->wcs_table_available = 0;
    readback->wcs_offsets_epoch = 0U;
    memset(readback->wcs_offsets, 0, sizeof(readback->wcs_offsets));
}

void v5_native_readback_set_wcs_actual_offsets(
    V5NativeReadback *readback,
    int wcs_index,
    const double *offsets,
    size_t offset_count)
{
    double table[V5_NATIVE_READBACK_WCS_COUNT][V5_NATIVE_READBACK_WCS_AXIS_COUNT];

    memset(table, 0, sizeof(table));
    if (wcs_index >= 0 && wcs_index < (int)V5_NATIVE_READBACK_WCS_COUNT && offsets) {
        size_t i;
        for (i = 0U; i < offset_count && i < V5_NATIVE_READBACK_WCS_AXIS_COUNT; ++i) {
            table[wcs_index][i] = offsets[i];
        }
    }
    v5_native_readback_set_wcs_table(
        readback,
        wcs_index,
        &table[0][0],
        V5_NATIVE_READBACK_WCS_COUNT,
        V5_NATIVE_READBACK_WCS_AXIS_COUNT,
        1U);
}

void v5_native_readback_set_wcs_table(
    V5NativeReadback *readback,
    int wcs_index,
    const double *offsets,
    size_t wcs_count,
    size_t axis_count,
    unsigned int epoch)
{
    size_t w;
    size_t a;

    v5_native_readback_set_wcs_actual(readback, wcs_index);
    if (!readback || !readback->wcs_actual_available || !offsets ||
        wcs_count != V5_NATIVE_READBACK_WCS_COUNT || axis_count != V5_NATIVE_READBACK_WCS_AXIS_COUNT) {
        return;
    }
    for (w = 0U; w < V5_NATIVE_READBACK_WCS_COUNT; ++w) {
        for (a = 0U; a < V5_NATIVE_READBACK_WCS_AXIS_COUNT; ++a) {
            double value = offsets[(w * V5_NATIVE_READBACK_WCS_AXIS_COUNT) + a];
            if (!isfinite(value)) {
                readback->wcs_offset_available = 0;
                readback->wcs_table_available = 0;
                readback->wcs_offsets_epoch = 0U;
                memset(readback->wcs_offsets, 0, sizeof(readback->wcs_offsets));
                return;
            }
        }
    }
    for (w = 0U; w < V5_NATIVE_READBACK_WCS_COUNT; ++w) {
        for (a = 0U; a < V5_NATIVE_READBACK_WCS_AXIS_COUNT; ++a) {
            readback->wcs_offsets[w][a] = offsets[(w * V5_NATIVE_READBACK_WCS_AXIS_COUNT) + a];
        }
    }
    readback->wcs_table_available = 1;
    readback->wcs_offsets_epoch = epoch ? epoch : 1U;
    readback->wcs_offset_available = 1;
}

const double *v5_native_readback_active_wcs_offsets(const V5NativeReadback *readback)
{
    if (!v5_native_readback_wcs_offset_known(readback)) {
        return 0;
    }
    return readback->wcs_offsets[readback->wcs_index];
}

void v5_native_readback_set_g53_geometry(
    V5NativeReadback *readback,
    const double *centers,
    size_t center_count,
    size_t axis_count,
    unsigned int epoch)
{
    size_t c;
    size_t a;
    if (!readback) {
        return;
    }
    readback->g53_geometry_available = 0;
    readback->g53_geometry_epoch = 0U;
    memset(readback->g53_centers, 0, sizeof(readback->g53_centers));
    if (!centers || center_count != V5_NATIVE_READBACK_G53_CENTER_COUNT ||
        axis_count != V5_NATIVE_READBACK_G53_AXIS_COUNT) {
        return;
    }
    for (c = 0U; c < V5_NATIVE_READBACK_G53_CENTER_COUNT; ++c) {
        for (a = 0U; a < V5_NATIVE_READBACK_G53_AXIS_COUNT; ++a) {
            double value = centers[(c * V5_NATIVE_READBACK_G53_AXIS_COUNT) + a];
            if (!isfinite(value)) {
                memset(readback->g53_centers, 0, sizeof(readback->g53_centers));
                return;
            }
            readback->g53_centers[c][a] = value;
        }
    }
    readback->g53_geometry_available = 1;
    readback->g53_geometry_epoch = epoch ? epoch : 1U;
}

const double *v5_native_readback_g53_center(const V5NativeReadback *readback, unsigned int center_index)
{
    if (!v5_native_readback_g53_geometry_known(readback) || center_index >= V5_NATIVE_READBACK_G53_CENTER_COUNT) {
        return 0;
    }
    return readback->g53_centers[center_index];
}

void v5_native_readback_set_interpreter_paused(V5NativeReadback *readback, int paused)
{
    if (!readback) {
        return;
    }
    readback->interpreter_state_available = 1;
    readback->interpreter_paused = paused ? 1 : 0;
}

void v5_native_readback_set_interpreter_idle(V5NativeReadback *readback, int idle)
{
    if (!readback) {
        return;
    }
    readback->interpreter_state_available = 1;
    readback->interpreter_idle_available = 1;
    readback->interpreter_idle = idle ? 1 : 0;
}

void v5_native_readback_set_current_line(V5NativeReadback *readback, int line)
{
    if (!readback) {
        return;
    }
    readback->current_line_available = 1;
    readback->current_line = line > 0 ? line : 0;
}

void v5_native_readback_set_motion_line(V5NativeReadback *readback, int line)
{
    if (!readback) {
        return;
    }
    readback->motion_line_available = 1;
    readback->motion_line = line > 0 ? line : 0;
}

void v5_native_readback_set_mdi_run_actual(
    V5NativeReadback *readback,
    int active,
    int line,
    const char *command)
{
    if (!readback) {
        return;
    }
    readback->mdi_run_available = 1;
    readback->mdi_run_active = active ? 1 : 0;
    readback->mdi_run_line = line > 0 ? line : 0;
    snprintf(readback->mdi_run_command, sizeof(readback->mdi_run_command), "%s", command ? command : "");
}

void v5_native_readback_set_all_homed(V5NativeReadback *readback, int all_homed)
{
    if (!readback) {
        return;
    }
    readback->homed_available = 1;
    readback->all_homed = all_homed ? 1 : 0;
}

void v5_native_readback_set_safety_estop(V5NativeReadback *readback, int active)
{
    if (!readback) {
        return;
    }
    readback->safety_estop_available = 1;
    readback->safety_estop_active = active ? 1 : 0;
}

void v5_native_readback_set_machine_enabled(V5NativeReadback *readback, int enabled)
{
    if (!readback) {
        return;
    }
    readback->machine_enable_available = 1;
    readback->machine_enabled = enabled ? 1 : 0;
}

void v5_native_readback_set_modal_actual(V5NativeReadback *readback, const char *modal_text)
{
    if (!readback) {
        return;
    }
    if (!modal_text || !modal_text[0]) {
        readback->modal_actual_available = 0;
        readback->modal_text[0] = '\0';
        return;
    }
    readback->modal_actual_available = 1;
    snprintf(readback->modal_text, sizeof(readback->modal_text), "%s", modal_text);
}

void v5_native_readback_set_tool_actual(
    V5NativeReadback *readback,
    int tool_number,
    int tool_length_available,
    double tool_length_mm)
{
    if (!readback) {
        return;
    }
    if (tool_number < 0) {
        readback->tool_actual_available = 0;
        readback->tool_number = 0;
        readback->tool_length_available = 0;
        readback->tool_length_mm = 0.0;
        return;
    }
    readback->tool_actual_available = 1;
    readback->tool_number = tool_number;
    readback->tool_length_available = (tool_length_available && isfinite(tool_length_mm)) ? 1 : 0;
    readback->tool_length_mm = readback->tool_length_available ? tool_length_mm : 0.0;
}

int v5_native_readback_rtcp_known(const V5NativeReadback *readback)
{
    return readback && readback->rtcp_actual_available;
}

int v5_native_readback_wcs_known(const V5NativeReadback *readback)
{
    return readback && readback->wcs_actual_available;
}

int v5_native_readback_wcs_offset_known(const V5NativeReadback *readback)
{
    return readback && readback->wcs_actual_available && readback->wcs_offset_available &&
           readback->wcs_table_available && readback->wcs_index >= 0 &&
           readback->wcs_index < (int)V5_NATIVE_READBACK_WCS_COUNT;
}

int v5_native_readback_wcs_table_known(const V5NativeReadback *readback)
{
    return readback && readback->wcs_table_available && readback->wcs_offsets_epoch != 0U;
}

int v5_native_readback_g53_geometry_known(const V5NativeReadback *readback)
{
    return readback && readback->g53_geometry_available && readback->g53_geometry_epoch != 0U;
}

int v5_native_readback_interpreter_known(const V5NativeReadback *readback)
{
    return readback && readback->interpreter_state_available;
}

int v5_native_readback_interpreter_idle_known(const V5NativeReadback *readback)
{
    return readback && readback->interpreter_idle_available;
}

int v5_native_readback_current_line_known(const V5NativeReadback *readback)
{
    return readback && readback->current_line_available;
}

int v5_native_readback_motion_line_known(const V5NativeReadback *readback)
{
    return readback && readback->motion_line_available;
}

int v5_native_readback_mdi_run_known(const V5NativeReadback *readback)
{
    return readback && readback->mdi_run_available;
}

int v5_native_readback_all_homed_known(const V5NativeReadback *readback)
{
    return readback && readback->homed_available;
}

int v5_native_readback_safety_estop_known(const V5NativeReadback *readback)
{
    return readback && readback->safety_estop_available;
}

int v5_native_readback_machine_enable_known(const V5NativeReadback *readback)
{
    return readback && readback->machine_enable_available;
}

int v5_native_readback_modal_known(const V5NativeReadback *readback)
{
    return readback && readback->modal_actual_available && readback->modal_text[0];
}

int v5_native_readback_tool_known(const V5NativeReadback *readback)
{
    return readback && readback->tool_actual_available;
}

int v5_native_readback_tool_length_known(const V5NativeReadback *readback)
{
    return readback && readback->tool_actual_available && readback->tool_length_available;
}
