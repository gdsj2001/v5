#include "v5_ui_shell_internal.h"

#include "v5_button_visuals.h"
#include "v5_lvgl_remote_display.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_popup_layout.h"
#include "v5_native_home.h"

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
        message, sizeof(message), "提示: %s\n\n原因: %s%s%s%s\n\n下一步: %s",
        status->title_cn,
        status->reason_cn,
        strcmp(status->source_id, "NATIVE_UNKNOWN") == 0 && status->fingerprint[0] ? "（参考编号: " : "",
        strcmp(status->source_id, "NATIVE_UNKNOWN") == 0 ? status->fingerprint : "",
        strcmp(status->source_id, "NATIVE_UNKNOWN") == 0 && status->fingerprint[0] ? "）" : "",
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

static const char *home_phase_cn(unsigned int phase)
{
    if (phase == V5_NATIVE_HOME_PHASE_RTCP_FORCE_OFF) return "关闭RTCP";
    if (phase == V5_NATIVE_HOME_PHASE_PROOF_MOVE) return "运动证明";
    if (phase == V5_NATIVE_HOME_PHASE_ZERO_RETURN) return "回到零位";
    if (phase == V5_NATIVE_HOME_PHASE_HOMED_SYNC) return "回零同步";
    return "回零准备";
}

static const char *home_reason_cn(const char *code)
{
    if (code && strcmp(code, "HOME_RTCP_FORCE_OFF_NOT_CONFIRMED") == 0)
        return "回零前RTCP状态未能切换为关闭";
    if (code && strstr(code, "WCHECKPOINT"))
        return "旋转轴逻辑计数或运行窗口回读缺失/不一致";
    if (code && strstr(code, "SAFE_ZERO"))
        return "旋转轴等效机械零规划或到位回读未满足";
    if (code && strstr(code, "HOMED"))
        return "驱动已回零状态未能由本次原生事务确认";
    if (code && strstr(code, "STILLNESS"))
        return "轴仍在运动或静止状态无法确认";
    if (code && strstr(code, "PRECONDITION"))
        return "当前安全状态不允许回零";
    return "本次原生回零事务未取得运动与到位回读";
}

void shell_show_home_failure_popup(const V5CommandGateHomeStatus *status)
{
    V5NativeOperatorErrorStatus popup;
    char axes[24];
    if (!status || !status->terminal || status->cancelled ||
        status->phase != V5_NATIVE_HOME_PHASE_FAILED) return;
    memset(&popup, 0, sizeof(popup));
    popup.display_mode = V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP;
    snprintf(popup.title_cn, sizeof(popup.title_cn), "%s", "回零失败");
    snprintf(axes, sizeof(axes), "%s", status->current_axes[0] ? status->current_axes : "当前");
    if (strcmp(status->direct_reason, "HOME_RTCP_FORCE_OFF_NOT_CONFIRMED") == 0) {
        snprintf(popup.reason_cn, sizeof(popup.reason_cn), "%s", home_reason_cn(status->direct_reason));
    } else if (status->detail_valid) {
        snprintf(popup.reason_cn, sizeof(popup.reason_cn),
                 "%s轴在%s阶段失败：%s；实际 %.6f，目标 %.6f，容差 %.6f",
                 axes, home_phase_cn(status->failure_phase), home_reason_cn(status->direct_reason),
                 status->actual, status->target, status->tolerance);
    } else {
        snprintf(popup.reason_cn, sizeof(popup.reason_cn), "%s轴在%s阶段失败：%s",
                 axes, home_phase_cn(status->failure_phase), home_reason_cn(status->direct_reason));
    }
    snprintf(popup.next_cn, sizeof(popup.next_cn), "%s",
             "确认已取消急停、驱动无报警且轴可安全运动后重试");
    snprintf(popup.source_id, sizeof(popup.source_id), "%s", "NATIVE_HOME_TRANSACTION");
    shell_show_operator_error_popup(&popup);
}

lv_obj_t *shell_operator_error_popup_confirm_button(void)
{
    return g_operator_error_popup.confirm;
}
