#include "v5_v3_local_pages.h"
#include "v5_v3_page_widgets.h"
#include "v5_native_wcs_status.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *g_offset_tabs[9];
static lv_obj_t *g_offset_tab_labels[9];
static lv_obj_t *g_offset_title_label;
static lv_obj_t *g_offset_info_wcs;
static lv_obj_t *g_offset_info_origin;
static lv_obj_t *g_offset_axis_value_labels[V5_NATIVE_READBACK_WCS_AXIS_COUNT];
static lv_obj_t *g_offset_summary_value_labels[3][V5_NATIVE_READBACK_WCS_COUNT];
static V5NativeReadback g_offset_readback;
static int g_offset_readback_valid;
static int g_offset_selected;

static const char *wcs_name(int index)
{
    static const char *names[] = {"G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"};
    return (index >= 0 && index < 9) ? names[index] : "--";
}

static void offset_format_value(char *out, size_t out_size, double value)
{
    if (!out || out_size == 0U) {
        return;
    }
    if (!isfinite(value)) {
        snprintf(out, out_size, "--");
        return;
    }
    if (value > -0.0005 && value < 0.0005) {
        value = 0.0;
    }
    snprintf(out, out_size, "%+.3f", value);
}

static void offset_refresh_wcs_readback(void)
{
    v5_native_readback_init(&g_offset_readback);
    g_offset_readback_valid = v5_native_wcs_status_read(
                                 0,
                                 V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS,
                                 &g_offset_readback) &&
                             v5_native_readback_wcs_table_known(&g_offset_readback);
}

static void offset_apply_wcs_values(void)
{
    unsigned int axis;
    unsigned int wcs;
    char text[32];

    for (axis = 0U; axis < V5_NATIVE_READBACK_WCS_AXIS_COUNT; ++axis) {
        if (!g_offset_axis_value_labels[axis]) {
            continue;
        }
        if (g_offset_readback_valid && g_offset_selected >= 0 &&
            g_offset_selected < (int)V5_NATIVE_READBACK_WCS_COUNT) {
            offset_format_value(
                text,
                sizeof(text),
                g_offset_readback.wcs_offsets[g_offset_selected][axis]);
            lv_label_set_text(g_offset_axis_value_labels[axis], text);
        } else {
            lv_label_set_text(g_offset_axis_value_labels[axis], "--");
        }
    }
    for (axis = 0U; axis < 3U; ++axis) {
        for (wcs = 0U; wcs < V5_NATIVE_READBACK_WCS_COUNT; ++wcs) {
            if (!g_offset_summary_value_labels[axis][wcs]) {
                continue;
            }
            if (g_offset_readback_valid) {
                offset_format_value(text, sizeof(text), g_offset_readback.wcs_offsets[wcs][axis]);
                lv_label_set_text(g_offset_summary_value_labels[axis][wcs], text);
            } else {
                lv_label_set_text(g_offset_summary_value_labels[axis][wcs], "--");
            }
        }
    }
    if (g_offset_info_origin) {
        lv_label_set_text(g_offset_info_origin, g_offset_readback_valid ? "有效" : "无效");
        lv_obj_set_style_text_color(g_offset_info_origin, g_offset_readback_valid ? v5_v3_page_color(91, 230, 130) : v5_v3_page_color(245, 214, 82), 0);
    }
}

static void offset_select(int index)
{
    int i;
    char title[64];
    if (index < 0 || index >= 9) {
        index = 0;
    }
    g_offset_selected = index;
    snprintf(title, sizeof(title), "当前坐标系（%s）偏置值", wcs_name(index));
    if (g_offset_title_label) {
        lv_label_set_text(g_offset_title_label, title);
    }
    if (g_offset_info_wcs) {
        lv_label_set_text(g_offset_info_wcs, wcs_name(index));
    }
    offset_refresh_wcs_readback();
    offset_apply_wcs_values();
    for (i = 0; i < 9; ++i) {
        const int selected = (i == index);
        if (g_offset_tabs[i]) {
            lv_obj_set_style_bg_color(g_offset_tabs[i], selected ? v5_v3_page_color(255, 211, 0) : v5_v3_page_color(32, 34, 34), 0);
        }
        if (g_offset_tab_labels[i]) {
            lv_obj_set_style_text_color(g_offset_tab_labels[i], selected ? v5_v3_page_color(0, 0, 0) : v5_v3_page_color(238, 245, 248), 0);
        }
    }
}

static void offset_tab_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        offset_select((int)(intptr_t)lv_event_get_user_data(event));
    }
}

