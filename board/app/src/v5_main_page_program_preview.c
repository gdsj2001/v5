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

void v5_main_page_internal_make_divider(lv_obj_t *parent, int x, int y, int w, int h)
{
    v5_main_page_internal_make_panel(parent, x, y, w, h, 33, 72, 98);
}

lv_obj_t *v5_main_page_internal_make_label_ex(lv_obj_t *parent, int x, int y, int w, int h, const char *text, uint8_t r, uint8_t g, uint8_t b, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text ? text : "--");
    lv_obj_set_style_text_color(label, v5_main_page_internal_rgb(r, g, b), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}


static void set_program_preview_row(V5MainPage *page, unsigned int row, const char *text, int highlighted, int has_text)
{
    if (!page || row >= V5_PROGRAM_PREVIEW_ROWS) {
        return;
    }
    if (page->program_line_bg[row]) {
        v5_main_page_internal_set_obj_bg_color_if_changed(
            page->program_line_bg[row],
            highlighted ? v5_main_page_internal_rgb(43, 133, 83) : v5_main_page_internal_rgb(7, 31, 48),
            0);
    }
    if (page->program_line_labels[row]) {
        v5_main_page_internal_set_label_text_if_changed(page->program_line_labels[row], text ? text : "");
        if (highlighted || has_text) {
            v5_main_page_internal_set_obj_text_color_if_changed(
                page->program_line_labels[row],
                highlighted ? v5_main_page_internal_rgb(226, 238, 246) : v5_main_page_internal_rgb(156, 178, 202),
                0);
        } else {
            v5_main_page_internal_set_obj_text_color_if_changed(page->program_line_labels[row], v5_main_page_internal_rgb(95, 116, 138), 0);
        }
    }
}

static int trim_line(char *out, size_t out_size, const char *start, size_t len)
{
    if (!out || out_size == 0U || !start) {
        return 0;
    }
    while (len > 0U && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
        ++start;
        --len;
    }
    while (len > 0U &&
           (start[len - 1U] == ' ' || start[len - 1U] == '\t' || start[len - 1U] == '\r' || start[len - 1U] == '\n')) {
        --len;
    }
    if (len >= out_size) {
        len = out_size - 1U;
    }
    if (len > 0U) {
        memcpy(out, start, len);
    }
    out[len] = '\0';
    return len > 0U;
}

unsigned int v5_main_page_internal_count_preview_source_lines(const char *text)
{
    const char *cursor;
    const char *segment;
    unsigned int count = 0U;
    if (!text || !text[0]) {
        return 0U;
    }
    cursor = text;
    segment = text;
    while (*cursor) {
        if (*cursor == '\n' || *cursor == '\r') {
            ++count;
            if (*cursor == '\r' && cursor[1] == '\n') {
                ++cursor;
            }
            segment = cursor + 1;
        }
        ++cursor;
    }
    if (segment && *segment) {
        ++count;
    }
    return count;
}

static int readback_execution_line_active(const V5NativeReadback *readback)
{
    if (!readback) {
        return 0;
    }
    if (v5_native_readback_safety_estop_known(readback) && readback->safety_estop_active) {
        return 0;
    }
    if (v5_native_readback_machine_enable_known(readback) && !readback->machine_enabled) {
        return 0;
    }
    if (v5_native_readback_interpreter_idle_known(readback) &&
        readback->interpreter_idle && !readback->interpreter_paused) {
        return 0;
    }
    return 1;
}

unsigned int v5_main_page_internal_program_preview_highlight_epoch(const V5ProgramRuntime *runtime);

int v5_main_page_internal_active_preview_line_from_readback(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    const char **native_command_out)
{
    const V5NativeReadback *readback;
    unsigned int runtime_epoch;
    int line_allowed;
    if (native_command_out) {
        *native_command_out = "";
    }
    if (!page || !runtime) {
        return 0;
    }
    readback = &page->native_readback;
    runtime_epoch = v5_main_page_internal_program_preview_highlight_epoch(runtime);
    if (v5_program_runtime_has_mdi(runtime) &&
        v5_native_readback_mdi_run_known(readback) &&
        readback->mdi_run_active && readback->mdi_run_line > 0) {
        if (native_command_out) {
            *native_command_out = readback->mdi_run_command;
        }
        return readback->mdi_run_line;
    }
    if (v5_native_readback_interpreter_idle_known(readback) &&
        readback->interpreter_idle &&
        (!v5_native_readback_interpreter_known(readback) || !readback->interpreter_paused)) {
        return 0;
    }
    line_allowed = readback_execution_line_active(readback) ||
        (runtime_epoch != 0U && page->program_preview_started_loaded_epoch == runtime_epoch) ||
        (runtime_epoch != 0U &&
            page->program_preview_highlight_loaded_epoch == runtime_epoch &&
            page->program_preview_highlight_line > 0);
    if (!line_allowed) {
        return 0;
    }
    if (v5_native_readback_interpreter_known(readback) && readback->interpreter_paused) {
        if (v5_native_readback_current_line_known(readback) && readback->current_line > 0) {
            return readback->current_line;
        }
        if (v5_native_readback_motion_line_known(readback) && readback->motion_line > 0) {
            return readback->motion_line;
        }
        return 0;
    }
    if (v5_native_readback_motion_line_known(readback) && readback->motion_line > 0) {
        return readback->motion_line;
    }
    if (v5_native_readback_current_line_known(readback) && readback->current_line > 0) {
        return readback->current_line;
    }
    return 0;
}

