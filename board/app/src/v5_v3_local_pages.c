#include "v5_v3_local_pages.h"
#include "v5_native_wcs_status.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum V5ToolTab {
    V5_TOOL_TAB_LIST = 0,
    V5_TOOL_TAB_LINEAR,
    V5_TOOL_TAB_ROTARY,
    V5_TOOL_TAB_COUNT
} V5ToolTab;

static lv_obj_t *g_tool_tabs[V5_TOOL_TAB_COUNT];
static lv_obj_t *g_tool_tab_labels[V5_TOOL_TAB_COUNT];
static lv_obj_t *g_tool_content;
static lv_event_cb_t g_tool_return_cb;
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

static void create_tool_list_page(lv_obj_t *parent, lv_event_cb_t return_cb);
static void create_tool_setting_page(lv_obj_t *parent, const char *title, const char *library, lv_event_cb_t return_cb);

static lv_color_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}
static void clear_obj(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}
static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *obj = lv_obj_create(parent);
    clear_obj(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, c(r, g, b), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, c(86, 96, 100), 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, lv_text_align_t align)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text ? text : "--");
    lv_obj_set_style_text_color(obj, c(r, g, b), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, lv_event_cb_t cb, void *user)
{
    lv_obj_t *obj = lv_btn_create(parent);
    lv_obj_t *txt;
    clear_obj(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, c(r, g, b), 0);
    lv_obj_set_style_bg_color(obj, c(245, 214, 82), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, c(76, 119, 146), 0);
    if (cb) {
        lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
    }
    txt = label(obj, text, 0, (h - 24) / 2, w, 24,
                (r + g + b > 540) ? 16 : 238,
                (r + g + b > 540) ? 20 : 245,
                (r + g + b > 540) ? 24 : 248,
                LV_TEXT_ALIGN_CENTER);
    (void)txt;
    return obj;
}

static lv_obj_t *cell(lv_obj_t *parent, const char *text, int x, int y, int w, int h, int header, int selected, uint8_t tr, uint8_t tg, uint8_t tb)
{
    lv_obj_t *obj = panel(parent, x, y, w, h,
                          selected ? 255 : (header ? 39 : 8),
                          selected ? 210 : (header ? 45 : 26),
                          selected ? 0 : (header ? 48 : 36));
    label(obj, text, 0, (h - 22) / 2, w, 24,
          selected ? 16 : tr, selected ? 20 : tg, selected ? 24 : tb,
          LV_TEXT_ALIGN_CENTER);
    return obj;
}

static lv_obj_t *cell_value_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t tr, uint8_t tg, uint8_t tb)
{
    lv_obj_t *obj = panel(parent, x, y, w, h, 8, 26, 36);
    return label(obj, text, 0, (h - 22) / 2, w, 24, tr, tg, tb, LV_TEXT_ALIGN_CENTER);
}

static void divider(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *line = panel(parent, x, y, w, h, 78, 92, 100);
    lv_obj_set_style_border_width(line, 0, 0);
}

static void tool_select_tab(int tab)
{
    int i;
    if (tab < 0 || tab >= V5_TOOL_TAB_COUNT) {
        tab = V5_TOOL_TAB_LIST;
    }
    for (i = 0; i < V5_TOOL_TAB_COUNT; ++i) {
        const int active = (i == tab);
        if (g_tool_tabs[i]) {
            lv_obj_set_style_bg_color(g_tool_tabs[i], active ? c(255, 210, 0) : c(25, 36, 43), 0);
            lv_obj_set_style_border_color(g_tool_tabs[i], active ? c(255, 232, 80) : c(88, 104, 112), 0);
        }
        if (g_tool_tab_labels[i]) {
            lv_obj_set_style_text_color(g_tool_tab_labels[i], active ? c(16, 20, 24) : c(238, 245, 248), 0);
        }
    }
    if (!g_tool_content) {
        return;
    }
    lv_obj_clean(g_tool_content);
    if (tab == V5_TOOL_TAB_LINEAR) {
        create_tool_setting_page(g_tool_content, "直排刀参数", "直排刀库", g_tool_return_cb);
    } else if (tab == V5_TOOL_TAB_ROTARY) {
        create_tool_setting_page(g_tool_content, "圆盘刀参数", "圆盘刀库", g_tool_return_cb);
    } else {
        create_tool_list_page(g_tool_content, g_tool_return_cb);
    }
}

static void tool_tab_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        tool_select_tab((int)(intptr_t)lv_event_get_user_data(event));
    }
}

