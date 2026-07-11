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

static void update_toolpath_view_button_visuals(V5MainPage *page)
{
    unsigned int i;
    if (!page) {
        return;
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (!page->buttons[i] || !v5_main_page_internal_is_view_action(page->button_actions[i])) {
            continue;
        }
        if (v5_main_page_internal_view_action_matches_plane(page->button_actions[i], page->view_plane)) {
            v5_main_page_internal_set_obj_bg_color_if_changed(page->buttons[i], v5_main_page_internal_rgb(39, 113, 164), 0);
        } else {
            v5_main_page_internal_set_obj_bg_color_if_changed(page->buttons[i], v5_main_page_internal_rgb(16, 48, 77), 0);
        }
        v5_main_page_internal_set_obj_border_color_if_changed(page->buttons[i], v5_main_page_internal_rgb(76, 119, 146), 0);
    }
}

static int wcs_index_for_button_action(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_WCS_G54: return 0;
    case V5_MAIN_PAGE_ACTION_WCS_G55: return 1;
    case V5_MAIN_PAGE_ACTION_WCS_G56: return 2;
    case V5_MAIN_PAGE_ACTION_WCS_G57: return 3;
    case V5_MAIN_PAGE_ACTION_WCS_G58: return 4;
    case V5_MAIN_PAGE_ACTION_WCS_G59: return 5;
    case V5_MAIN_PAGE_ACTION_WCS_G591: return 6;
    case V5_MAIN_PAGE_ACTION_WCS_G592: return 7;
    case V5_MAIN_PAGE_ACTION_WCS_G593: return 8;
    default: return -1;
    }
}

static int jog_step_for_button_action(V5MainPageActionKind action, double *step_out)
{
    double step;
    switch (action) {
    case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
        step = 0.001;
        break;
    case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
        step = 0.01;
        break;
    case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
        step = 0.1;
        break;
    default:
        return 0;
    }
    if (step_out) {
        *step_out = step;
    }
    return 1;
}

static void set_button_state_color(lv_obj_t *button, int active, uint8_t ar, uint8_t ag, uint8_t ab, uint8_t ir, uint8_t ig, uint8_t ib)
{
    if (!button) {
        return;
    }
    v5_main_page_internal_set_obj_bg_color_if_changed(button, active ? v5_main_page_internal_rgb(ar, ag, ab) : v5_main_page_internal_rgb(ir, ig, ib), 0);
    v5_main_page_internal_set_obj_border_color_if_changed(button, active ? v5_main_page_internal_rgb(86, 228, 153) : v5_main_page_internal_rgb(76, 119, 146), 0);
}

void v5_main_page_internal_update_wcs_button_visuals(V5MainPage *page)
{
    int wcs_known;
    unsigned int i;
    if (!page) {
        return;
    }
    wcs_known = v5_native_readback_wcs_known(&page->native_readback);
    for (i = 0U; i < page->button_count; ++i) {
        int wcs_index = wcs_index_for_button_action(page->button_actions[i]);
        if (wcs_index < 0) {
            continue;
        }
        set_button_state_color(
            page->buttons[i],
            wcs_known && page->native_readback.wcs_index == wcs_index,
            35, 198, 120,
            32, 52, 73);
    }
}

static void update_jog_step_button_visuals(V5MainPage *page)
{
    unsigned int i;
    if (!page) {
        return;
    }
    for (i = 0U; i < page->button_count; ++i) {
        double step = 0.0;
        if (!jog_step_for_button_action(page->button_actions[i], &step)) {
            continue;
        }
        set_button_state_color(
            page->buttons[i],
            fabs(page->jog_step - step) < 1.0e-9,
            29, 151, 104,
            32, 52, 73);
    }
}

void v5_main_page_internal_update_axis_all_button_visuals(V5MainPage *page)
{
    unsigned int i;
    int selected;
    if (!page) {
        return;
    }
    selected = page->selection.space == V5_MAIN_PAGE_SELECT_MCS && page->selection.all_axes;
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_AXIS_ALL) {
            set_button_state_color(page->buttons[i], selected, 29, 151, 104, 42, 63, 85);
            return;
        }
    }
}

static void update_rtcp_button_visuals(V5MainPage *page)
{
    const char *text = "RTCP";
    int highlighted;
    unsigned int i;
    if (!page) {
        return;
    }
    highlighted = v5_native_readback_rtcp_known(&page->native_readback) && page->native_readback.rtcp_enabled;
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_RTCP_TOGGLE) {
            if (page->button_labels[i]) {
                v5_main_page_internal_set_label_text_if_changed(page->button_labels[i], text);
                v5_main_page_internal_set_obj_text_color_if_changed(page->button_labels[i], v5_main_page_internal_rgb(238, 245, 248), 0);
            }
            set_button_state_color(page->buttons[i], highlighted, 29, 151, 104, 42, 63, 85);
            return;
        }
    }
}

static void update_home_button_visuals(V5MainPage *page)
{
    unsigned int i;
    if (!page) {
        return;
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_HOME) {
            set_button_state_color(page->buttons[i], page->home_transaction_active, 29, 151, 104, 42, 63, 85);
            return;
        }
    }
}

void v5_main_page_internal_set_home_transaction_active(V5MainPage *page, int active, int flush)
{
    unsigned int i;
    (void)flush;
    if (!page) {
        return;
    }
    page->home_transaction_active = active ? 1 : 0;
    update_home_button_visuals(page);
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_HOME) {
            v5_button_visual_set_transaction_active(page->buttons[i], active);
            return;
        }
    }
}