static void create_offset_tab(lv_obj_t *root, int index, int x, int w)
{
    g_offset_tabs[index] = v5_v3_page_panel(root, x, 50, w, 34, 32, 34, 34);
    lv_obj_add_flag(g_offset_tabs[index], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_offset_tabs[index], offset_tab_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    g_offset_tab_labels[index] = v5_v3_page_label(g_offset_tabs[index], wcs_name(index), 0, 6, w, 24, 238, 245, 248, LV_TEXT_ALIGN_CENTER);
}

static void create_offset_current_table(lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z", "A", "C"};
    int i;
    g_offset_title_label = v5_v3_page_label(root, "当前坐标系（--）偏置值", 18, 98, 330, 24, 255, 218, 0, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_cell(root, "轴", 10, 124, 56, 30, 1, 0, 238, 245, 248);
    v5_v3_page_cell(root, "偏置值", 66, 124, 210, 30, 1, 0, 238, 245, 248);
    v5_v3_page_cell(root, "单位：mm", 276, 124, 100, 30, 1, 0, 238, 245, 248);
    for (i = 0; i < 5; ++i) {
        v5_v3_page_cell(root, axes[i], 10, 154 + i * 30, 56, 30, 0, 0, 238, 245, 248);
        g_offset_axis_value_labels[i] = v5_v3_page_cell_value_label(root, "--", 66, 154 + i * 30, 210, 30, 0, 210, 255);
        v5_v3_page_cell(root, "", 276, 154 + i * 30, 100, 30, 0, 0, 238, 245, 248);
    }
}

static void create_offset_summary(lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z"};
    int axis;
    int wcs;
    v5_v3_page_label(root, "坐标系总览（单位：mm）", 18, 312, 260, 24, 255, 218, 0, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_cell(root, "轴", 10, 340, 56, 30, 1, 0, 238, 245, 248);
    for (wcs = 0; wcs < 9; ++wcs) {
        v5_v3_page_cell(root, wcs_name(wcs), 66 + wcs * 70, 340, 70, 30, 1, 0, 238, 245, 248);
    }
    for (axis = 0; axis < 3; ++axis) {
        v5_v3_page_cell(root, axes[axis], 10, 370 + axis * 30, 56, 30, 0, 0, 238, 245, 248);
        for (wcs = 0; wcs < 9; ++wcs) {
            g_offset_summary_value_labels[axis][wcs] = v5_v3_page_cell_value_label(root, "--", 66 + wcs * 70, 370 + axis * 30, 70, 30, 238, 245, 248);
        }
    }
}

static void create_offset_machine_panel(lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z", "A", "C"};
    int i;
    lv_obj_t *box = v5_v3_page_panel(root, 386, 96, 304, 190, 7, 31, 48);
    v5_v3_page_label(box, "机械坐标", 0, 12, 304, 26, 255, 218, 0, LV_TEXT_ALIGN_CENTER);
    for (i = 0; i < 5; ++i) {
        v5_v3_page_label(box, axes[i], 26, 50 + i * 26, 38, 22, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
        v5_v3_page_label(box, "--", 104, 50 + i * 26, 140, 22, 0, 210, 255, LV_TEXT_ALIGN_RIGHT);
    }
}

static void offset_add_line(lv_obj_t *parent, const lv_point_t *points, uint16_t count, int x, int y, uint8_t r, uint8_t g, uint8_t b, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, count);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_line_color(line, v5_v3_page_color(r, g, b), 0);
    lv_obj_set_style_line_width(line, width, 0);
}

static void create_offset_axis_guide(lv_obj_t *side)
{
    static const lv_point_t cube_front[] = {{0, 30}, {64, 30}, {64, 96}, {0, 96}, {0, 30}};
    static const lv_point_t cube_back[] = {{28, 0}, {92, 0}, {92, 66}, {64, 96}};
    static const lv_point_t cube_join1[] = {{0, 30}, {28, 0}};
    static const lv_point_t cube_join2[] = {{64, 30}, {92, 0}};
    static const lv_point_t cube_join3[] = {{64, 96}, {92, 66}};
    static const lv_point_t axis_x[] = {{0, 0}, {104, 0}, {92, -8}, {104, 0}, {92, 8}};
    static const lv_point_t axis_y[] = {{0, 0}, {62, -62}, {48, -58}, {62, -62}, {58, -48}};
    static const lv_point_t axis_z[] = {{0, 0}, {0, -104}, {-8, -90}, {0, -104}, {8, -90}};
    v5_v3_page_label(side, "轴向示意图", 22, 192, 150, 24, 255, 218, 0, LV_TEXT_ALIGN_LEFT);
    offset_add_line(side, cube_front, 5, 94, 267, 238, 245, 248, 1);
    offset_add_line(side, cube_back, 4, 94, 267, 238, 245, 248, 1);
    offset_add_line(side, cube_join1, 2, 94, 267, 180, 190, 198, 1);
    offset_add_line(side, cube_join2, 2, 94, 267, 180, 190, 198, 1);
    offset_add_line(side, cube_join3, 2, 94, 267, 180, 190, 198, 1);
    offset_add_line(side, axis_x, 5, 136, 322, 255, 80, 80, 3);
    offset_add_line(side, axis_y, 5, 136, 322, 74, 220, 85, 3);
    offset_add_line(side, axis_z, 5, 136, 322, 80, 160, 255, 3);
    v5_v3_page_label(side, "X+", 246, 309, 34, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(side, "Y+", 214, 238, 34, 24, 74, 220, 85, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(side, "Z+", 128, 226, 34, 24, 80, 160, 255, LV_TEXT_ALIGN_LEFT);
}

static void create_offset_side(lv_obj_t *root)
{
    lv_obj_t *side = v5_v3_page_panel(root, 710, 88, 304, 382, 6, 23, 28);
    lv_obj_set_style_border_width(side, 0, 0);
    v5_v3_page_label(side, "当前坐标系信息", 0, 14, 304, 26, 255, 218, 0, LV_TEXT_ALIGN_CENTER);
    v5_v3_page_panel(side, 10, 52, 284, 2, 84, 96, 102);
    v5_v3_page_label(side, "当前坐标系：", 22, 84, 176, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    g_offset_info_wcs = v5_v3_page_label(side, "--", 208, 84, 64, 24, 0, 210, 255, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(side, "工件原点状态：", 22, 124, 176, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    g_offset_info_origin = v5_v3_page_label(side, "无效", 208, 124, 64, 24, 245, 214, 82, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(side, "偏置输入方式：", 22, 164, 176, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(side, "手动", 208, 164, 64, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_panel(side, 10, 210, 284, 1, 84, 96, 102);
    create_offset_axis_guide(side);
}

static lv_obj_t *offset_return_button(lv_obj_t *parent, lv_event_cb_t return_cb)
{
    lv_obj_t *btn = v5_v3_page_button(parent, "", 862, 536, 160, 50, 42, 42, 42, return_cb, 0);
    v5_v3_page_label(btn, LV_SYMBOL_NEW_LINE, 14, 7, 42, 34, 255, 218, 0, LV_TEXT_ALIGN_CENTER);
    v5_v3_page_label(btn, "返回", 54, 13, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_CENTER);
    return btn;
}

lv_obj_t *v5_v3_local_page_create_offset(lv_obj_t *screen, lv_event_cb_t return_cb)
{
    lv_obj_t *root = lv_obj_create(screen);
    int i;
    memset(g_offset_tabs, 0, sizeof(g_offset_tabs));
    memset(g_offset_tab_labels, 0, sizeof(g_offset_tab_labels));
    memset(g_offset_axis_value_labels, 0, sizeof(g_offset_axis_value_labels));
    memset(g_offset_summary_value_labels, 0, sizeof(g_offset_summary_value_labels));
    v5_native_readback_init(&g_offset_readback);
    g_offset_readback_valid = 0;
    g_offset_title_label = 0;
    g_offset_info_wcs = 0;
    g_offset_info_origin = 0;
    v5_v3_page_clear_obj(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, v5_v3_page_color(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    v5_v3_page_panel(root, 0, 0, 1024, 44, 8, 14, 16);
    v5_v3_page_label(root, "加工坐标系偏置", 12, 10, 260, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(root, "2025/05/20  10:30:45", 820, 13, 190, 24, 238, 245, 248, LV_TEXT_ALIGN_RIGHT);
    for (i = 0; i < 9; ++i) {
        create_offset_tab(root, i, 12 + i * 108, 100);
    }
    v5_v3_page_panel(root, 0, 88, 704, 382, 4, 20, 31);
    create_offset_current_table(root);
    create_offset_machine_panel(root);
    create_offset_summary(root);
    create_offset_side(root);
    v5_v3_page_panel(root, 2, 478, 1018, 44, 4, 20, 31);
    v5_v3_page_label(root, "i", 18, 486, 24, 24, 86, 204, 252, LV_TEXT_ALIGN_CENTER);
    v5_v3_page_label(root, "说明：设置工件坐标偏置后，用于加工原点定位。", 52, 486, 560, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    offset_return_button(root, return_cb);
    offset_select(0);
    return root;
}