unsigned int v5_main_page_internal_program_preview_highlight_epoch(const V5ProgramRuntime *runtime)
{
    if (!runtime) {
        return 0U;
    }
    if (v5_program_runtime_has_open_program(runtime)) {
        return v5_program_runtime_loaded_epoch(runtime);
    }
    return 0U;
}

void v5_main_page_internal_clear_program_preview_highlight(V5MainPage *page)
{
    if (!page) {
        return;
    }
    page->program_preview_highlight_line = 0;
    page->program_preview_highlight_loaded_epoch = 0U;
}

void v5_main_page_internal_sync_program_preview_after_execution(
    V5MainPage *page,
    V5CommandKind request_kind)
{
    const V5ProgramRuntime *runtime;
    unsigned int epoch;
    if (!page || request_kind != V5_COMMAND_START || !page->program_controller) {
        return;
    }
    runtime = v5_program_controller_runtime(page->program_controller);
    epoch = v5_main_page_internal_program_preview_highlight_epoch(runtime);
    if (epoch == 0U) {
        return;
    }
    page->program_preview_scroll_start_line = 1U;
    page->program_preview_highlight_line = 1;
    page->program_preview_highlight_loaded_epoch = epoch;
    page->program_preview_started_loaded_epoch = epoch;
    page->program_preview_touch_active = 0;
    page->program_preview_touch_accum_y = 0;
    page->program_preview_dragged = 0;
    v5_main_page_internal_refresh_program_preview_rows(page, runtime);
}

int v5_main_page_internal_remembered_program_preview_highlight_line(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    unsigned int total)
{
    int line;
    if (!page || !runtime || page->program_preview_highlight_line <= 0) {
        return 0;
    }
    if (page->program_preview_highlight_loaded_epoch != v5_main_page_internal_program_preview_highlight_epoch(runtime)) {
        return 0;
    }
    line = page->program_preview_highlight_line;
    if (total > 0U && (unsigned int)line > total) {
        return 0;
    }
    return line;
}

static int display_preview_line_from_readback(
    V5MainPage *page,
    const V5ProgramRuntime *runtime,
    unsigned int total,
    const char **native_command_out)
{
    int active;
    const char *native_command = "";
    if (native_command_out) {
        *native_command_out = "";
    }
    active = v5_main_page_internal_active_preview_line_from_readback(page, runtime, &native_command);
    if (active > 0 && (total == 0U || (unsigned int)active <= total || (native_command && native_command[0]))) {
        if (page) {
            page->program_preview_highlight_line = active;
            page->program_preview_highlight_loaded_epoch = v5_main_page_internal_program_preview_highlight_epoch(runtime);
            page->program_preview_started_loaded_epoch = page->program_preview_highlight_loaded_epoch;
        }
        if (native_command_out) {
            *native_command_out = native_command;
        }
        return active;
    }
    return v5_main_page_internal_remembered_program_preview_highlight_line(page, runtime, total);
}

unsigned int v5_main_page_internal_clamp_preview_start_line(unsigned int total, unsigned int start)
{
    if (total <= V5_PROGRAM_PREVIEW_ROWS) {
        return 1U;
    }
    if (start < 1U) {
        start = 1U;
    }
    {
        unsigned int max_start = total - (V5_PROGRAM_PREVIEW_ROWS - 1U);
        if (start > max_start) {
            start = max_start;
        }
    }
    return start;
}

static unsigned int preview_start_line_for_active(unsigned int total, int active)
{
    unsigned int start = 1U;
    if (active > 0) {
        start = (unsigned int)active;
    }
    return v5_main_page_internal_clamp_preview_start_line(total, start);
}

