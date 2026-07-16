#include "v5_ui_shell_program_delete.h"

#include "v5_lvgl_headless.h"
#include "v5_popup_layout.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_ui_shell_internal.h"

#include <stdio.h>
#include <string.h>

V5MainPage g_v5_shell_main_page;
V5ProgramController g_v5_shell_program_controller;
V5UiModel g_v5_shell_model;
V5ProgramRow g_v5_shell_program_rows[V5_PROGRAM_ROWS_MAX];
unsigned int g_v5_shell_program_row_count;
int g_v5_shell_program_selected_index = -1;
int g_v5_shell_program_confirm_selected_index = -1;
char g_v5_shell_program_confirm_selected_path[384];
int g_v5_shell_program_last_click_index = -1;
unsigned long long g_v5_shell_program_last_click_ns;
char g_v5_shell_program_last_click_path[384];
lv_obj_t *g_v5_shell_program_source_label;

static int g_delete_ok = 1;
static unsigned int g_delete_count;
static unsigned int g_refresh_count;
static unsigned int g_main_dirty_count;
static unsigned int g_program_dirty_count;
static unsigned int g_main_apply_count;

int shell_program_path_allowed(const char *path)
{
    return path && strstr(path, "gcode/golden/") != NULL;
}

void shell_update_program_row(void)
{
    ++g_refresh_count;
}

void shell_mark_page_cache_dirty(V5ShellPageKind page)
{
    if (page == V5_SHELL_PAGE_MAIN) {
        ++g_main_dirty_count;
    } else if (page == V5_SHELL_PAGE_PROGRAM) {
        ++g_program_dirty_count;
    }
}

void shell_log_program_event(
    const char *event,
    const char *path,
    int ok,
    const V5ProgramOpenResult *result)
{
    (void)event;
    (void)path;
    (void)ok;
    (void)result;
}

int v5_main_page_apply_status(V5MainPage *page, const V5UiStatusView *status)
{
    (void)page;
    (void)status;
    ++g_main_apply_count;
    return 1;
}

int v5_command_program_delete(
    V5ProgramController *controller,
    const char *path,
    V5ProgramDeleteResult *result)
{
    (void)controller;
    (void)path;
    ++g_delete_count;
    memset(result, 0, sizeof(*result));
    result->ok = g_delete_ok;
    result->removed = g_delete_ok;
    result->code = g_delete_ok ? "OK" : "PROGRAM_DELETE_FAILED";
    return g_delete_ok;
}

int v5_ui_first_frame_guard_begin_overlay(V5UiFirstFrameGuard *guard)
{
    if (!guard) {
        return 0;
    }
    memset(guard, 0, sizeof(*guard));
    guard->captured = 1;
    guard->stacked = 1;
    return 1;
}

int v5_ui_first_frame_guard_present_overlay(V5UiFirstFrameGuard *guard, lv_obj_t *overlay)
{
    if (!guard || !guard->captured || !overlay) {
        return 0;
    }
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(overlay);
    return 1;
}

int v5_ui_first_frame_guard_dismiss_overlay(V5UiFirstFrameGuard *guard, lv_obj_t *overlay)
{
    if (!guard || !guard->captured || !overlay) {
        return 0;
    }
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    guard->captured = 0;
    guard->stacked = 0;
    return 1;
}

int v5_ui_first_frame_guard_set_label_text(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *label,
    const char *text)
{
    const char *next = text ? text : "";
    (void)guard;
    if (!label || strcmp(lv_label_get_text(label), next) == 0) {
        return 0;
    }
    lv_label_set_text(label, next);
    return 1;
}

int v5_ui_first_frame_guard_set_text_color(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *obj,
    lv_color_t color)
{
    (void)guard;
    if (!obj) {
        return 0;
    }
    lv_obj_set_style_text_color(obj, color, 0);
    return 1;
}

int v5_ui_first_frame_guard_set_disabled(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *obj,
    int disabled)
{
    (void)guard;
    if (!obj) {
        return 0;
    }
    if (disabled) {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
    }
    return 1;
}

int main(void)
{
    lv_obj_t *confirm;
    lv_obj_t *close;
    lv_obj_t *panel;
    lv_obj_t *overlay;
    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    g_v5_shell_program_source_label = lv_label_create(lv_scr_act());
    shell_create_program_delete_popup(lv_scr_act());
    confirm = shell_program_delete_popup_confirm_button();
    close = shell_program_delete_popup_close_button();
    panel = confirm ? lv_obj_get_parent(confirm) : NULL;
    overlay = panel ? lv_obj_get_parent(panel) : NULL;
    if (overlay) {
        lv_obj_update_layout(overlay);
    }
    if (!confirm || !close || !panel || !overlay ||
        lv_obj_get_x(panel) != V5_POPUP_PANEL_X ||
        lv_obj_get_y(panel) != V5_POPUP_PANEL_Y ||
        lv_obj_get_width(panel) != V5_POPUP_PANEL_W ||
        lv_obj_get_height(panel) != V5_POPUP_PANEL_H) {
        return 2;
    }

    g_v5_shell_program_row_count = 1U;
    g_v5_shell_program_selected_index = 0;
    g_v5_shell_program_rows[0].exists = 1;
    snprintf(g_v5_shell_program_rows[0].name, sizeof(g_v5_shell_program_rows[0].name), "delete-me.ngc");
    snprintf(g_v5_shell_program_rows[0].path, sizeof(g_v5_shell_program_rows[0].path), "gcode/golden/delete-me.ngc");
    if (!shell_program_delete_request_selected() ||
        !shell_program_delete_popup_visible() ||
        g_delete_count != 0U ||
        lv_obj_has_state(confirm, LV_STATE_DISABLED) ||
        !strstr(shell_program_delete_popup_text(), "delete-me.ngc")) {
        return 3;
    }
    lv_event_send(confirm, LV_EVENT_RELEASED, NULL);
    if (shell_program_delete_popup_visible() || g_delete_count != 1U ||
        g_refresh_count != 1U || g_v5_shell_program_selected_index != -1 ||
        g_main_dirty_count != 1U || g_program_dirty_count != 1U ||
        g_main_apply_count != 0U ||
        strcmp(lv_label_get_text(g_v5_shell_program_source_label), "已删除: delete-me.ngc") != 0) {
        return 4;
    }

    g_delete_ok = 0;
    g_v5_shell_program_selected_index = 0;
    g_v5_shell_program_rows[0].exists = 1;
    if (!shell_program_delete_request_selected()) {
        return 5;
    }
    lv_event_send(confirm, LV_EVENT_RELEASED, NULL);
    if (!shell_program_delete_popup_visible() || g_delete_count != 2U ||
        !lv_obj_has_state(confirm, LV_STATE_DISABLED) ||
        !strstr(shell_program_delete_popup_text(), "删除失败")) {
        return 6;
    }
    lv_event_send(close, LV_EVENT_RELEASED, NULL);
    if (shell_program_delete_popup_visible() || g_refresh_count != 1U) {
        return 7;
    }
    puts("v5_ui_shell_program_delete_smoke PASS");
    return 0;
}
