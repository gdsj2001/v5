#include "v5_ui_shell_program_delete.h"

#include "v5_button_visuals.h"
#include "v5_popup_layout.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_ui_shell_internal.h"

#include <stdio.h>
#include <string.h>

static V5PopupLayoutObjects g_program_delete_popup;
static V5UiFirstFrameGuard g_program_delete_guard;
static char g_program_delete_pending_path[384];
static char g_program_delete_pending_name[168];
static int g_program_delete_refresh_pending;

static void program_delete_clear_selection(void)
{
    g_v5_shell_program_selected_index = -1;
    g_v5_shell_program_confirm_selected_index = -1;
    g_v5_shell_program_confirm_selected_path[0] = '\0';
    g_v5_shell_program_last_click_index = -1;
    g_v5_shell_program_last_click_ns = 0ULL;
    g_v5_shell_program_last_click_path[0] = '\0';
}

static void program_delete_refresh_background(void)
{
    char status[240];
    if (!g_program_delete_refresh_pending) {
        return;
    }
    program_delete_clear_selection();
    shell_update_program_row();
    if (g_v5_shell_program_source_label) {
        snprintf(
            status,
            sizeof(status),
            "已删除: %s",
            g_program_delete_pending_name[0] ? g_program_delete_pending_name : "程序文件");
        lv_label_set_text(g_v5_shell_program_source_label, status);
        lv_obj_invalidate(g_v5_shell_program_source_label);
    }
    (void)v5_main_page_apply_status(&g_v5_shell_main_page, &g_v5_shell_model.status_view);
    shell_mark_page_cache_dirty(V5_SHELL_PAGE_MAIN);
    shell_mark_page_cache_dirty(V5_SHELL_PAGE_PROGRAM);
    g_program_delete_refresh_pending = 0;
    g_program_delete_pending_path[0] = '\0';
    g_program_delete_pending_name[0] = '\0';
}

