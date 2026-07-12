#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_popup_layout.h"
#include "v5_ui_first_frame_guard.h"

#include <stdio.h>

static unsigned int g_capture_slots[8];
static unsigned int g_capture_count;
static unsigned int g_blit_slots[8];
static unsigned int g_blit_count;
static unsigned int g_publish_count;
static int g_output_suppressed;
static int g_publish_allowed = 1;
static int g_blit_allowed = 1;

int v5_lvgl_remote_display_cache_capture(unsigned int slot)
{
    if (g_capture_count < sizeof(g_capture_slots) / sizeof(g_capture_slots[0])) {
        g_capture_slots[g_capture_count++] = slot;
    }
    return 1;
}

int v5_lvgl_remote_display_cache_blit(unsigned int slot)
{
    if (!g_blit_allowed) {
        return 0;
    }
    if (g_blit_count < sizeof(g_blit_slots) / sizeof(g_blit_slots[0])) {
        g_blit_slots[g_blit_count++] = slot;
    }
    return 1;
}

int v5_lvgl_remote_display_set_output_suppressed(int suppressed)
{
    int previous = g_output_suppressed;
    g_output_suppressed = suppressed ? 1 : 0;
    return previous;
}

int v5_lvgl_remote_display_publish_current_frame(void)
{
    if (g_output_suppressed || !g_publish_allowed) {
        return 0;
    }
    ++g_publish_count;
    return 1;
}

