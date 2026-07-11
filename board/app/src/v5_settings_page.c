#include "v5_settings_page.h"
#include "v5_button_visuals.h"
#include "v5_settings_actions.h"
#include "v5_settings_axis_table.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include "v5_settings_page_internal.h"

void v5_settings_page_init(V5SettingsPage *page)
{
    if (page) {
        memset(page, 0, sizeof(*page));
    }
}

int v5_settings_page_create(V5SettingsPage *page, lv_obj_t *parent)
{
    int i;
    if (!page || !parent) {
        return 0;
    }
    v5_settings_page_init(page);
    page->root = lv_obj_create(parent);
    v5_settings_axis_table_begin_page();
    v5_settings_page_clear_obj_style(page->root);
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_set_size(page->root, 1024, 600);
    lv_obj_set_style_bg_color(page->root, v5_settings_page_rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);

    v5_settings_page_make_label(page->root, "参数设置", 30, 17, 100, 24, 226, 238, 246);
    page->machine_code_label = v5_settings_page_make_label(page->root, "本机码 未登记", 500, 17, 198, 24, 155, 177, 198);
    v5_settings_page_refresh_machine_code_label(page);

    v5_settings_page_make_panel(page->root, 16, 42, 220, 176, 7, 31, 48);
    v5_settings_page_make_label(page->root, "运动模型", 24, 55, 100, 24, 155, 177, 198);
    v5_settings_page_make_motion_model_dropdown(page, page->root, 25, 78, 190, 36);
    v5_settings_page_make_label(page->root, "脉冲/总线", 24, 132, 100, 24, 226, 238, 246);
    v5_settings_page_make_value_cell_colored(page->root,
                            v5_settings_axis_table_bus_pulse_value(),
                            162,
                            127,
                            70,
                            31,
                            0,
                            150,
                            170,
                            190);

    v5_settings_page_make_panel(page->root, 247, 42, 449, 176, 7, 31, 48);
    v5_settings_page_make_label(page->root, "机床坐标目标位置表(G53)", 264, 55, 230, 24, 0, 190, 255);
    v5_settings_page_make_label(page->root, "X", 395, 86, 36, 20, 155, 177, 198);
    v5_settings_page_make_label(page->root, "Y", 479, 86, 36, 20, 155, 177, 198);
    v5_settings_page_make_label(page->root, "Z", 563, 86, 36, 20, 155, 177, 198);
    {
        static const char *g53_labels[] = {"A中心", "B中心", "C中心", "对刀仪", "5方向监测仪"};
        static const unsigned char g53_label_colors[][3] = {
            {255, 100, 106},
            {226, 238, 246},
            {0, 225, 220},
            {155, 177, 198},
            {155, 177, 198},
        };
        for (i = 0; i < 5; ++i) {
            v5_settings_page_make_label(page->root,
                       g53_labels[i],
                       276,
                       107 + i * 22,
                       i == 4 ? 100 : 68,
                       22,
                       g53_label_colors[i][0],
                       g53_label_colors[i][1],
                       g53_label_colors[i][2]);
        }
    }
    for (i = 0; i < 15; ++i) {
        unsigned int row = (unsigned int)(i / 3);
        unsigned int col = (unsigned int)(i % 3);
        v5_settings_axis_table_create_g53_cell(page->root,
                                               row,
                                               col,
                                               366 + (i % 3) * 84,
                                               106 + (i / 3) * 22,
                                               76,
                                               22);
    }

    v5_settings_page_make_machine_coordinate_widget(page, page->root);

    v5_settings_axis_table_set_commit_callback(v5_settings_page_parameter_changed_cb, page);
    v5_settings_axis_table_set_axis_zero_callback(v5_settings_page_axis_zero_requested_cb, page);
    v5_settings_axis_table_create(page->root);
    page->status_label = v5_settings_page_make_label(page->root, "", 24, 34, 1, 1, 155, 177, 198);
    page->status_timer = lv_timer_create(v5_settings_page_status_timer_cb, 250, page);

    v5_settings_page_make_button(page, "下载授权", 410, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_AUTH_DOWNLOAD);
    v5_settings_page_make_button(page, "服务器下载", 494, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_SERVER_DOWNLOAD);
    v5_settings_page_make_button(page, "扫描从站", 578, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_SCAN);
    v5_settings_page_make_button(page, "复位驱动", 662, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_DRIVE_RESET);
    v5_settings_page_make_button(page, "读取驱动", 746, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_READ);
    v5_settings_page_make_button(page, "清除故障", 830, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_FAULT_RESET);
    v5_settings_page_make_button(page, "设置驱动", 914, 236, 82, 34, 39, 113, 164, V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE);
    v5_settings_page_make_button(page, "登记本机码", 806, 8, 94, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_DNA_REGISTER);
    v5_settings_page_make_button(page, "保存并重启", 902, 8, 92, 34, 74, 91, 111, V5_MAIN_PAGE_ACTION_SETTINGS_SAVE_RETURN);
    v5_settings_page_popup_create(page);
    return page->button_count == V5_SETTINGS_PAGE_BUTTON_COUNT;
}

