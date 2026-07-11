#ifndef V5_V3_PAGE_WIDGETS_H
#define V5_V3_PAGE_WIDGETS_H

#include "lvgl.h"

#include <stdint.h>

lv_color_t v5_v3_page_color(uint8_t r, uint8_t g, uint8_t b);
void v5_v3_page_clear_obj(lv_obj_t *obj);
lv_obj_t *v5_v3_page_panel(lv_obj_t *parent, int x, int y, int w, int h,
                           uint8_t r, uint8_t g, uint8_t b);
lv_obj_t *v5_v3_page_label(lv_obj_t *parent, const char *text, int x, int y,
                           int w, int h, uint8_t r, uint8_t g, uint8_t b,
                           lv_text_align_t align);
lv_obj_t *v5_v3_page_button(lv_obj_t *parent, const char *text, int x, int y,
                            int w, int h, uint8_t r, uint8_t g, uint8_t b,
                            lv_event_cb_t cb, void *user);
lv_obj_t *v5_v3_page_cell(lv_obj_t *parent, const char *text, int x, int y,
                          int w, int h, int header, int selected,
                          uint8_t tr, uint8_t tg, uint8_t tb);
lv_obj_t *v5_v3_page_cell_value_label(lv_obj_t *parent, const char *text,
                                      int x, int y, int w, int h,
                                      uint8_t tr, uint8_t tg, uint8_t tb);
void v5_v3_page_divider(lv_obj_t *parent, int x, int y, int w, int h);

#endif
