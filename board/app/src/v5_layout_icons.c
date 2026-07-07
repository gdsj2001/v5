#include "v5_layout_icons.h"

#include "v5_layout_icon_masks.h"

static lv_color_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

static void add_icon_line(lv_obj_t *parent, const lv_point_t *points, uint16_t count, int x, int y, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, 28, 28);
    lv_obj_set_style_line_color(line, rgb(238, 245, 248), 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_line_set_points(line, points, count);
}

static void add_mill_icon(lv_obj_t *parent, int cx, int cy)
{
    static const lv_point_t body[] = {{11, 0}, {22, 13}, {11, 26}, {0, 13}, {11, 0}};
    static const lv_point_t hole[] = {
        {6, 0}, {9, 1}, {11, 3}, {12, 6}, {11, 9}, {9, 11}, {6, 12},
        {3, 11}, {1, 9}, {0, 6}, {1, 3}, {3, 1}, {6, 0}
    };
    add_icon_line(parent, body, (uint16_t)(sizeof(body) / sizeof(body[0])), cx - 11, cy - 13, 2);
    add_icon_line(parent, hole, (uint16_t)(sizeof(hole) / sizeof(hole[0])), cx - 6, cy - 6, 1);
}

static void add_offset_icon(lv_obj_t *parent, int cx, int cy)
{
    static const lv_point_t seg1[] = {{0, 2}, {11, 2}, {8, 0}};
    static const lv_point_t seg2[] = {{3, 0}, {0, 2}};
    static const lv_point_t seg3[] = {{2, 11}, {2, 0}, {0, 3}};
    static const lv_point_t seg4[] = {{0, 0}, {2, 3}};
    static const lv_point_t seg5[] = {{0, 3}, {13, 3}, {10, 0}};
    static const lv_point_t seg6[] = {{3, 0}, {0, 3}};
    static const lv_point_t seg7[] = {{3, 13}, {3, 0}, {0, 3}};
    static const lv_point_t seg8[] = {{0, 0}, {3, 3}};
    add_icon_line(parent, seg1, (uint16_t)(sizeof(seg1) / sizeof(seg1[0])), cx - 3, cy - 2, 1);
    add_icon_line(parent, seg2, (uint16_t)(sizeof(seg2) / sizeof(seg2[0])), cx + 5, cy, 1);
    add_icon_line(parent, seg3, (uint16_t)(sizeof(seg3) / sizeof(seg3[0])), cx - 5, cy - 11, 1);
    add_icon_line(parent, seg4, (uint16_t)(sizeof(seg4) / sizeof(seg4[0])), cx - 3, cy - 11, 1);
    add_icon_line(parent, seg5, (uint16_t)(sizeof(seg5) / sizeof(seg5[0])), cx - 8, cy + 4, 2);
    add_icon_line(parent, seg6, (uint16_t)(sizeof(seg6) / sizeof(seg6[0])), cx + 2, cy + 7, 2);
    add_icon_line(parent, seg7, (uint16_t)(sizeof(seg7) / sizeof(seg7[0])), cx - 11, cy - 6, 2);
    add_icon_line(parent, seg8, (uint16_t)(sizeof(seg8) / sizeof(seg8[0])), cx - 8, cy - 6, 2);
}

static const char *icon_name_for_action(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_NAV_MAIN:
        return "home";
    case V5_MAIN_PAGE_ACTION_NAV_PROBE:
        return "probe";
    case V5_MAIN_PAGE_ACTION_NAV_IO:
        return "bolt";
    case V5_MAIN_PAGE_ACTION_NAV_SETTINGS:
        return "gear";
    case V5_MAIN_PAGE_ACTION_NAV_NETWORK:
        return "wifi";
    case V5_MAIN_PAGE_ACTION_PAUSE:
        return "pause";
    case V5_MAIN_PAGE_ACTION_START:
        return "play";
    case V5_MAIN_PAGE_ACTION_ESTOP_FORCE:
        return "stop";
    case V5_MAIN_PAGE_ACTION_HOME:
        return "home_small";
    case V5_MAIN_PAGE_ACTION_JOG_PLUS:
        return "plus";
    case V5_MAIN_PAGE_ACTION_JOG_MINUS:
        return "minus";
    case V5_MAIN_PAGE_ACTION_NAV_PROGRAM:
        return "folder";
    case V5_MAIN_PAGE_ACTION_NAV_MDI:
        return "keyboard";
    default:
        return "";
    }
}

void v5_layout_add_button_icon(lv_obj_t *button, V5MainPageActionKind action, int w, int h, int right_nav)
{
    const char *name = icon_name_for_action(action);
    const lv_img_dsc_t *icon;
    int cx = right_nav ? 22 : 18;
    int cy = h / 2;
    lv_obj_t *img;
    if (action == V5_MAIN_PAGE_ACTION_NAV_TOOL) {
        add_mill_icon(button, cx, cy);
        return;
    }
    if (action == V5_MAIN_PAGE_ACTION_NAV_OFFSET) {
        add_offset_icon(button, cx, cy);
        return;
    }
    if (!name || !name[0]) {
        return;
    }
    icon = v5_layout_icon_mask_for_name(name);
    if (!icon) {
        return;
    }
    img = lv_img_create(button);
    lv_img_set_src(img, icon);
    lv_obj_set_pos(img, cx - (int)icon->header.w / 2, cy - (int)icon->header.h / 2);
    lv_obj_set_size(img, (int)icon->header.w, (int)icon->header.h);
    lv_obj_set_style_bg_opa(img, LV_OPA_TRANSP, 0);
    lv_obj_set_style_img_recolor(img, rgb(238, 245, 248), 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    (void)w;
}