static int program_delete_dismiss(void)
{
    if (!g_program_delete_popup.overlay ||
        lv_obj_has_flag(g_program_delete_popup.overlay, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }
    if (!v5_ui_first_frame_guard_dismiss_overlay(
            &g_program_delete_guard,
            g_program_delete_popup.overlay)) {
        return 0;
    }
    program_delete_refresh_background();
    if (!g_program_delete_refresh_pending) {
        g_program_delete_pending_path[0] = '\0';
        g_program_delete_pending_name[0] = '\0';
    }
    return 1;
}

static void program_delete_close_cb(lv_event_t *event)
{
    lv_indev_t *indev;
    if (!event || lv_event_get_code(event) != LV_EVENT_RELEASED) {
        return;
    }
    v5_button_visual_release_now(lv_event_get_target(event));
    indev = lv_indev_get_act();
    if (indev) {
        lv_indev_wait_release(indev);
    }
    (void)program_delete_dismiss();
}

static void program_delete_show_failure(const V5ProgramDeleteResult *result)
{
    char message[512];
    const char *code = result && result->code ? result->code : "PROGRAM_DELETE_FAILED";
    snprintf(
        message,
        sizeof(message),
        "提示: 删除失败\n\n原因: 程序文件未删除（%s）\n\n下一步: 关闭提示，刷新列表后重试",
        code);
    (void)v5_ui_first_frame_guard_set_label_text(
        &g_program_delete_guard, g_program_delete_popup.title, "删除程序失败");
    (void)v5_ui_first_frame_guard_set_label_text(
        &g_program_delete_guard, g_program_delete_popup.message, message);
    (void)v5_ui_first_frame_guard_set_text_color(
        &g_program_delete_guard, g_program_delete_popup.message, lv_color_make(255, 96, 104));
    (void)v5_ui_first_frame_guard_set_disabled(
        &g_program_delete_guard, g_program_delete_popup.confirm, 1);
    (void)v5_ui_first_frame_guard_set_disabled(
        &g_program_delete_guard, g_program_delete_popup.close, 0);
}

static void program_delete_confirm_cb(lv_event_t *event)
{
    V5ProgramDeleteResult result;
    lv_indev_t *indev;
    if (!event || lv_event_get_code(event) != LV_EVENT_RELEASED ||
        !g_program_delete_pending_path[0] ||
        lv_obj_has_state(g_program_delete_popup.confirm, LV_STATE_DISABLED)) {
        return;
    }
    v5_button_visual_release_now(lv_event_get_target(event));
    indev = lv_indev_get_act();
    if (indev) {
        lv_indev_wait_release(indev);
    }
    memset(&result, 0, sizeof(result));
    if (!shell_program_path_allowed(g_program_delete_pending_path) ||
        !v5_command_program_delete(
            &g_v5_shell_program_controller,
            g_program_delete_pending_path,
            &result)) {
        shell_log_program_event("program_file_delete", g_program_delete_pending_path, 0, 0);
        program_delete_show_failure(&result);
        return;
    }
    shell_log_program_event("program_file_delete", g_program_delete_pending_path, 1, 0);
    g_program_delete_refresh_pending = 1;
    (void)v5_ui_first_frame_guard_set_disabled(
        &g_program_delete_guard, g_program_delete_popup.confirm, 1);
    if (!program_delete_dismiss()) {
        (void)v5_ui_first_frame_guard_set_label_text(
            &g_program_delete_guard, g_program_delete_popup.title, "删除程序完成");
        (void)v5_ui_first_frame_guard_set_label_text(
            &g_program_delete_guard,
            g_program_delete_popup.message,
            "提示: 程序文件已删除\n\n原因: 正在等待背景帧恢复\n\n下一步: 点击关闭完成列表刷新");
        (void)v5_ui_first_frame_guard_set_text_color(
            &g_program_delete_guard, g_program_delete_popup.message, lv_color_make(42, 221, 128));
    }
}

void shell_create_program_delete_popup(lv_obj_t *screen)
{
    V5PopupLayoutConfig config = {0};
    if (!screen || g_program_delete_popup.overlay) {
        return;
    }
    config.title = "删除程序";
    config.message = "";
    config.confirm_text = "确认删除";
    config.confirm_enabled = 0;
    config.confirm_cb = program_delete_confirm_cb;
    config.close_text = "取消";
    config.close_enabled = 1;
    config.close_cb = program_delete_close_cb;
    (void)v5_popup_layout_create(screen, &config, &g_program_delete_popup);
}

int shell_program_delete_request_selected(void)
{
    char message[512];
    int idx = g_v5_shell_program_selected_index;
    if (!g_program_delete_popup.overlay || shell_program_delete_popup_visible()) {
        return 0;
    }
    if (idx < 0 || (unsigned int)idx >= g_v5_shell_program_row_count ||
        !g_v5_shell_program_rows[idx].exists ||
        !shell_program_path_allowed(g_v5_shell_program_rows[idx].path)) {
        if (g_v5_shell_program_source_label) {
            lv_label_set_text(g_v5_shell_program_source_label, "请先选择要删除的G代码文件");
            lv_obj_invalidate(g_v5_shell_program_source_label);
        }
        return 0;
    }
    if (!v5_ui_first_frame_guard_begin_overlay(&g_program_delete_guard)) {
        return 0;
    }
    snprintf(
        g_program_delete_pending_path,
        sizeof(g_program_delete_pending_path),
        "%s",
        g_v5_shell_program_rows[idx].path);
    snprintf(
        g_program_delete_pending_name,
        sizeof(g_program_delete_pending_name),
        "%s",
        g_v5_shell_program_rows[idx].name);
    snprintf(
        message,
        sizeof(message),
        "提示: 即将删除程序文件\n\n原因: 已选择 %s\n\n下一步: 点击确认删除将永久删除；点击取消则保留文件",
        g_program_delete_pending_name);
    (void)v5_ui_first_frame_guard_set_label_text(
        &g_program_delete_guard, g_program_delete_popup.title, "删除程序");
    (void)v5_ui_first_frame_guard_set_label_text(
        &g_program_delete_guard, g_program_delete_popup.message, message);
    (void)v5_ui_first_frame_guard_set_text_color(
        &g_program_delete_guard, g_program_delete_popup.message, lv_color_make(226, 238, 246));
    (void)v5_ui_first_frame_guard_set_disabled(
        &g_program_delete_guard, g_program_delete_popup.confirm, 0);
    (void)v5_ui_first_frame_guard_set_disabled(
        &g_program_delete_guard, g_program_delete_popup.close, 0);
    if (!v5_ui_first_frame_guard_present_overlay(
            &g_program_delete_guard,
            g_program_delete_popup.overlay)) {
        g_program_delete_pending_path[0] = '\0';
        g_program_delete_pending_name[0] = '\0';
        return 0;
    }
    return 1;
}

void shell_program_delete_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    (void)shell_program_delete_request_selected();
}

int shell_program_delete_popup_visible(void)
{
    return g_program_delete_popup.overlay &&
        !lv_obj_has_flag(g_program_delete_popup.overlay, LV_OBJ_FLAG_HIDDEN);
}

const char *shell_program_delete_popup_text(void)
{
    return g_program_delete_popup.message ? lv_label_get_text(g_program_delete_popup.message) : "";
}

lv_obj_t *shell_program_delete_popup_confirm_button(void)
{
    return g_program_delete_popup.confirm;
}

lv_obj_t *shell_program_delete_popup_close_button(void)
{
    return g_program_delete_popup.close;
}
