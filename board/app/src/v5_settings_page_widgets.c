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

void v5_settings_page_make_value_cell_colored(lv_obj_t *parent, const char *text, int x, int y, int w, int h, int muted, uint8_t tr, uint8_t tg, uint8_t tb)
{
    lv_obj_t *cell = v5_settings_page_make_panel(parent, x, y, w, h, muted ? 38 : 5, muted ? 54 : 27, muted ? 65 : 43);
    lv_obj_t *label = v5_settings_page_make_label(cell, text, 0, 5, w, h - 6, muted ? 150 : tr, muted ? 170 : tg, muted ? 190 : tb);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static void settings_motion_model_refresh_dropdown(V5SettingsPage *page)
{
    const V5MotionModelDescriptor *model;
    unsigned int model_index = 0U;
    if (!page || !page->motion_model_dropdown) {
        return;
    }
    model = v5_motion_model_find(v5_settings_axis_table_motion_model_value());
    if (!v5_motion_model_registry_index(model, &model_index)) {
        lv_dropdown_set_selected(page->motion_model_dropdown, 0U);
        return;
    }
    lv_dropdown_set_selected(page->motion_model_dropdown, model_index + 1U);
}

static void settings_motion_model_close_dropdown_async(void *object)
{
    if (object) {
        lv_dropdown_close((lv_obj_t *)object);
    }
}

static void settings_motion_model_changed_cb(lv_event_t *event)
{
    V5SettingsPage *page = (V5SettingsPage *)lv_event_get_user_data(event);
    char selected[64];
    const V5MotionModelDescriptor *current_model;
    const V5MotionModelDescriptor *model;
    int actual_change;
    int ok;
    if (!page || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || !page->motion_model_dropdown) {
        return;
    }
    selected[0] = '\0';
    lv_dropdown_get_selected_str(page->motion_model_dropdown, selected, sizeof(selected));
    model = v5_motion_model_find(selected);
    if (!model) {
        settings_motion_model_refresh_dropdown(page);
        v5_settings_page_set_status_text(page, 245, 214, 82, "运动模型: 未登记模型");
        return;
    }
    current_model = v5_motion_model_find(
        v5_settings_axis_table_motion_model_value());
    actual_change =
        !current_model ||
        strcmp(current_model->canonical, model->canonical) != 0;
    ok = v5_settings_axis_table_commit_motion_model(model->canonical);
    settings_motion_model_refresh_dropdown(page);
    lv_async_call(
        settings_motion_model_close_dropdown_async,
        page->motion_model_dropdown);
    if (ok) {
        v5_settings_page_set_status_text(
            page,
            42,
            221,
            128,
            "运动模型: %s",
            v5_settings_axis_table_motion_model_value());
        if (actual_change) {
            char body[512];
            snprintf(
                body,
                sizeof(body),
                "提示: 默认映射已自动更新\n"
                "原因: 目标模型 %s 及 descriptor 默认轴从站映射已写入唯一 owner 并完成源位置回读\n"
                "下一步: 点击设置驱动完成默认映射预检，再到轴参数 → 从站核对或覆盖；人工覆盖后需再次设置驱动",
                model->display);
            v5_settings_page_popup_show(
                page,
                "motion_model_mapping_confirm",
                "运动模型",
                body,
                1,
                1);
        }
    } else {
        v5_settings_page_set_status_text(page, 245, 214, 82, "运动模型: owner readback 未确认");
    }
}

lv_obj_t *v5_settings_page_make_motion_model_dropdown(V5SettingsPage *page, lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *dd;
    char registry_options[128];
    char options[136];
    if (!page || !parent) {
        return 0;
    }
    if (!v5_motion_model_dropdown_options(registry_options, sizeof(registry_options))) {
        return 0;
    }
    snprintf(options, sizeof(options), "--\n%s", registry_options);
    dd = lv_dropdown_create(parent);
    lv_obj_set_pos(dd, x, y);
    lv_obj_set_size(dd, w, h);
    lv_dropdown_set_options(dd, options);
    lv_obj_set_style_bg_color(dd, v5_settings_page_rgb(5, 27, 43), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dd, 0, 0);
    lv_obj_set_style_radius(dd, 2, 0);
    lv_obj_set_style_pad_all(dd, 0, 0);
    lv_obj_set_style_text_color(dd, v5_settings_page_rgb(150, 170, 190), 0);
    lv_obj_set_style_text_align(dd, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(dd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(dd, settings_motion_model_changed_cb, LV_EVENT_VALUE_CHANGED, page);
    page->motion_model_dropdown = dd;
    settings_motion_model_refresh_dropdown(page);
    return dd;
}

static void make_value_cell(lv_obj_t *parent, const char *text, int x, int y, int w, int h, int muted)
{
    v5_settings_page_make_value_cell_colored(parent, text, x, y, w, h, muted, 226, 238, 246);
}

void v5_settings_page_axis_color(const char *axis, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!axis || !r || !g || !b) return;
    *r = 218;
    *g = 232;
    *b = 242;
    if (strcmp(axis, "X") == 0 || strcmp(axis, "A") == 0 || strcmp(axis, "B") == 0) {
        *r = 255;
        *g = 100;
        *b = 106;
    } else if (strcmp(axis, "Y") == 0) {
        *r = 0;
        *g = 232;
        *b = 150;
    } else if (strcmp(axis, "Z") == 0) {
        *r = 82;
        *g = 178;
        *b = 255;
    } else if (strcmp(axis, "C") == 0) {
        *r = 0;
        *g = 225;
        *b = 220;
    }
}

void v5_settings_page_format_mcs_value(double value, int valid, char *out, size_t out_size)
{
    double abs_value;
    long whole;
    long frac;
    if (!out || out_size == 0U) {
        return;
    }
    if (!valid || value != value || value > 99999999.0 || value < -99999999.0) {
        snprintf(out, out_size, "--");
        return;
    }
    if (value > -0.0005 && value < 0.0005) {
        value = 0.0;
    }
    abs_value = value < 0.0 ? -value : value;
    whole = (long)abs_value;
    frac = (long)((abs_value - (double)whole) * 1000.0 + 0.5);
    if (frac >= 1000) {
        frac -= 1000;
        ++whole;
    }
    if (whole > 999) {
        snprintf(out, out_size, "%c999.999", value < 0.0 ? '<' : '>');
    } else {
        snprintf(out, out_size, "%s%03ld.%03ld", value < 0.0 ? "-" : "", whole, frac);
    }
}

static lv_obj_t *make_settings_mcs_value(lv_obj_t *panel, int row_y)
{
    lv_obj_t *value = v5_settings_page_make_label(panel, "--", 76, row_y, 124, 30, 88, 204, 255);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_bg_color(value, v5_settings_page_rgb(6, 26, 39), 0);
    lv_obj_set_style_bg_opa(value, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_opa(value, LV_OPA_TRANSP, 0);
    return value;
}

void v5_settings_page_make_machine_coordinate_widget(V5SettingsPage *page, lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z", "-", "-"};
    lv_obj_t *outer = v5_settings_page_make_panel(root, 696, 42, 312, 176, 7, 31, 48);
    lv_obj_t *panel = v5_settings_page_make_panel(root, 696, 42, 216, 176, 6, 26, 39);
    lv_obj_t *unit;
    (void)outer;
    lv_obj_set_style_border_color(panel, v5_settings_page_rgb(84, 96, 102), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    v5_settings_page_make_label(panel, "机械坐标", 12, 8, 92, 24, 88, 204, 255);
    unit = v5_settings_page_make_label(panel, "MCS", 158, 8, 44, 24, 155, 177, 198);
    lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_RIGHT, 0);
    if (page) {
        v5_coordinate_digits_create_settings(&page->mcs_digits, panel, page->mcs_digits_buffer);
    }
    for (unsigned int i = 0U; i < sizeof(axes) / sizeof(axes[0]); ++i) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        int row_y = 40 + (int)i * 25;
        v5_settings_page_axis_color(axes[i], &r, &g, &b);
        if (page && i < V5_MAIN_PAGE_AXIS_COUNT) {
            page->mcs_axis_labels[i] = v5_settings_page_make_label(panel, axes[i], 12, row_y, 32, 26, r, g, b);
            page->mcs_status_slots[i] = i < 3U ? i : V5_STATUS_AXIS_COUNT;
        } else {
            v5_settings_page_make_label(panel, axes[i], 12, row_y, 32, 26, r, g, b);
        }
        if (page && i < V5_MAIN_PAGE_AXIS_COUNT) {
            page->mcs_labels[i] = make_settings_mcs_value(panel, 39 + (int)i * 25);
        }
    }
}
