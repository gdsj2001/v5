#include "v5_ui_shell_internal.h"

#include "v5_button_visuals.h"
#include "v5_lvgl_remote_display.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_popup_layout.h"

#include <stdio.h>
#include <string.h>

static V5PopupLayoutObjects g_operator_error_popup;
static V5UiFirstFrameGuard g_operator_error_frame_guard;

void shell_hide_operator_error_popup(void)
{
    if (!g_operator_error_popup.overlay ||
        lv_obj_has_flag(g_operator_error_popup.overlay, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (!v5_ui_first_frame_guard_dismiss_overlay(
            &g_operator_error_frame_guard,
            g_operator_error_popup.overlay)) {
        return;
    }
    shell_update_top_status_label();
}

static void operator_error_close_cb(lv_event_t *event)
{
    lv_indev_t *indev;
    if (lv_event_get_code(event) != LV_EVENT_RELEASED) {
        return;
    }
    v5_button_visual_release_now(lv_event_get_target(event));
    indev = lv_indev_get_act();
    if (indev) {
        lv_indev_wait_release(indev);
    }
    shell_hide_operator_error_popup();
}

void shell_create_operator_error_popup(lv_obj_t *screen)
{
    V5PopupLayoutConfig config = {0};
    if (!screen || g_operator_error_popup.overlay) {
        return;
    }
    v5_ui_first_frame_guard_clear(&g_operator_error_frame_guard);
    config.title = "控制提示";
    config.message = "";
    config.message_color_rgb = 0xFF6068U;
    config.confirm_text = "确认继续";
    config.confirm_enabled = 0;
    config.close_text = "关闭";
    config.close_enabled = 1;
    config.close_cb = operator_error_close_cb;
    (void)v5_popup_layout_create(screen, &config, &g_operator_error_popup);
}

void shell_show_operator_error_popup(const V5NativeOperatorErrorStatus *status)
{
    char message[896];
    int opening;
    if (!status || status->display_mode != V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP ||
        !g_operator_error_popup.overlay || !g_operator_error_popup.message) {
        return;
    }
    opening = lv_obj_has_flag(g_operator_error_popup.overlay, LV_OBJ_FLAG_HIDDEN);
    if (opening) {
        if (!v5_ui_first_frame_guard_begin_overlay(&g_operator_error_frame_guard)) {
            return;
        }
    }
    snprintf(
        message, sizeof(message), "提示: %s\n\n原因: %s\n\n下一步: %s",
        status->title_cn,
        status->reason_cn,
        status->next_cn);
    (void)v5_ui_first_frame_guard_set_label_text(
        &g_operator_error_frame_guard,
        g_operator_error_popup.message,
        message);
    if (opening) {
        (void)v5_ui_first_frame_guard_present_overlay(
            &g_operator_error_frame_guard,
            g_operator_error_popup.overlay);
    }
}

int shell_operator_error_popup_visible(void)
{
    return g_operator_error_popup.overlay &&
        !lv_obj_has_flag(g_operator_error_popup.overlay, LV_OBJ_FLAG_HIDDEN);
}

void shell_raise_operator_error_popup(void)
{
    if (shell_operator_error_popup_visible()) {
        v5_ui_first_frame_guard_invalidate_or_defer(
            &g_operator_error_frame_guard,
            g_operator_error_popup.message);
    }
}

const char *shell_operator_error_popup_text(void)
{
    return g_operator_error_popup.message ? lv_label_get_text(g_operator_error_popup.message) : "";
}

lv_obj_t *shell_operator_error_popup_confirm_button(void)
{
    return g_operator_error_popup.confirm;
}
