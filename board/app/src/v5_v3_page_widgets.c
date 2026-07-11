#include "v5_v3_page_widgets.h"
#include "v5_button_visuals.h"

lv_color_t v5_v3_page_color(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}
void v5_v3_page_clear_obj(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}
lv_obj_t *v5_v3_page_panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *obj = lv_obj_create(parent);
    v5_v3_page_clear_obj(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, v5_v3_page_color(r, g, b), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, v5_v3_page_color(86, 96, 100), 0);
    return obj;
}

lv_obj_t *v5_v3_page_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, lv_text_align_t align)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text ? text : "--");
    lv_obj_set_style_text_color(obj, v5_v3_page_color(r, g, b), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    return obj;
}

lv_obj_t *v5_v3_page_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, lv_event_cb_t cb, void *user)
{
    lv_obj_t *obj = lv_btn_create(parent);
    lv_obj_t *txt;
    v5_v3_page_clear_obj(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, v5_v3_page_color(r, g, b), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, v5_v3_page_color(76, 119, 146), 0);
    v5_button_visual_bind(obj);
    if (cb) {
        lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
    }
    txt = v5_v3_page_label(obj, text, 0, (h - 24) / 2, w, 24,
                (r + g + b > 540) ? 16 : 238,
                (r + g + b > 540) ? 20 : 245,
                (r + g + b > 540) ? 24 : 248,
                LV_TEXT_ALIGN_CENTER);
    (void)txt;
    return obj;
}

lv_obj_t *v5_v3_page_cell(lv_obj_t *parent, const char *text, int x, int y, int w, int h, int header, int selected, uint8_t tr, uint8_t tg, uint8_t tb)
{
    lv_obj_t *obj = v5_v3_page_panel(parent, x, y, w, h,
                          selected ? 255 : (header ? 39 : 8),
                          selected ? 210 : (header ? 45 : 26),
                          selected ? 0 : (header ? 48 : 36));
    v5_v3_page_label(obj, text, 0, (h - 22) / 2, w, 24,
          selected ? 16 : tr, selected ? 20 : tg, selected ? 24 : tb,
          LV_TEXT_ALIGN_CENTER);
    return obj;
}

lv_obj_t *v5_v3_page_cell_value_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t tr, uint8_t tg, uint8_t tb)
{
    lv_obj_t *obj = v5_v3_page_panel(parent, x, y, w, h, 8, 26, 36);
    return v5_v3_page_label(obj, text, 0, (h - 22) / 2, w, 24, tr, tg, tb, LV_TEXT_ALIGN_CENTER);
}

void v5_v3_page_divider(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *line = v5_v3_page_panel(parent, x, y, w, h, 78, 92, 100);
    lv_obj_set_style_border_width(line, 0, 0);
}