int main(void)
{
    V5UiFirstFrameGuard first = {0};
    V5UiFirstFrameGuard second = {0};
    V5UiOperatorInputSequence input_sequence = {0};
    lv_obj_t *root;
    lv_obj_t *first_overlay;
    lv_obj_t *second_overlay;
    lv_obj_t *popup_panel;
    V5PopupLayoutConfig popup_config = {0};
    V5PopupLayoutObjects popup = {0};
    lv_obj_t *estop;
    lv_obj_t *hidden_main_root;
    lv_obj_t *safety_parent;
    uint16_t invalid_before_inner_close;

    if (V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT != 9U ||
        V5_REMOTE_DISPLAY_CACHE_MAIN != 0U ||
        V5_REMOTE_DISPLAY_CACHE_MDI != 8U ||
        V5_REMOTE_DISPLAY_CACHE_OVERLAY_BASE != 9U ||
        V5_REMOTE_DISPLAY_CACHE_OVERLAY_COUNT != 4U ||
        V5_REMOTE_DISPLAY_CACHE_COUNT != 13U) {
        return 1;
    }

    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 2;
    }
    root = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    popup_config.title = "测试提示";
    popup_config.message = "提示: 测试\n原因: 验证公共构造器\n下一步: 关闭";
    popup_config.close_enabled = 1;
    if (!v5_popup_layout_create(root, &popup_config, &popup)) {
        return 3;
    }
    first_overlay = popup.overlay;
    popup_panel = popup.panel;
    second_overlay = lv_obj_create(root);
    estop = lv_btn_create(root);
    lv_obj_set_pos(estop, 900, 520);
    lv_obj_set_size(estop, 100, 60);
    lv_obj_update_layout(first_overlay);
    if (lv_obj_get_x(first_overlay) != V5_POPUP_MASK_X ||
        lv_obj_get_y(first_overlay) != V5_POPUP_MASK_Y ||
        lv_obj_get_width(first_overlay) != V5_POPUP_MASK_W ||
        lv_obj_get_height(first_overlay) != V5_POPUP_MASK_H ||
        lv_obj_get_x(popup_panel) != V5_POPUP_PANEL_X ||
        lv_obj_get_y(popup_panel) != V5_POPUP_PANEL_Y ||
        lv_obj_get_width(popup_panel) != V5_POPUP_PANEL_W ||
        lv_obj_get_height(popup_panel) != V5_POPUP_PANEL_H ||
        !popup.title || !popup.message || !popup.status || !popup.confirm || !popup.close ||
        strcmp(lv_label_get_text(popup.title), "测试提示") != 0 ||
        strcmp(lv_label_get_text(popup.message), popup_config.message) != 0 ||
        !lv_obj_has_state(popup.confirm, LV_STATE_DISABLED) ||
        lv_obj_has_state(popup.close, LV_STATE_DISABLED)) {
        return 3;
    }
    v5_ui_first_frame_guard_register_safety_button(estop);

    if (!v5_ui_first_frame_guard_begin_overlay(&first) ||
        !v5_ui_first_frame_guard_present_overlay(&first, first_overlay)) {
        return 4;
    }
    safety_parent = lv_obj_get_parent(estop);
    if (!safety_parent || safety_parent == root || safety_parent == first_overlay ||
        !v5_ui_first_frame_guard_overlay_active()) {
        return 5;
    }
    if (!v5_ui_first_frame_guard_begin_overlay(&second) ||
        !v5_ui_first_frame_guard_present_overlay(&second, second_overlay) ||
        lv_obj_get_parent(estop) != safety_parent) {
        return 6;
    }
    if (!v5_ui_first_frame_guard_set_label_text(&first, popup.message, "下层更新") ||
        strcmp(lv_label_get_text(popup.message), "下层更新") != 0 ||
        first.deferred_dirty_count != 1U ||
        v5_ui_first_frame_guard_set_label_text(&first, popup.message, "下层更新") ||
        first.deferred_dirty_count != 1U) {
        return 31;
    }
    if (g_output_suppressed || g_publish_count != 2U) {
        return 7;
    }
    if (g_capture_count != 2U ||
        g_capture_slots[0] != V5_REMOTE_DISPLAY_CACHE_OVERLAY_0 ||
        g_capture_slots[1] != V5_REMOTE_DISPLAY_CACHE_OVERLAY_1 ||
        lv_obj_get_parent(estop) != safety_parent) {
        return 8;
    }

    v5_ui_first_frame_guard_invalidate_or_defer(&first, popup_panel);
    invalid_before_inner_close = lv_obj_get_disp(second_overlay)->inv_p;
    if (v5_ui_first_frame_guard_dismiss_overlay(&first, first_overlay) ||
        g_blit_count != 0U || lv_obj_get_parent(estop) != safety_parent) {
        return 9;
    }
    if (!v5_ui_first_frame_guard_dismiss_overlay(&second, second_overlay)) {
        return 10;
    }
    if (g_blit_count != 1U) {
        return 11;
    }
    if (g_blit_slots[0] != V5_REMOTE_DISPLAY_CACHE_OVERLAY_1) {
        return 14;
    }
    if (lv_obj_get_parent(estop) != safety_parent) {
        return 15;
    }
    if (lv_obj_get_disp(first_overlay)->inv_p <= invalid_before_inner_close) {
        return 16;
    }
    if (!v5_ui_first_frame_guard_dismiss_overlay(&first, first_overlay)) {
        return 12;
    }
    if (g_blit_count != 2U ||
        g_blit_slots[1] != V5_REMOTE_DISPLAY_CACHE_OVERLAY_0 ||
        lv_obj_get_parent(estop) != root ||
        v5_ui_first_frame_guard_overlay_active() ||
        !lv_obj_has_flag(first_overlay, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(second_overlay, LV_OBJ_FLAG_HIDDEN)) {
        return 13;
    }
    if (!v5_ui_first_frame_guard_input_suppressed() ||
        v5_ui_first_frame_guard_operator_input_allowed_at(100, 100) ||
        !v5_ui_first_frame_guard_operator_input_allowed_at(950, 550)) {
        return 22;
    }
    lv_tick_inc(V5_UI_POST_MODAL_INPUT_SUPPRESSION_MS - 1U);
    if (!v5_ui_first_frame_guard_input_suppressed()) {
        return 23;
    }
    lv_tick_inc(1U);
    if (v5_ui_first_frame_guard_input_suppressed() ||
        !v5_ui_first_frame_guard_operator_input_allowed_at(100, 100)) {
        return 24;
    }

    hidden_main_root = lv_obj_create(root);
    lv_obj_set_size(hidden_main_root, 1024, 600);
    lv_obj_add_flag(hidden_main_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_parent(estop, hidden_main_root);
    lv_obj_set_pos(estop, 900, 520);
    if (!v5_ui_first_frame_guard_begin_overlay(&first) ||
        !v5_ui_first_frame_guard_present_overlay(&first, first_overlay) ||
        !v5_ui_first_frame_guard_dismiss_overlay(&first, first_overlay) ||
        lv_obj_get_parent(estop) != hidden_main_root ||
        v5_ui_first_frame_guard_operator_input_allowed_at(950, 550)) {
        return 25;
    }
    v5_ui_first_frame_guard_input_sequence_reset(&input_sequence);
    if (v5_ui_first_frame_guard_input_sequence_begin(&input_sequence, 950, 550)) {
        return 26;
    }
    lv_tick_inc(V5_UI_POST_MODAL_INPUT_SUPPRESSION_MS);
    if (v5_ui_first_frame_guard_input_suppressed() ||
        v5_ui_first_frame_guard_input_sequence_continue(&input_sequence) ||
        v5_ui_first_frame_guard_input_sequence_end(&input_sequence)) {
        return 27;
    }

    lv_obj_set_parent(estop, root);
    lv_obj_set_pos(estop, 900, 520);
    if (!v5_ui_first_frame_guard_begin_overlay(&first) ||
        !v5_ui_first_frame_guard_present_overlay(&first, first_overlay) ||
        !v5_ui_first_frame_guard_dismiss_overlay(&first, first_overlay)) {
        return 28;
    }
    v5_ui_first_frame_guard_input_sequence_reset(&input_sequence);
    if (!v5_ui_first_frame_guard_input_sequence_begin(&input_sequence, 950, 550) ||
        !input_sequence.safety) {
        return 29;
    }
    lv_tick_inc(V5_UI_POST_MODAL_INPUT_SUPPRESSION_MS);
    if (v5_ui_first_frame_guard_input_suppressed() ||
        !v5_ui_first_frame_guard_input_sequence_continue(&input_sequence) ||
        !v5_ui_first_frame_guard_input_sequence_end(&input_sequence)) {
        return 30;
    }

    if (!v5_ui_first_frame_guard_begin_overlay(&first)) {
        return 17;
    }
    g_publish_allowed = 0;
    if (v5_ui_first_frame_guard_present_overlay(&first, first_overlay) ||
        v5_ui_first_frame_guard_overlay_active() ||
        first.captured ||
        !lv_obj_has_flag(first_overlay, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_get_parent(estop) != root) {
        return 18;
    }
    g_publish_allowed = 1;

    if (!v5_ui_first_frame_guard_begin_overlay(&first) ||
        !v5_ui_first_frame_guard_present_overlay(&first, first_overlay)) {
        return 19;
    }
    g_blit_allowed = 0;
    if (v5_ui_first_frame_guard_dismiss_overlay(&first, first_overlay) ||
        !v5_ui_first_frame_guard_overlay_active() ||
        !first.captured ||
        lv_obj_has_flag(first_overlay, LV_OBJ_FLAG_HIDDEN) ||
        v5_ui_first_frame_guard_input_suppressed()) {
        return 20;
    }
    g_blit_allowed = 1;
    if (!v5_ui_first_frame_guard_dismiss_overlay(&first, first_overlay) ||
        v5_ui_first_frame_guard_overlay_active() ||
        lv_obj_get_parent(estop) != root ||
        !v5_ui_first_frame_guard_input_suppressed()) {
        return 21;
    }

    puts("v5_ui_first_frame_guard_smoke PASS");
    return 0;
}
