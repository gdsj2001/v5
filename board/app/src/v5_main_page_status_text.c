#include "v5_main_page.h"

#include "v5_command_gate_ipc.h"
#include "v5_button_visuals.h"
#include "v5_native_wcs_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_layout_icons.h"
#include "v5_lvgl_clock.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"
#include "v5_remote_metrics.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "v5_main_page_internal.h"

double v5_main_page_internal_monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

const char *v5_main_page_internal_main_page_wcs_code(const V5NativeReadback *readback)
{
    static const char *const names[] = {"G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"};
    if (!v5_native_readback_wcs_known(readback) || readback->wcs_index < 0 || readback->wcs_index > 8) {
        return "--";
    }
    return names[readback->wcs_index];
}

static void append_main_page_modal_text(char *out, size_t out_size, const char *text)
{
    size_t used;
    if (!out || out_size == 0U || !text) {
        return;
    }
    used = strlen(out);
    if (used + 1U >= out_size) {
        return;
    }
    snprintf(out + used, out_size - used, "%s", text);
}

static void append_main_page_modal_tokens(char *out, size_t out_size, const char *modal_text)
{
    char token[24];
    size_t token_len = 0U;
    int emitted = 0;
    const char *p = modal_text;
    if (!p || !p[0]) {
        append_main_page_modal_text(out, out_size, "--");
        return;
    }
    while (*p) {
        if (isspace((unsigned char)*p)) {
            if (token_len > 0U) {
                token[token_len] = '\0';
                if (emitted) {
                    append_main_page_modal_text(out, out_size, "\n");
                }
                append_main_page_modal_text(out, out_size, token);
                emitted = 1;
                token_len = 0U;
            }
        } else if (token_len + 1U < sizeof(token)) {
            token[token_len++] = *p;
        }
        ++p;
    }
    if (token_len > 0U) {
        token[token_len] = '\0';
        if (emitted) {
            append_main_page_modal_text(out, out_size, "\n");
        }
        append_main_page_modal_text(out, out_size, token);
        emitted = 1;
    }
    if (!emitted) {
        append_main_page_modal_text(out, out_size, "--");
    }
}

static const char *main_page_rtcp_modal_text(const V5NativeReadback *readback)
{
    if (!v5_native_readback_rtcp_known(readback)) {
        return "RTCP --";
    }
    return readback->rtcp_enabled ? "RTCP ON" : "RTCP OFF";
}

static void format_main_page_modal(char *out, size_t out_size, const V5NativeReadback *readback)
{
    char line[32];
    if (!out || out_size == 0U) {
        return;
    }
    snprintf(out, out_size, "当前模态\n");
    if (v5_native_readback_tool_known(readback)) {
        snprintf(line, sizeof(line), "T%d", readback->tool_number);
        append_main_page_modal_text(out, out_size, line);
        append_main_page_modal_text(out, out_size, "\n");
        if (v5_native_readback_tool_length_known(readback)) {
            snprintf(line, sizeof(line), "L%.3f", readback->tool_length_mm);
        } else {
            snprintf(line, sizeof(line), "L--");
        }
        append_main_page_modal_text(out, out_size, line);
    } else {
        append_main_page_modal_text(out, out_size, "T--\nL--");
    }
    append_main_page_modal_text(out, out_size, "\n");
    append_main_page_modal_text(out, out_size, main_page_rtcp_modal_text(readback));
    append_main_page_modal_text(out, out_size, "\n");
    append_main_page_modal_tokens(out, out_size, v5_native_readback_modal_known(readback) ? readback->modal_text : "--");
}

void v5_main_page_internal_update_main_page_modal_label(V5MainPage *page)
{
    char modal_display_text[256];
    if (!page || !page->modal_label) {
        return;
    }
    format_main_page_modal(modal_display_text, sizeof(modal_display_text), &page->native_readback);
    v5_main_page_internal_set_label_text_if_changed(page->modal_label, modal_display_text);
}

