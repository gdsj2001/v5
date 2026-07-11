#include "v5_app.h"

#include "lvgl.h"
#include "v5_button_visuals.h"
#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_remote_input.h"
#include "v5_lvgl_touch_input.h"
#include "v5_boot_closure.h"
#include "v5_main_page.h"
#include "v5_native_rtcp_status.h"
#include "v5_native_wcs_status.h"
#include "v5_native_g53_geometry_status.h"
#include "v5_native_modal_tool_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_page.h"
#include "v5_settings_axis_table.h"
#include "v5_status_shm.h"
#include "v5_ui_model.h"
#include "v5_v3_local_pages.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "v5_ui_shell_internal.h"

static void shell_update_program_row_visual(unsigned int idx)
{
    V5ProgramRow *row;
    int selected;
    lv_obj_t *layer;
    if (idx >= V5_PROGRAM_ROWS_MAX || !g_v5_shell_program_row_layers[idx]) {
        return;
    }
    layer = g_v5_shell_program_row_layers[idx];
    if (idx >= g_v5_shell_program_row_count || !g_v5_shell_program_rows[idx].exists) {
        lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    row = &g_v5_shell_program_rows[idx];
    selected = ((int)idx == g_v5_shell_program_selected_index);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(layer, shell_rgb(selected ? 43 : 17, selected ? 133 : 40, selected ? 83 : 58), 0);
    if (g_v5_shell_program_row_name_labels[idx]) {
        lv_label_set_text(g_v5_shell_program_row_name_labels[idx], row->name);
    }
    if (g_v5_shell_program_row_size_labels[idx]) {
        lv_label_set_text(g_v5_shell_program_row_size_labels[idx], row->size);
    }
    if (g_v5_shell_program_row_created_labels[idx]) {
        lv_label_set_text(g_v5_shell_program_row_created_labels[idx], row->created);
    }
    if (g_v5_shell_program_row_modified_labels[idx]) {
        lv_label_set_text(g_v5_shell_program_row_modified_labels[idx], row->modified);
    }
}

void shell_update_program_row(void)
{
    unsigned int i;
    char count_text[32];
    int loaded = shell_load_program_rows();
    snprintf(count_text, sizeof(count_text), "共 %u", g_v5_shell_program_row_count);
    if (g_v5_shell_program_count_label) {
        lv_label_set_text(g_v5_shell_program_count_label, count_text);
    }
    if (g_v5_shell_program_empty_label) {
        if (g_v5_shell_program_row_count == 0U) {
            lv_obj_clear_flag(g_v5_shell_program_empty_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(g_v5_shell_program_empty_label, "未找到可打开的G代码文件");
        } else {
            lv_obj_add_flag(g_v5_shell_program_empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (i = 0U; i < V5_PROGRAM_ROWS_MAX; ++i) {
        shell_update_program_row_visual(i);
    }
    if (g_v5_shell_program_source_label) {
        if (!loaded) {
            lv_label_set_text(g_v5_shell_program_source_label, "来源: 本机 目录不可读");
        } else if (g_v5_shell_program_selected_index >= 0 && (unsigned int)g_v5_shell_program_selected_index < g_v5_shell_program_row_count) {
            char text[220];
            snprintf(text, sizeof(text), "已选择: %s  双击打开运行，或点打开修改", g_v5_shell_program_rows[g_v5_shell_program_selected_index].name);
            lv_label_set_text(g_v5_shell_program_source_label, text);
        } else {
            lv_label_set_text(g_v5_shell_program_source_label, "来源: 本机");
        }
    }
}

static void shell_clear_program_selection_confirm(void)
{
    g_v5_shell_program_confirm_selected_index = -1;
    g_v5_shell_program_confirm_selected_path[0] = '\0';
    g_v5_shell_program_last_click_index = -1;
    g_v5_shell_program_last_click_ns = 0ULL;
    g_v5_shell_program_last_click_path[0] = '\0';
}

static int shell_program_row_matches_explicit_selection(int idx)
{
    if (idx < 0 || idx != g_v5_shell_program_selected_index || idx != g_v5_shell_program_confirm_selected_index ||
        (unsigned int)idx >= g_v5_shell_program_row_count || !g_v5_shell_program_rows[idx].exists) {
        return 0;
    }
    return strcmp(g_v5_shell_program_confirm_selected_path, g_v5_shell_program_rows[idx].path) == 0;
}

static void shell_select_program_row(int idx)
{
    g_v5_shell_program_selected_index = idx;
    g_v5_shell_program_confirm_selected_index = idx;
    snprintf(g_v5_shell_program_confirm_selected_path, sizeof(g_v5_shell_program_confirm_selected_path), "%s", g_v5_shell_program_rows[idx].path);
    g_v5_shell_program_last_click_index = idx;
    g_v5_shell_program_last_click_ns = shell_monotonic_ns();
    snprintf(g_v5_shell_program_last_click_path, sizeof(g_v5_shell_program_last_click_path), "%s", g_v5_shell_program_rows[idx].path);
    shell_update_program_row();
    shell_log_program_event("program_file_select", g_v5_shell_program_rows[idx].path, 1, 0);
}

static void shell_open_program_row_for_run(int idx)
{
    V5ProgramOpenResult result;
    memset(&result, 0, sizeof(result));
    shell_clear_program_selection_confirm();
    if (v5_main_page_open_program(&g_v5_shell_main_page, g_v5_shell_program_rows[idx].path, &result)) {
        shell_log_program_event("program_file_double_click", g_v5_shell_program_rows[idx].path, 1, &result);
        (void)v5_main_page_apply_status(&g_v5_shell_main_page, &g_v5_shell_model.status_view);
        shell_log_program_event("program_file_double_click_applied", g_v5_shell_program_rows[idx].path, 1, &result);
        (void)v5_settings_page_apply_status(&g_v5_shell_settings_page, &g_v5_shell_model.status_view);
        g_v5_shell_main_cache_dirty = 1;
        shell_navigate(0, V5_MAIN_PAGE_ACTION_NAV_MAIN);
    } else {
        shell_log_program_event("program_file_double_click", g_v5_shell_program_rows[idx].path, 0, &result);
        if (g_v5_shell_program_source_label) {
            lv_label_set_text(g_v5_shell_program_source_label, "打开失败");
        }
    }
}

static int shell_load_program_row_for_edit(int idx, const char *event_name)
{
    FILE *fp;
    struct stat st;
    char text[V5_MDI_TEXT_CAP];
    size_t n;
    if (idx < 0 || (unsigned int)idx >= g_v5_shell_program_row_count || !g_v5_shell_program_rows[idx].exists) {
        shell_log_mdi_event("program_edit_rejected", "no selected program", 0);
        return 0;
    }
    if (!shell_program_path_allowed(g_v5_shell_program_rows[idx].path)) {
        shell_log_mdi_event("program_edit_bad_path", g_v5_shell_program_rows[idx].path, 0);
        return 0;
    }
    if (stat(g_v5_shell_program_rows[idx].path, &st) != 0 || !S_ISREG(st.st_mode)) {
        shell_log_mdi_event("program_edit_unavailable", g_v5_shell_program_rows[idx].path, 0);
        return 0;
    }
    if (st.st_size <= 0 || st.st_size >= (off_t)sizeof(text)) {
        shell_log_mdi_event("program_edit_oversize_or_empty", g_v5_shell_program_rows[idx].path, 0);
        return 0;
    }
    fp = fopen(g_v5_shell_program_rows[idx].path, "rb");
    if (!fp) {
        shell_log_mdi_event("program_edit_open_failed", g_v5_shell_program_rows[idx].path, 0);
        return 0;
    }
    n = fread(text, 1, sizeof(text) - 1U, fp);
    if (ferror(fp)) {
        fclose(fp);
        shell_log_mdi_event("program_edit_read_failed", g_v5_shell_program_rows[idx].path, 0);
        return 0;
    }
    fclose(fp);
    text[n] = '\0';
    return shell_load_mdi_edit_text(
        g_v5_shell_program_rows[idx].name,
        g_v5_shell_program_rows[idx].path,
        text,
        n,
        1,
        event_name ? event_name : "program_edit");
}

static void shell_open_program_row_for_edit(int idx)
{
    if (shell_load_program_row_for_edit(idx, "program_edit_button")) {
        shell_clear_program_selection_confirm();
        shell_navigate(0, V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT);
        return;
    }
    if (g_v5_shell_program_source_label) {
        lv_label_set_text(g_v5_shell_program_source_label, "打开修改失败");
    }
}

void shell_program_edit_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    if (g_v5_shell_program_selected_index < 0 || (unsigned int)g_v5_shell_program_selected_index >= g_v5_shell_program_row_count) {
        shell_log_mdi_event("program_edit_rejected", "no selected program", 0);
        if (g_v5_shell_program_source_label) {
            lv_label_set_text(g_v5_shell_program_source_label, "未选择G代码文件");
        }
        return;
    }
    shell_open_program_row_for_edit(g_v5_shell_program_selected_index);
}

static int shell_program_row_matches_recent_click(int idx)
{
    unsigned long long now;
    if (idx < 0 || idx != g_v5_shell_program_last_click_index || (unsigned int)idx >= g_v5_shell_program_row_count ||
        !g_v5_shell_program_rows[idx].exists || g_v5_shell_program_last_click_ns == 0ULL ||
        strcmp(g_v5_shell_program_last_click_path, g_v5_shell_program_rows[idx].path) != 0) {
        return 0;
    }
    now = shell_monotonic_ns();
    return now >= g_v5_shell_program_last_click_ns && now - g_v5_shell_program_last_click_ns <= V5_PROGRAM_DOUBLE_CLICK_NS;
}

static void shell_program_row_cb(lv_event_t *event)
{
    const int *index_ptr;
    int idx;
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    index_ptr = (const int *)lv_event_get_user_data(event);
    if (!index_ptr) {
        return;
    }
    idx = *index_ptr;
    if (idx < 0 || (unsigned int)idx >= g_v5_shell_program_row_count || !g_v5_shell_program_rows[idx].exists) {
        return;
    }
    if (shell_program_row_matches_explicit_selection(idx) || shell_program_row_matches_recent_click(idx)) {
        shell_open_program_row_for_run(idx);
        return;
    }
    shell_select_program_row(idx);
}

void shell_bind_program_row_hit(lv_obj_t *obj, int *index_ptr)
{
    if (!obj || !index_ptr) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, shell_program_row_cb, LV_EVENT_CLICKED, index_ptr);
}

void shell_bind_program_row_child_hit(lv_obj_t *obj, int *index_ptr)
{
    if (!obj || !index_ptr) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, shell_program_row_cb, LV_EVENT_CLICKED, index_ptr);
}

int shell_load_current_program_for_mdi_edit(void)
{
    const V5ProgramRuntime *runtime = v5_program_controller_runtime(&g_v5_shell_program_controller);
    if (runtime && v5_program_runtime_has_open_program(runtime) && runtime->gcode_text && runtime->gcode_size > 0U) {
        return shell_load_mdi_edit_text(
            runtime->display_name,
            runtime->source_path,
            runtime->gcode_text,
            runtime->gcode_size,
            1,
            "main_program_area");
    }
    if (runtime && v5_program_runtime_has_mdi(runtime)) {
        const char *text = v5_program_runtime_mdi_text(runtime);
        return shell_load_mdi_edit_text("手动输入", 0, text, strlen(text), 0, "main_program_area_mdi");
    }
    if (g_v5_shell_mdi_line[0]) {
        g_v5_shell_mdi_edit_prepared = 1;
        shell_update_mdi_line();
        shell_log_mdi_event("mdi_edit_existing_text", "main_program_area", 1);
        return 1;
    }
    shell_log_mdi_event("program_edit_rejected", "no opened program metadata", 0);
    return 0;
}
