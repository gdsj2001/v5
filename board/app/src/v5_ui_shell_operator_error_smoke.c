#include "v5_ui_shell_internal.h"

#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_popup_layout.h"
#include "v5_ui_first_frame_guard.h"

#include <stdio.h>
#include <string.h>

static unsigned int g_guard_begin_count;
static unsigned int g_guard_restore_count;
static lv_obj_t *g_registered_safety_button;
V5MainPage g_v5_shell_main_page;

void shell_clear_style(lv_obj_t *obj)
{
    if (obj) {
        lv_obj_remove_style_all(obj);
    }
}

lv_color_t shell_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

void shell_update_top_status_label(void)
{
}

void v5_ui_first_frame_guard_begin(V5UiFirstFrameGuard *guard, unsigned int slot)
{
    if (guard) {
        guard->slot = slot;
        guard->captured = 1;
    }
    g_guard_begin_count += 1U;
}

int v5_ui_first_frame_guard_begin_overlay(V5UiFirstFrameGuard *guard)
{
    v5_ui_first_frame_guard_begin(guard, V5_REMOTE_DISPLAY_CACHE_OVERLAY_0);
    if (guard) {
        guard->stacked = 1;
    }
    return guard ? 1 : 0;
}

void v5_ui_first_frame_guard_register_safety_button(lv_obj_t *button)
{
    g_registered_safety_button = button;
}

void v5_ui_first_frame_guard_raise_safety(V5UiFirstFrameGuard *guard, lv_obj_t *overlay)
{
    if (!guard || !overlay || !g_registered_safety_button || guard->safety_button) {
        return;
    }
    guard->safety_button = g_registered_safety_button;
    guard->safety_parent = lv_obj_get_parent(g_registered_safety_button);
    lv_obj_set_parent(g_registered_safety_button, overlay);
    lv_obj_move_foreground(g_registered_safety_button);
}

int v5_ui_first_frame_guard_present_overlay(V5UiFirstFrameGuard *guard, lv_obj_t *overlay)
{
    if (!guard || !overlay || !guard->captured) {
        return 0;
    }
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
    v5_ui_first_frame_guard_raise_safety(guard, overlay);
    lv_obj_update_layout(overlay);
    lv_obj_invalidate(overlay);
    lv_refr_now(lv_obj_get_disp(overlay));
    return 1;
}

int v5_ui_first_frame_guard_dismiss_overlay(V5UiFirstFrameGuard *guard, lv_obj_t *overlay)
{
    if (!guard || !overlay || !guard->captured) {
        return 0;
    }
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    v5_ui_first_frame_guard_restore(guard);
    return 1;
}

int v5_ui_first_frame_guard_overlay_active(void)
{
    return 0;
}

void v5_ui_first_frame_guard_invalidate_or_defer(V5UiFirstFrameGuard *guard, lv_obj_t *obj)
{
    (void)guard;
    if (obj) {
        lv_obj_invalidate(obj);
    }
}

int v5_ui_first_frame_guard_set_label_text(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *label,
    const char *text)
{
    const char *next = text ? text : "";
    if (!label || strcmp(lv_label_get_text(label), next) == 0) {
        return 0;
    }
    lv_label_set_text(label, next);
    v5_ui_first_frame_guard_invalidate_or_defer(guard, label);
    return 1;
}

void v5_ui_first_frame_guard_restore(V5UiFirstFrameGuard *guard)
{
    if (guard) {
        if (guard->safety_button && guard->safety_parent) {
            lv_obj_set_parent(guard->safety_button, guard->safety_parent);
        }
        guard->captured = 0;
    }
    g_guard_restore_count += 1U;
}

void v5_ui_first_frame_guard_restore_dirty(V5UiFirstFrameGuard *guard, lv_obj_t *dirty_obj)
{
    (void)dirty_obj;
    v5_ui_first_frame_guard_restore(guard);
}

void v5_ui_first_frame_guard_clear(V5UiFirstFrameGuard *guard)
{
    if (guard) {
        memset(guard, 0, sizeof(*guard));
    }
}