static void create_tool_tab(lv_obj_t *root, int index, const char *title, int x, int w)
{
    g_tool_tabs[index] = button(root, title, x, 12, w, 48, 25, 36, 43, tool_tab_cb, (void *)(intptr_t)index);
    g_tool_tab_labels[index] = lv_obj_get_child(g_tool_tabs[index], 0);
}

static void create_tool_list_table(lv_obj_t *parent)
{
    static const char *rows[][6] = {
        {"01", "立铣刀", "直排刀", "125.000", "5.000", "有效"},
        {"02", "钻头", "直排刀", "85.000", "2.000", "有效"},
        {"03", "面铣刀", "圆盘刀", "140.000", "10.000", "有效"},
        {"04", "镗刀", "直排刀", "110.000", "4.000", "有效"},
        {"05", "球头铣刀", "直排刀", "95.000", "3.000", "有效"},
        {"06", "倒角刀", "直排刀", "60.000", "1.000", "无效"},
    };
    static const int widths[] = {68, 148, 104, 94, 94, 84};
    static const char *headers[] = {"刀号", "刀具名称", "类型", "H", "R", "状态"};
    int row;
    int col;
    int x;
    for (col = 0, x = 12; col < 6; ++col) {
        cell(parent, headers[col], x, 72, widths[col], 32, 1, 0, 238, 245, 248);
        x += widths[col];
    }
    for (row = 0; row < 6; ++row) {
        for (col = 0, x = 12; col < 6; ++col) {
            const int valid = (col == 5 && strcmp(rows[row][5], "有效") == 0);
            cell(parent, rows[row][col], x, 104 + row * 32, widths[col], 32, 0, row == 0 && col == 0,
                 valid ? 130 : 238, valid ? 220 : 245, valid ? 55 : 248);
            x += widths[col];
        }
    }
}