void v5_main_page_internal_format_main_page_wcs_coordinate(char *out, size_t out_size, const V5UiStatusView *status,
                                            const V5NativeReadback *readback, unsigned int axis)
{
    double value;
    const double *active_offsets;

    if (!out || out_size == 0U) {
        return;
    }
    active_offsets = v5_native_readback_active_wcs_offsets(readback);
    if (!status || axis >= V5_MAIN_PAGE_AXIS_COUNT || axis >= V5_STATUS_AXIS_COUNT ||
        axis >= V5_NATIVE_READBACK_WCS_OFFSET_COUNT || (status->valid_mask & V5_STATUS_VALID_MCS) == 0U ||
        !isfinite(status->mcs[axis]) || !active_offsets || !isfinite(active_offsets[axis])) {
        snprintf(out, out_size, "--.---");
        return;
    }

    value = status->mcs[axis] - active_offsets[axis];
    if (axis >= 3U) {
        value = v5_ui_status_view_rotary_phase_deg(value);
    }
    value = (value >= 0.0 ? floor(value * 1000.0 + 1.0e-9) :
            ceil(value * 1000.0 - 1.0e-9)) / 1000.0;
    if (value == 0.0) {
        value = 0.0;
    }
    snprintf(out, out_size, "%+010.3f", value);
}

static int format_toolpath_g53_active_model_text(
    char *out,
    size_t out_size,
    const V5MainPage *page)
{
    const V5MainPageModelScene *scene;

    if (!out || out_size == 0U || !page ||
        !page->toolpath_model_scene_valid ||
        !page->toolpath_model_scene_fresh) {
        return 0;
    }
    scene = &page->toolpath_model_scene;
    snprintf(
        out,
        out_size,
        "G53 %c %.2f,%.2f,%.2f  %c %.2f,%.2f,%.2f",
        scene->primary_axis,
        scene->primary_center[0],
        scene->primary_center[1],
        scene->primary_center[2],
        scene->child_axis,
        scene->child_center[0],
        scene->child_center[1],
        scene->child_center[2]);
    return 1;
}

static int format_toolpath_wcs_offset_text(
    char *out,
    size_t out_size,
    const V5MainPage *page)
{
    const double *offsets;

    if (!out || out_size == 0U || !page ||
        !page->toolpath_model_scene_valid ||
        !page->toolpath_model_scene_fresh) {
        return 0;
    }
    offsets = v5_native_readback_active_wcs_offsets(&page->native_readback);
    if (!offsets ||
        page->toolpath_model_scene.primary_status_slot >= V5_NATIVE_READBACK_WCS_OFFSET_COUNT ||
        page->toolpath_model_scene.child_status_slot >= V5_NATIVE_READBACK_WCS_OFFSET_COUNT ||
        !isfinite(offsets[0]) || !isfinite(offsets[1]) || !isfinite(offsets[2]) ||
        !isfinite(offsets[page->toolpath_model_scene.primary_status_slot]) ||
        !isfinite(offsets[page->toolpath_model_scene.child_status_slot])) {
        return 0;
    }
    snprintf(
        out,
        out_size,
        "%s偏置 X%.2f Y%.2f Z%.2f %c%.2f %c%.2f",
        v5_main_page_internal_main_page_wcs_code(&page->native_readback),
        offsets[0],
        offsets[1],
        offsets[2],
        page->toolpath_model_scene.primary_axis,
        offsets[page->toolpath_model_scene.primary_status_slot],
        page->toolpath_model_scene.child_axis,
        offsets[page->toolpath_model_scene.child_status_slot]);
    return 1;
}

void v5_main_page_internal_update_toolpath_status_text(V5MainPage *page)
{
    char text[128];
    if (!page) {
        return;
    }
    if (page->toolpath_summary_label &&
        format_toolpath_g53_active_model_text(text, sizeof(text), page)) {
        v5_main_page_internal_set_label_text_if_changed(page->toolpath_summary_label, text);
    } else if (page->toolpath_summary_label && !page->toolpath_model_scene_valid) {
        v5_main_page_internal_set_label_text_if_changed(page->toolpath_summary_label, "");
    }
    if (page->toolpath_detail_label &&
        format_toolpath_wcs_offset_text(text, sizeof(text), page)) {
        v5_main_page_internal_set_label_text_if_changed(page->toolpath_detail_label, text);
    } else if (page->toolpath_detail_label && !page->toolpath_model_scene_valid) {
        v5_main_page_internal_set_label_text_if_changed(page->toolpath_detail_label, "");
    }
}

void v5_main_page_internal_update_main_page_wcs_header(V5MainPage *page)
{
    char text[24];
    if (!page || !page->wcs_header_label) {
        return;
    }
    snprintf(text, sizeof(text), "加工 %s", v5_main_page_internal_main_page_wcs_code(&page->native_readback));
    v5_main_page_internal_set_label_text_if_changed(page->wcs_header_label, text);
}
