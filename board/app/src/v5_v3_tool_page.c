#include "v5_v3_local_pages.h"
#include "v5_v3_page_widgets.h"

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

static void create_tool_list_page(lv_obj_t *parent, lv_event_cb_t return_cb);
static void create_tool_setting_page(lv_obj_t *parent, const char *title, const char *library, lv_event_cb_t return_cb);

static void tool_select_tab(int tab)
{
    int i;
    if (tab < 0 || tab >= V5_TOOL_TAB_COUNT) {
        tab = V5_TOOL_TAB_LIST;
    }
    for (i = 0; i < V5_TOOL_TAB_COUNT; ++i) {
        const int active = (i == tab);
        if (g_tool_tabs[i]) {
            lv_obj_set_style_bg_color(g_tool_tabs[i], active ? v5_v3_page_color(255, 210, 0) : v5_v3_page_color(25, 36, 43), 0);
            lv_obj_set_style_border_color(g_tool_tabs[i], active ? v5_v3_page_color(255, 232, 80) : v5_v3_page_color(88, 104, 112), 0);
        }
        if (g_tool_tab_labels[i]) {
            lv_obj_set_style_text_color(g_tool_tab_labels[i], active ? v5_v3_page_color(16, 20, 24) : v5_v3_page_color(238, 245, 248), 0);
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
    g_tool_tabs[index] = v5_v3_page_button(root, title, x, 12, w, 48, 25, 36, 43, tool_tab_cb, (void *)(intptr_t)index);
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
        v5_v3_page_cell(parent, headers[col], x, 72, widths[col], 32, 1, 0, 238, 245, 248);
        x += widths[col];
    }
    for (row = 0; row < 6; ++row) {
        for (col = 0, x = 12; col < 6; ++col) {
            const int valid = (col == 5 && strcmp(rows[row][5], "有效") == 0);
            v5_v3_page_cell(parent, rows[row][col], x, 104 + row * 32, widths[col], 32, 0, row == 0 && col == 0,
                 valid ? 130 : 238, valid ? 220 : 245, valid ? 55 : 248);
            x += widths[col];
        }
    }
}

static void create_library_choice(lv_obj_t *parent, int y, const char *text, int selected)
{
    lv_obj_t *card = v5_v3_page_panel(parent, 22, y, 326, 74, selected ? 255 : 28, selected ? 210 : 36, selected ? 0 : 43);
    lv_obj_t *radio = v5_v3_page_panel(card, 22, 18, 38, 38, selected ? 255 : 28, selected ? 210 : 36, selected ? 0 : 43);
    lv_obj_set_style_radius(radio, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(radio, 3, 0);
    lv_obj_set_style_border_color(radio, selected ? v5_v3_page_color(16, 20, 24) : v5_v3_page_color(238, 245, 248), 0);
    if (selected) {
        lv_obj_t *dot = v5_v3_page_panel(card, 34, 30, 14, 14, 16, 20, 24);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    }
    v5_v3_page_label(card, text, 84, 22, 190, 30, selected ? 16 : 238, selected ? 20 : 245, selected ? 24 : 248, LV_TEXT_ALIGN_LEFT);
}

static void create_tool_list_page(lv_obj_t *parent, lv_event_cb_t return_cb)
{
    lv_obj_t *left = v5_v3_page_panel(parent, 16, 10, 606, 306, 7, 31, 48);
    lv_obj_t *right = v5_v3_page_panel(parent, 638, 10, 370, 306, 7, 31, 48);
    lv_obj_t *summary;
    v5_v3_page_label(left, "刀具列表", 18, 18, 160, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    create_tool_list_table(left);
    v5_v3_page_label(right, "刀库选择", 22, 18, 160, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    create_library_choice(right, 78, "直排刀库", 1);
    create_library_choice(right, 170, "圆盘刀库", 0);
    v5_v3_page_divider(right, 22, 256, 326, 1);
    v5_v3_page_label(right, "当前刀库:", 22, 270, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(right, "直排刀库", 140, 270, 120, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    summary = v5_v3_page_panel(parent, 16, 334, 992, 78, 7, 31, 48);
    v5_v3_page_label(summary, "当前刀具信息", 16, 10, 180, 26, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, "当前刀号:", 46, 45, 94, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, "01", 154, 45, 50, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_divider(summary, 250, 34, 1, 32);
    v5_v3_page_label(summary, "刀具名称:", 294, 45, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, "立铣刀", 420, 45, 90, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_divider(summary, 540, 34, 1, 32);
    v5_v3_page_label(summary, "刀库类型:", 584, 45, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, "直排刀", 710, 45, 90, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_divider(summary, 820, 34, 1, 32);
    v5_v3_page_label(summary, "状态:", 864, 45, 60, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, "有效", 930, 45, 60, 24, 130, 220, 55, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_button(parent, "新建", 16, 448, 184, 58, 25, 36, 43, 0, 0);
    v5_v3_page_button(parent, "编辑", 216, 448, 184, 58, 25, 36, 43, 0, 0);
    v5_v3_page_button(parent, "删除", 416, 448, 184, 58, 25, 36, 43, 0, 0);
    v5_v3_page_button(parent, "保存", 616, 448, 184, 58, 25, 36, 43, 0, 0);
    v5_v3_page_button(parent, "返回", 816, 448, 192, 58, 25, 36, 43, return_cb, 0);
}

static void create_param_row(lv_obj_t *parent, int y, const char *name, const char *value, const char *unit)
{
    lv_obj_t *box;
    v5_v3_page_label(parent, name, 22, y + 4, 146, 22, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    box = v5_v3_page_panel(parent, 190, y, 178, 24, 6, 20, 28);
    v5_v3_page_label(box, value, 0, 2, 178, 20, 238, 245, 248, LV_TEXT_ALIGN_CENTER);
    if (unit && unit[0]) {
        v5_v3_page_label(parent, unit, 380, y + 4, 42, 22, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    }
}

static void create_tool_setting_page(lv_obj_t *parent, const char *title, const char *library, lv_event_cb_t return_cb)
{
    lv_obj_t *params = v5_v3_page_panel(parent, 16, 10, 418, 342, 7, 31, 48);
    lv_obj_t *diagram = v5_v3_page_panel(parent, 450, 10, 558, 342, 7, 31, 48);
    lv_obj_t *summary = v5_v3_page_panel(parent, 16, 366, 992, 70, 7, 31, 48);
    int i;
    v5_v3_page_label(params, title, 22, 18, 190, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    create_param_row(params, 50, "刀位数量", "12", "");
    create_param_row(params, 76, strcmp(library, "直排刀库") == 0 ? "排数" : "第一刀位角度", strcmp(library, "直排刀库") == 0 ? "2" : "0.0", strcmp(library, "直排刀库") == 0 ? "" : "deg");
    create_param_row(params, 102, strcmp(library, "直排刀库") == 0 ? "每排刀数" : "旋转方向", strcmp(library, "直排刀库") == 0 ? "6" : "顺时针", "");
    create_param_row(params, 128, "换刀位号", "1", "");
    create_param_row(params, 154, strcmp(library, "直排刀库") == 0 ? "刀位间距" : "刀盘半径", strcmp(library, "直排刀库") == 0 ? "80.000" : "220.000", "mm");
    create_param_row(params, 180, "换刀安全高度Z", strcmp(library, "直排刀库") == 0 ? "150.000" : "180.000", "");
    create_param_row(params, 206, "换刀位置X", strcmp(library, "直排刀库") == 0 ? "320.000" : "450.000", "");
    create_param_row(params, 232, "换刀位置Y", "0.000", "");
    create_param_row(params, 258, "换刀位置Z", strcmp(library, "直排刀库") == 0 ? "150.000" : "180.000", "");
    v5_v3_page_label(params, strcmp(library, "直排刀库") == 0 ? "启用到位检测" : "启用刀位确认", 68, 308, 170, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_panel(params, 22, 302, 32, 32, 255, 210, 0);

    v5_v3_page_label(diagram, strcmp(library, "直排刀库") == 0 ? "直排刀示意图" : "圆盘刀示意图", 22, 18, 190, 30, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    if (strcmp(library, "直排刀库") == 0) {
        v5_v3_page_panel(diagram, 54, 103, 450, 10, 92, 96, 100);
        v5_v3_page_panel(diagram, 54, 197, 450, 10, 92, 96, 100);
        for (i = 0; i < 12; ++i) {
            int row = i / 6;
            int col = i % 6;
            v5_v3_page_cell(diagram, (char [4]){(char)('0' + ((i + 1) / 10)), (char)('0' + ((i + 1) % 10)), 0, 0}, 54 + col * 82, 82 + row * 94, 42, 42, 0, i == 0, 238, 245, 248);
        }
        v5_v3_page_label(diagram, "2排 x 6位", 420, 18, 100, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    } else {
        lv_obj_t *disc = v5_v3_page_panel(diagram, 184, 58, 224, 224, 34, 40, 42);
        lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
        v5_v3_page_panel(diagram, 263, 137, 66, 66, 8, 24, 32);
        v5_v3_page_label(diagram, "12 位 / 30.0 deg", 408, 96, 130, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
        v5_v3_page_label(diagram, "旋转方向: 顺时针", 408, 126, 130, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    }
    v5_v3_page_panel(diagram, 38, 270, 480, 50, 8, 26, 36);
    v5_v3_page_label(diagram, strcmp(library, "直排刀库") == 0 ? "说明: 按刀位数量/排数排列，换刀到对应刀位。" : "说明: 刀位按数量等分圆周，按方向换到指定刀位。", 60, 284, 438, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);

    v5_v3_page_label(summary, "当前设置", 16, 10, 150, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, "当前刀库:", 50, 42, 100, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, library, 160, 42, 110, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_divider(summary, 300, 34, 1, 28);
    v5_v3_page_label(summary, "刀位数量:", 330, 42, 96, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, "12", 442, 42, 48, 24, 255, 210, 0, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_divider(summary, 520, 34, 1, 28);
    v5_v3_page_label(summary, "状态:", 820, 42, 60, 24, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_label(summary, "有效", 878, 42, 60, 24, 130, 220, 55, LV_TEXT_ALIGN_LEFT);
    v5_v3_page_button(parent, "编辑", 16, 448, 184, 58, 25, 36, 43, 0, 0);
    v5_v3_page_button(parent, "保存", 216, 448, 184, 58, 25, 36, 43, 0, 0);
    v5_v3_page_button(parent, "应用", 416, 448, 184, 58, 25, 36, 43, 0, 0);
    v5_v3_page_button(parent, "测试", 616, 448, 184, 58, 25, 36, 43, 0, 0);
    v5_v3_page_button(parent, "返回", 816, 448, 192, 58, 25, 36, 43, return_cb, 0);
}

lv_obj_t *v5_v3_local_page_create_tool(lv_obj_t *screen, lv_event_cb_t return_cb)
{
    lv_obj_t *root = lv_obj_create(screen);
    memset(g_tool_tabs, 0, sizeof(g_tool_tabs));
    memset(g_tool_tab_labels, 0, sizeof(g_tool_tab_labels));
    v5_v3_page_clear_obj(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, v5_v3_page_color(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    create_tool_tab(root, V5_TOOL_TAB_LIST, "刀具列表+刀库选择", 16, 220);
    create_tool_tab(root, V5_TOOL_TAB_LINEAR, "直排刀设置", 240, 220);
    create_tool_tab(root, V5_TOOL_TAB_ROTARY, "圆盘刀设置", 464, 220);
    v5_v3_page_divider(root, 16, 64, 992, 1);
    g_tool_return_cb = return_cb;
    g_tool_content = lv_obj_create(root);
    v5_v3_page_clear_obj(g_tool_content);
    lv_obj_set_pos(g_tool_content, 0, 68);
    lv_obj_set_size(g_tool_content, 1024, 532);
    lv_obj_set_style_bg_opa(g_tool_content, LV_OPA_TRANSP, 0);
    tool_select_tab(V5_TOOL_TAB_LIST);
    return root;
}