int v5_settings_page_set_native_readback(V5SettingsPage *page, const V5NativeReadback *readback)
{
    const V5MotionModelDescriptor *model = 0;
    unsigned int registry_id;
    char axis_text[2] = {'-', '\0'};
    unsigned int i;
    if (!page) {
        return 0;
    }
    if (readback && v5_native_readback_motion_model_known(readback)) {
        model = v5_motion_model_find(readback->motion_model);
    }
    registry_id = model ? model->registry_id : 0U;
    if (page->mcs_model_registry_id == registry_id) {
        return model != 0;
    }
    page->mcs_model_registry_id = registry_id;
    for (i = 0U; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        char axis = i < 3U ? "XYZ"[i] : '-';
        uint8_t r;
        uint8_t g;
        uint8_t b;
        page->mcs_status_slots[i] = i < 3U ? i : V5_STATUS_AXIS_COUNT;
        if (model && i == 3U) {
            axis = model->first_rotary_axis;
            page->mcs_status_slots[i] = model->first_status_slot;
        } else if (model && i == 4U) {
            axis = model->second_rotary_axis;
            page->mcs_status_slots[i] = model->second_status_slot;
        }
        axis_text[0] = axis;
        v5_settings_page_axis_color(axis_text, &r, &g, &b);
        if (page->mcs_axis_labels[i]) {
            lv_label_set_text(page->mcs_axis_labels[i], axis_text);
            lv_obj_set_style_text_color(page->mcs_axis_labels[i], v5_settings_page_rgb(r, g, b), 0);
        }
    }
    return model != 0;
}

int v5_settings_page_apply_status(V5SettingsPage *page, const V5UiStatusView *status)
{
    char text[24];
    int valid;
    if (!page) {
        return 0;
    }
    valid = status && ((status->valid_mask & V5_STATUS_VALID_MCS) != 0U);
    for (unsigned int i = 0U; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        unsigned int slot = page->mcs_status_slots[i];
        int row_valid = valid && slot < V5_STATUS_AXIS_COUNT;
        if (!page->mcs_labels[i]) {
            continue;
        }
        v5_settings_page_format_mcs_value(row_valid ? status->mcs[slot] : 0.0, row_valid, text, sizeof(text));
        lv_label_set_text(page->mcs_labels[i], text);
        lv_obj_set_style_text_color(page->mcs_labels[i], row_valid ? v5_settings_page_rgb(88, 204, 255) : v5_settings_page_rgb(155, 177, 198), 0);
        v5_coordinate_digits_set_value(
            &page->mcs_digits,
            0U,
            i,
            text,
            row_valid ? v5_settings_page_rgb(88, 204, 255) : v5_settings_page_rgb(155, 177, 198));
    }
    return 1;
}

void v5_settings_page_set_navigation_callback(V5SettingsPage *page, V5UiNavigationCallback cb, void *user_data)
{
    if (!page) {
        return;
    }
    page->navigation_cb = cb;
    page->navigation_user_data = user_data;
}

int v5_settings_page_trigger_action(V5SettingsPage *page, V5MainPageActionKind action, V5MainPageActionReport *report)
{
    V5MainPageActionReport local_report;
    V5MainPageActionReport *out = report ? report : &local_report;
    V5SettingsActionResult action_result;
    if (!page) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->action = action;
    if (v5_settings_action_start(action, &action_result)) {
        out->prepared = 1;
        out->local_only = 0;
        out->request.kind = V5_COMMAND_UI_LOCAL;
        out->command.kind = V5_COMMAND_UI_LOCAL;
        out->command.name = action_result.name;
        out->command.owner = action_result.owner;
        out->command.accepted = 1;
        page->last_action = *out;
        v5_settings_page_set_status_text(page, 88, 204, 255, "%s: 已启动 %s",
                        v5_main_page_action_label(action),
                        action_result.result_path ? action_result.result_path : "");
        {
            const char *daemon_action = action_result.message ? action_result.message : "accepted";
            char body[256];
            snprintf(body, sizeof(body), "提示: 已启动\n原因: %s\n下一步: 等待后台完成", daemon_action);
            v5_settings_page_popup_show(page, action_result.daemon_action, v5_settings_page_status_action_label(action_result.daemon_action), body, 0, 0);
        }
        return 1;
    }
    out->prepared = 0;
    out->local_only = 1;
    out->request.kind = V5_COMMAND_UI_LOCAL;
    out->command.kind = V5_COMMAND_UI_LOCAL;
    out->command.name = "settings_action_blocked";
    out->command.owner = "ui_layout_shell";
    out->command.accepted = 0;
    page->last_action = *out;
    v5_settings_page_set_status_text(page, 245, 214, 82, "%s: 已阻断 %s",
                    v5_main_page_action_label(action),
                    action_result.message ? action_result.message : "unsupported");
    {
        char body[256];
        snprintf(body, sizeof(body), "提示: BLOCKED\n原因: %s\n下一步: 检查后台动作通道", action_result.message ? action_result.message : "unsupported");
        v5_settings_page_popup_show(page, "", v5_main_page_action_label(action), body, 1, 0);
    }
    return 1;
}