static void create_library_choice(lv_obj_t *parent, int y, const char *text, int selected)
{
    lv_obj_t *card = panel(parent, 22, y, 326, 74, selected ? 255 : 28, selected ? 210 : 36, selected ? 0 : 43);
    lv_obj_t *radio = panel(card, 22, 18, 38, 38, selected ? 255 : 28, selected ? 210 : 36, selected ? 0 : 43);
    lv_obj_set_style_radius(radio, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(radio, 3, 0);
    lv_obj_set_style_border_color(radio, selected ? c(16, 20, 24) : c(238, 245, 248), 0);
    if (selected) {
        lv_obj_t *dot = panel(card, 34, 30, 14, 14, 16, 20, 24);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    }
    label(card, text, 84, 22, 190, 30, selected ? 16 : 238, selected ? 20 : 245, selected ? 24 : 248, LV_TEXT_ALIGN_LEFT);
}

static void create_tool_list_page(lv_obj_t *parent, lv_event_cb_t return_cb)
{
    lv_obj_t *left = panel(parent, 16, 10, 606, 306, 7, 31, 48);
    lv_obj_t *right = panel(parent, 638, 10, 370, 306, 7, 31, 48);
    lv_obj_t *summary;
    label(left, "刀具列表", 18, 18, 160, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    create_tool_list_table(left);
    label(right, "刀库选择", 22, 18, 160, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    create_library_choice(right, 78, "直排刀库", 1);
    create_library_choice(right, 170, "圆盘刀库", 0);
    divider(right, 22, 256, 326, 1);
    label(right, "当前刀库:", 22, 270, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(right, "直排刀库", 140, 270, 120, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    summary = panel(parent, 16, 334, 992, 78, 7, 31, 48);
    label(summary, "当前刀具信息", 16, 10, 180, 26, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, "当前刀号:", 46, 45, 94, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, "01", 154, 45, 50, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    divider(summary, 250, 34, 1, 32);
    label(summary, "刀具名称:", 294, 45, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, "立铣刀", 420, 45, 90, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    divider(summary, 540, 34, 1, 32);
    label(summary, "刀库类型:", 584, 45, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, "直排刀", 710, 45, 90, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    divider(summary, 820, 34, 1, 32);
    label(summary, "状态:", 864, 45, 60, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, "有效", 930, 45, 60, 24, 130, 220, 55, LV_TEXT_ALIGN_LEFT);
    button(parent, "新建", 16, 448, 184, 58, 25, 36, 43, 0, 0);
    button(parent, "编辑", 216, 448, 184, 58, 25, 36, 43, 0, 0);
    button(parent, "删除", 416, 448, 184, 58, 25, 36, 43, 0, 0);
    button(parent, "保存", 616, 448, 184, 58, 25, 36, 43, 0, 0);
    button(parent, "返回", 816, 448, 192, 58, 25, 36, 43, return_cb, 0);
}

static void create_param_row(lv_obj_t *parent, int y, const char *name, const char *value, const char *unit)
{
    lv_obj_t *box;
    label(parent, name, 22, y + 4, 146, 22, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    box = panel(parent, 190, y, 178, 24, 6, 20, 28);
    label(box, value, 0, 2, 178, 20, 238, 245, 248, LV_TEXT_ALIGN_CENTER);
    if (unit && unit[0]) {
        label(parent, unit, 380, y + 4, 42, 22, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    }
}

static void create_tool_setting_page(lv_obj_t *parent, const char *title, const char *library, lv_event_cb_t return_cb)
{
    lv_obj_t *params = panel(parent, 16, 10, 418, 342, 7, 31, 48);
    lv_obj_t *diagram = panel(parent, 450, 10, 558, 342, 7, 31, 48);
    lv_obj_t *summary = panel(parent, 16, 366, 992, 70, 7, 31, 48);
    int i;
    label(params, title, 22, 18, 190, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    create_param_row(params, 50, "刀位数量", "12", "");
    create_param_row(params, 76, strcmp(library, "直排刀库") == 0 ? "排数" : "第一刀位角度", strcmp(library, "直排刀库") == 0 ? "2" : "0.0", strcmp(library, "直排刀库") == 0 ? "" : "deg");
    create_param_row(params, 102, strcmp(library, "直排刀库") == 0 ? "每排刀数" : "旋转方向", strcmp(library, "直排刀库") == 0 ? "6" : "顺时针", "");
    create_param_row(params, 128, "换刀位号", "1", "");
    create_param_row(params, 154, strcmp(library, "直排刀库") == 0 ? "刀位间距" : "刀盘半径", strcmp(library, "直排刀库") == 0 ? "80.000" : "220.000", "mm");
    create_param_row(params, 180, "换刀安全高度Z", strcmp(library, "直排刀库") == 0 ? "150.000" : "180.000", "");
    create_param_row(params, 206, "换刀位置X", strcmp(library, "直排刀库") == 0 ? "320.000" : "450.000", "");
    create_param_row(params, 232, "换刀位置Y", "0.000", "");
    create_param_row(params, 258, "换刀位置Z", strcmp(library, "直排刀库") == 0 ? "150.000" : "180.000", "");
    label(params, strcmp(library, "直排刀库") == 0 ? "启用到位检测" : "启用刀位确认", 68, 308, 170, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    panel(params, 22, 302, 32, 32, 255, 210, 0);

    label(diagram, strcmp(library, "直排刀库") == 0 ? "直排刀示意图" : "圆盘刀示意图", 22, 18, 190, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    if (strcmp(library, "直排刀库") == 0) {
        panel(diagram, 54, 103, 450, 10, 92, 96, 100);
        panel(diagram, 54, 197, 450, 10, 92, 96, 100);
        for (i = 0; i < 12; ++i) {
            int row = i / 6;
            int col = i % 6;
            cell(diagram, (char [4]){(char)('0' + ((i + 1) / 10)), (char)('0' + ((i + 1) % 10)), 0, 0}, 54 + col * 82, 82 + row * 94, 42, 42, 0, i == 0, 238, 245, 248);
        }
        label(diagram, "2排 x 6位", 420, 18, 100, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    } else {
        lv_obj_t *disc = panel(diagram, 184, 58, 224, 224, 34, 40, 42);
        lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
        panel(diagram, 263, 137, 66, 66, 8, 24, 32);
        label(diagram, "12 位 / 30.0 deg", 408, 96, 130, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
        label(diagram, "旋转方向: 顺时针", 408, 126, 130, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    }
    panel(diagram, 38, 270, 480, 50, 8, 26, 36);
    label(diagram, strcmp(library, "直排刀库") == 0 ? "说明: 按刀位数量/排数排列，换刀到对应刀位。" : "说明: 刀位按数量等分圆周，按方向换到指定刀位。", 60, 284, 438, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);

    label(summary, "当前设置", 16, 10, 150, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, "当前刀库:", 50, 42, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, library, 160, 42, 110, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    divider(summary, 300, 34, 1, 28);
    label(summary, "刀位数量:", 330, 42, 96, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, "12", 442, 42, 48, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    divider(summary, 520, 34, 1, 28);
    label(summary, "状态:", 820, 42, 60, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(summary, "有效", 878, 42, 60, 24, 130, 220, 55, LV_TEXT_ALIGN_LEFT);
    button(parent, "编辑", 16, 448, 184, 58, 25, 36, 43, 0, 0);
    button(parent, "保存", 216, 448, 184, 58, 25, 36, 43, 0, 0);
    button(parent, "应用", 416, 448, 184, 58, 25, 36, 43, 0, 0);
    button(parent, "测试", 616, 448, 184, 58, 25, 36, 43, 0, 0);
    button(parent, "返回", 816, 448, 192, 58, 25, 36, 43, return_cb, 0);
}

lv_obj_t *v5_v3_local_page_create_tool(lv_obj_t *screen, lv_event_cb_t return_cb)
{
    lv_obj_t *root = lv_obj_create(screen);
    memset(g_tool_tabs, 0, sizeof(g_tool_tabs));
    memset(g_tool_tab_labels, 0, sizeof(g_tool_tab_labels));
    clear_obj(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, c(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    create_tool_tab(root, V5_TOOL_TAB_LIST, "刀具列表+刀库选择", 16, 220);
    create_tool_tab(root, V5_TOOL_TAB_LINEAR, "直排刀设置", 240, 220);
    create_tool_tab(root, V5_TOOL_TAB_ROTARY, "圆盘刀设置", 464, 220);
    divider(root, 16, 64, 992, 1);
    g_tool_return_cb = return_cb;
    g_tool_content = lv_obj_create(root);
    clear_obj(g_tool_content);
    lv_obj_set_pos(g_tool_content, 0, 68);
    lv_obj_set_size(g_tool_content, 1024, 532);
    lv_obj_set_style_bg_opa(g_tool_content, LV_OPA_TRANSP, 0);
    tool_select_tab(V5_TOOL_TAB_LIST);
    return root;
}

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
        lv_obj_set_style_text_color(g_offset_info_origin, g_offset_readback_valid ? c(91, 230, 130) : c(245, 214, 82), 0);
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
            lv_obj_set_style_bg_color(g_offset_tabs[i], selected ? c(255, 211, 0) : c(32, 34, 34), 0);
        }
        if (g_offset_tab_labels[i]) {
            lv_obj_set_style_text_color(g_offset_tab_labels[i], selected ? c(0, 0, 0) : c(238, 245, 248), 0);
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
    g_offset_tabs[index] = panel(root, x, 50, w, 34, 32, 34, 34);
    lv_obj_add_flag(g_offset_tabs[index], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_offset_tabs[index], offset_tab_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    g_offset_tab_labels[index] = label(g_offset_tabs[index], wcs_name(index), 0, 6, w, 24, 238, 245, 248, LV_TEXT_ALIGN_CENTER);
}

static void create_offset_current_table(lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z", "A", "C"};
    int i;
    g_offset_title_label = label(root, "当前坐标系（--）偏置值", 18, 98, 330, 24, 255, 218, 0, LV_TEXT_ALIGN_LEFT);
    cell(root, "轴", 10, 124, 56, 30, 1, 0, 238, 245, 248);
    cell(root, "偏置值", 66, 124, 210, 30, 1, 0, 238, 245, 248);
    cell(root, "单位：mm", 276, 124, 100, 30, 1, 0, 238, 245, 248);
    for (i = 0; i < 5; ++i) {
        cell(root, axes[i], 10, 154 + i * 30, 56, 30, 0, 0, 238, 245, 248);
        g_offset_axis_value_labels[i] = cell_value_label(root, "--", 66, 154 + i * 30, 210, 30, 0, 210, 255);
        cell(root, "", 276, 154 + i * 30, 100, 30, 0, 0, 238, 245, 248);
    }
}

static void create_offset_summary(lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z"};
    int axis;
    int wcs;
    label(root, "坐标系总览（单位：mm）", 18, 312, 260, 24, 255, 218, 0, LV_TEXT_ALIGN_LEFT);
    cell(root, "轴", 10, 340, 56, 30, 1, 0, 238, 245, 248);
    for (wcs = 0; wcs < 9; ++wcs) {
        cell(root, wcs_name(wcs), 66 + wcs * 70, 340, 70, 30, 1, 0, 238, 245, 248);
    }
    for (axis = 0; axis < 3; ++axis) {
        cell(root, axes[axis], 10, 370 + axis * 30, 56, 30, 0, 0, 238, 245, 248);
        for (wcs = 0; wcs < 9; ++wcs) {
            g_offset_summary_value_labels[axis][wcs] = cell_value_label(root, "--", 66 + wcs * 70, 370 + axis * 30, 70, 30, 238, 245, 248);
        }
    }
}

static void create_offset_machine_panel(lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z", "A", "C"};
    int i;
    lv_obj_t *box = panel(root, 386, 96, 304, 190, 7, 31, 48);
    label(box, "机械坐标", 0, 12, 304, 26, 255, 218, 0, LV_TEXT_ALIGN_CENTER);
    for (i = 0; i < 5; ++i) {
        label(box, axes[i], 26, 50 + i * 26, 38, 22, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
        label(box, "--", 104, 50 + i * 26, 140, 22, 0, 210, 255, LV_TEXT_ALIGN_RIGHT);
    }
}

static void offset_add_line(lv_obj_t *parent, const lv_point_t *points, uint16_t count, int x, int y, uint8_t r, uint8_t g, uint8_t b, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, count);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_line_color(line, c(r, g, b), 0);
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
    label(side, "轴向示意图", 22, 192, 150, 24, 255, 218, 0, LV_TEXT_ALIGN_LEFT);
    offset_add_line(side, cube_front, 5, 94, 267, 238, 245, 248, 1);
    offset_add_line(side, cube_back, 4, 94, 267, 238, 245, 248, 1);
    offset_add_line(side, cube_join1, 2, 94, 267, 180, 190, 198, 1);
    offset_add_line(side, cube_join2, 2, 94, 267, 180, 190, 198, 1);
    offset_add_line(side, cube_join3, 2, 94, 267, 180, 190, 198, 1);
    offset_add_line(side, axis_x, 5, 136, 322, 255, 80, 80, 3);
    offset_add_line(side, axis_y, 5, 136, 322, 74, 220, 85, 3);
    offset_add_line(side, axis_z, 5, 136, 322, 80, 160, 255, 3);
    label(side, "X+", 246, 309, 34, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(side, "Y+", 214, 238, 34, 24, 74, 220, 85, LV_TEXT_ALIGN_LEFT);
    label(side, "Z+", 128, 226, 34, 24, 80, 160, 255, LV_TEXT_ALIGN_LEFT);
}

static void create_offset_side(lv_obj_t *root)
{
    lv_obj_t *side = panel(root, 710, 88, 304, 382, 6, 23, 28);
    lv_obj_set_style_border_width(side, 0, 0);
    label(side, "当前坐标系信息", 0, 14, 304, 26, 255, 218, 0, LV_TEXT_ALIGN_CENTER);
    panel(side, 10, 52, 284, 2, 84, 96, 102);
    label(side, "当前坐标系：", 22, 84, 176, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    g_offset_info_wcs = label(side, "--", 208, 84, 64, 24, 0, 210, 255, LV_TEXT_ALIGN_LEFT);
    label(side, "工件原点状态：", 22, 124, 176, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    g_offset_info_origin = label(side, "无效", 208, 124, 64, 24, 245, 214, 82, LV_TEXT_ALIGN_LEFT);
    label(side, "偏置输入方式：", 22, 164, 176, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(side, "手动", 208, 164, 64, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    panel(side, 10, 210, 284, 1, 84, 96, 102);
    create_offset_axis_guide(side);
}

static lv_obj_t *offset_return_button(lv_obj_t *parent, lv_event_cb_t return_cb)
{
    lv_obj_t *btn = button(parent, "", 862, 536, 160, 50, 42, 42, 42, return_cb, 0);
    label(btn, LV_SYMBOL_NEW_LINE, 14, 7, 42, 34, 255, 218, 0, LV_TEXT_ALIGN_CENTER);
    label(btn, "返回", 54, 13, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_CENTER);
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
    clear_obj(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, c(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    panel(root, 0, 0, 1024, 44, 8, 14, 16);
    label(root, "加工坐标系偏置", 12, 10, 260, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    label(root, "2025/05/20  10:30:45", 820, 13, 190, 24, 238, 245, 248, LV_TEXT_ALIGN_RIGHT);
    for (i = 0; i < 9; ++i) {
        create_offset_tab(root, i, 12 + i * 108, 100);
    }
    panel(root, 0, 88, 704, 382, 4, 20, 31);
    create_offset_current_table(root);
    create_offset_machine_panel(root);
    create_offset_summary(root);
    create_offset_side(root);
    panel(root, 2, 478, 1018, 44, 4, 20, 31);
    label(root, "i", 18, 486, 24, 24, 86, 204, 252, LV_TEXT_ALIGN_CENTER);
    label(root, "说明：设置工件坐标偏置后，用于加工原点定位。", 52, 486, 560, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    offset_return_button(root, return_cb);
    offset_select(0);
    return root;
}