int main(void)
{
    V5NativeOperatorErrorStatus status;
    const char *text;
    lv_obj_t *main_root;
    lv_obj_t *estop;
    lv_obj_t *confirm_button;
    lv_obj_t *confirm_label;
    lv_obj_t *popup_panel;
    lv_obj_t *popup_overlay;
    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    main_root = lv_obj_create(lv_scr_act());
    estop = lv_btn_create(main_root);
    lv_obj_set_pos(estop, 920, 540);
    lv_obj_set_size(estop, 104, 60);
    g_v5_shell_main_page.root = main_root;
    g_v5_shell_main_page.buttons[0] = estop;
    g_v5_shell_main_page.button_actions[0] = V5_MAIN_PAGE_ACTION_ESTOP_FORCE;
    g_v5_shell_main_page.button_count = 1U;
    v5_ui_first_frame_guard_register_safety_button(estop);
    shell_create_operator_error_popup(lv_scr_act());
    confirm_button = shell_operator_error_popup_confirm_button();
    if (confirm_button) {
        lv_obj_update_layout(confirm_button);
    }
    confirm_label = confirm_button ? lv_obj_get_child(confirm_button, 0) : 0;
    popup_panel = confirm_button ? lv_obj_get_parent(confirm_button) : 0;
    popup_overlay = popup_panel ? lv_obj_get_parent(popup_panel) : 0;
    if (popup_overlay) {
        lv_obj_update_layout(popup_overlay);
    }
    if (shell_operator_error_popup_visible() || !confirm_button ||
        !confirm_label || strcmp(lv_label_get_text(confirm_label), "确认继续") != 0 ||
        !lv_obj_has_state(confirm_button, LV_STATE_DISABLED) ||
        !popup_panel || !popup_overlay ||
        lv_obj_get_x(popup_overlay) != V5_POPUP_MASK_X ||
        lv_obj_get_y(popup_overlay) != V5_POPUP_MASK_Y ||
        lv_obj_get_width(popup_overlay) != V5_POPUP_MASK_W ||
        lv_obj_get_height(popup_overlay) != V5_POPUP_MASK_H ||
        lv_obj_get_x(popup_panel) != V5_POPUP_PANEL_X ||
        lv_obj_get_y(popup_panel) != V5_POPUP_PANEL_Y ||
        lv_obj_get_width(popup_panel) != V5_POPUP_PANEL_W ||
        lv_obj_get_height(popup_panel) != V5_POPUP_PANEL_H ||
        lv_obj_get_x(confirm_button) != V5_POPUP_CONFIRM_X ||
        lv_obj_get_y(confirm_button) != V5_POPUP_CONFIRM_Y ||
        lv_obj_get_width(confirm_button) != V5_POPUP_CONFIRM_W ||
        lv_obj_get_height(confirm_button) != V5_POPUP_CONFIRM_H) {
        return 2;
    }

    v5_native_operator_error_status_init(&status);
    status.generation = 1ULL;
    status.display_mode = V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS;
    snprintf(status.title_cn, sizeof(status.title_cn), "%s", "点动未执行");
    snprintf(status.reason_cn, sizeof(status.reason_cn), "%s", "当前状态不允许点动");
    snprintf(status.next_cn, sizeof(status.next_cn), "%s", "松开按钮后重试");
    shell_show_operator_error_popup(&status);
    if (shell_operator_error_popup_visible() || g_guard_begin_count != 0U) {
        return 3;
    }

    status.generation = 2ULL;
    status.display_mode = V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP;
    snprintf(status.source_id, sizeof(status.source_id), "%s", "MOTION_INTERNAL_123");
    snprintf(status.title_cn, sizeof(status.title_cn), "%s", "轴限位阻止运动");
    snprintf(status.reason_cn, sizeof(status.reason_cn), "%s", "A轴目标超出允许范围");
    snprintf(status.next_cn, sizeof(status.next_cn), "%s", "检查目标位置后重新运行");
    shell_show_operator_error_popup(&status);
    if (!shell_operator_error_popup_visible() || g_guard_begin_count != 1U) {
        return 4;
    }
    if (lv_obj_get_parent(estop) == main_root) {
        return 5;
    }
    text = shell_operator_error_popup_text();
    if (!strstr(text, "提示: 轴限位阻止运动") ||
        !strstr(text, "原因: A轴目标超出允许范围") ||
        !strstr(text, "下一步: 检查目标位置后重新运行") ||
        strstr(text, "MOTION_INTERNAL_123") || strstr(text, "LinuxCNC")) {
        return 6;
    }

    status.generation = 3ULL;
    snprintf(status.source_id, sizeof(status.source_id), "%s", "NATIVE_UNKNOWN");
    snprintf(status.fingerprint, sizeof(status.fingerprint), "%s", "A1B2C3D4E5F6");
    snprintf(status.title_cn, sizeof(status.title_cn), "%s", "控制系统错误");
    snprintf(status.reason_cn, sizeof(status.reason_cn), "%s", "控制系统收到尚未登记的异常，当前操作结果不可信");
    snprintf(status.next_cn, sizeof(status.next_cn), "%s", "停止当前操作并联系维护人员");
    shell_show_operator_error_popup(&status);
    text = shell_operator_error_popup_text();
    if (!strstr(text, "尚未登记的异常") ||
        strstr(text, "参考编号") || strstr(text, "A1B2C3D4E5F6") ||
        strstr(text, "NATIVE_UNKNOWN")) {
        return 7;
    }

    shell_hide_operator_error_popup();
    if (shell_operator_error_popup_visible()) {
        return 8;
    }
    if (g_guard_restore_count != 1U) {
        return 9;
    }
    if (lv_obj_get_parent(estop) != main_root) {
        return 10;
    }
    puts("v5_ui_shell_operator_error_smoke PASS");
    return 0;
}