static void refresh_program_preview_from_text(
    V5MainPage *page,
    const char *text,
    const char *native_command,
    unsigned int total,
    int active)
{
    unsigned int row;
    unsigned int start_line;
    int shown[V5_PROGRAM_PREVIEW_ROWS] = {0, 0, 0, 0};
    const char *segment;
    const char *cursor;
    unsigned int source_line = 0U;

    if (active < 0 || (total > 0U && (unsigned int)active > total && (!native_command || !native_command[0]))) {
        active = 0;
    }
    if (!text || !text[0]) {
        page->program_preview_scroll_start_line = 1U;
        for (row = 0U; row < V5_PROGRAM_PREVIEW_ROWS; ++row) {
            set_program_preview_row(page, row, "", 0, 0);
        }
        return;
    }
    if (active > 0) {
        start_line = preview_start_line_for_active(total, active);
        page->program_preview_scroll_start_line = start_line;
    } else {
        start_line = v5_main_page_internal_clamp_preview_start_line(
            total,
            page->program_preview_scroll_start_line ? page->program_preview_scroll_start_line : 1U);
        page->program_preview_scroll_start_line = start_line;
        active = (int)start_line;
    }
    segment = text;
    cursor = text;
    while (*cursor) {
        if (*cursor == '\n' || *cursor == '\r') {
            char code[120];
            char line[192];
            ++source_line;
            (void)trim_line(code, sizeof(code), segment, (size_t)(cursor - segment));
            if (source_line >= start_line && source_line < start_line + V5_PROGRAM_PREVIEW_ROWS) {
                row = source_line - start_line;
                snprintf(line, sizeof(line), "%03u %s", source_line, code);
                set_program_preview_row(page, row, line, active == (int)source_line, 1);
                shown[row] = 1;
            }
            if (*cursor == '\r' && cursor[1] == '\n') {
                ++cursor;
            }
            segment = cursor + 1;
        }
        ++cursor;
    }
    if (segment && *segment) {
        char code[120];
        char line[192];
        ++source_line;
        (void)trim_line(code, sizeof(code), segment, strlen(segment));
        if (source_line >= start_line && source_line < start_line + V5_PROGRAM_PREVIEW_ROWS) {
            row = source_line - start_line;
            snprintf(line, sizeof(line), "%03u %s", source_line, code);
            set_program_preview_row(page, row, line, active == (int)source_line, 1);
            shown[row] = 1;
        }
    }
    if (active > 0 && (unsigned int)active >= start_line &&
        (unsigned int)active < start_line + V5_PROGRAM_PREVIEW_ROWS) {
        row = (unsigned int)active - start_line;
        if (!shown[row] && native_command && native_command[0]) {
            char line[192];
            snprintf(line, sizeof(line), "%03d %s", active, native_command);
            set_program_preview_row(page, row, line, 1, 1);
            shown[row] = 1;
        }
    }
    for (row = 0U; row < V5_PROGRAM_PREVIEW_ROWS; ++row) {
        if (!shown[row]) {
            set_program_preview_row(page, row, "", 0, 0);
        }
    }
}

void v5_main_page_internal_refresh_program_preview_rows(V5MainPage *page, const V5ProgramRuntime *runtime)
{
    unsigned int row;
    int active;
    const char *native_command = "";
    if (!page) {
        return;
    }
    if (!runtime) {
        v5_main_page_internal_clear_program_preview_highlight(page);
        for (row = 0U; row < V5_PROGRAM_PREVIEW_ROWS; ++row) {
            set_program_preview_row(page, row, "", 0, 0);
        }
        return;
    }
    if (v5_program_runtime_has_mdi(runtime)) {
        const char *text = v5_program_runtime_mdi_text(runtime);
        unsigned int total = v5_main_page_internal_count_preview_source_lines(text);
        active = display_preview_line_from_readback(page, runtime, total, &native_command);
        refresh_program_preview_from_text(page, text, native_command, total, active);
        return;
    }
    if (v5_program_runtime_has_open_program(runtime) && runtime->gcode_text) {
        unsigned int total = v5_main_page_internal_count_preview_source_lines(runtime->gcode_text);
        active = display_preview_line_from_readback(page, runtime, total, &native_command);
        refresh_program_preview_from_text(page, runtime->gcode_text, native_command, total, active);
        return;
    }
    v5_main_page_internal_clear_program_preview_highlight(page);
    for (row = 0U; row < V5_PROGRAM_PREVIEW_ROWS; ++row) {
        set_program_preview_row(page, row, "", 0, 0);
    }
}