void v5_main_page_internal_update_main_page_state_button_visuals(V5MainPage *page)
{
    update_toolpath_view_button_visuals(page);
    v5_main_page_internal_update_wcs_button_visuals(page);
    update_jog_step_button_visuals(page);
    v5_main_page_internal_update_axis_all_button_visuals(page);
    update_rtcp_button_visuals(page);
    update_home_button_visuals(page);
}

void v5_main_page_internal_make_v3_main_buttons(V5MainPage *page)
{
    v5_main_page_internal_make_button_rgb(page, 920, 0, 104, 60, V5_MAIN_PAGE_ACTION_NAV_MAIN, "主页面", 41, 145, 107);
    v5_main_page_internal_make_button_rgb(page, 920, 60, 104, 60, V5_MAIN_PAGE_ACTION_NAV_TOOL, "刀具补偿", 16, 48, 77);
    v5_main_page_internal_make_button_rgb(page, 920, 120, 104, 60, V5_MAIN_PAGE_ACTION_NAV_PROBE, "探测", 16, 48, 77);
    v5_main_page_internal_make_button_rgb(page, 920, 180, 104, 60, V5_MAIN_PAGE_ACTION_NAV_OFFSET, "偏置", 16, 48, 77);
    v5_main_page_internal_make_button_rgb(page, 920, 240, 104, 60, V5_MAIN_PAGE_ACTION_NAV_IO, "IO设置", 16, 48, 77);
    v5_main_page_internal_make_button_rgb(page, 920, 300, 104, 60, V5_MAIN_PAGE_ACTION_NAV_SETTINGS, "系统设置", 16, 48, 77);
    v5_main_page_internal_make_button_rgb(page, 920, 420, 104, 60, V5_MAIN_PAGE_ACTION_PAUSE, "暂停", 74, 91, 111);
    v5_main_page_internal_make_button_rgb(page, 920, 480, 104, 60, V5_MAIN_PAGE_ACTION_START, "启动", 16, 48, 77);
    v5_main_page_internal_make_button_rgb(page, 920, 540, 104, 60, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "急停", 199, 70, 46);
    v5_main_page_internal_make_button_rgb(page, 842, 14, 38, 34, V5_MAIN_PAGE_ACTION_NAV_NETWORK, "网", 16, 48, 77);

    v5_main_page_internal_make_button_rgb(page, 456, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G54, "G54", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 502, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G55, "G55", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 548, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G56, "G56", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 594, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G57, "G57", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 640, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G58, "G58", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 456, 306, 50, 20, V5_MAIN_PAGE_ACTION_WCS_G59, "G59", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 510, 306, 50, 20, V5_MAIN_PAGE_ACTION_WCS_G591, "G59.1", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 564, 306, 50, 20, V5_MAIN_PAGE_ACTION_WCS_G592, "G59.2", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 618, 306, 50, 20, V5_MAIN_PAGE_ACTION_WCS_G593, "G59.3", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 402, 328, 108, 48, V5_MAIN_PAGE_ACTION_AXIS_ALL, "机械全轴", 42, 63, 85);
    v5_main_page_internal_make_button_rgb(page, 516, 328, 50, 48, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, "RTCP", 42, 63, 85);
    v5_main_page_internal_make_button_rgb(page, 572, 328, 80, 48, V5_MAIN_PAGE_ACTION_HOME, "回零", 42, 63, 85);
    v5_main_page_internal_make_button_rgb(page, 658, 328, 82, 48, V5_MAIN_PAGE_ACTION_WORK_ZERO_X, "归零", 42, 63, 85);
    v5_main_page_internal_make_button_rgb(page, 402, 382, 54, 48, V5_MAIN_PAGE_ACTION_JOG_STEP_1, "X1", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 462, 382, 50, 48, V5_MAIN_PAGE_ACTION_JOG_STEP_10, "X10", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 518, 382, 50, 48, V5_MAIN_PAGE_ACTION_JOG_STEP_100, "X100", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 574, 382, 78, 48, V5_MAIN_PAGE_ACTION_JOG_PLUS, "点动+", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 658, 382, 82, 48, V5_MAIN_PAGE_ACTION_JOG_MINUS, "点动-", 32, 52, 73);
    v5_main_page_internal_make_button_rgb(page, 328, 262, 58, 30, V5_MAIN_PAGE_ACTION_VIEW_XY, "XY", 25, 45, 62);
    v5_main_page_internal_make_button_rgb(page, 328, 298, 58, 30, V5_MAIN_PAGE_ACTION_VIEW_XZ, "XZ", 25, 45, 62);
    v5_main_page_internal_make_button_rgb(page, 328, 334, 58, 30, V5_MAIN_PAGE_ACTION_VIEW_YZ, "YZ", 25, 45, 62);
    v5_main_page_internal_make_button_rgb(page, 328, 370, 58, 30, V5_MAIN_PAGE_ACTION_VIEW_3D, "3D", 25, 45, 62);
    v5_main_page_internal_make_button_rgb(page, 570, 444, 82, 44, V5_MAIN_PAGE_ACTION_NAV_PROGRAM, "打开程序", 16, 48, 77);
    v5_main_page_internal_make_button_rgb(page, 570, 494, 82, 44, V5_MAIN_PAGE_ACTION_NAV_MDI, "手动输入", 16, 48, 77);
    v5_main_page_internal_make_button_rgb(page, 656, 494, 82, 44, V5_MAIN_PAGE_ACTION_FIRST_POINT, "首点", 16, 48, 77);
}
