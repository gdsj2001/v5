#ifndef V5_LAYOUT_ICONS_H
#define V5_LAYOUT_ICONS_H

#include "lvgl.h"
#include "v5_main_page_actions.h"

#ifdef __cplusplus
extern "C" {
#endif

void v5_layout_add_button_icon(lv_obj_t *button, V5MainPageActionKind action, int w, int h, int right_nav);

#ifdef __cplusplus
}
#endif

#endif
