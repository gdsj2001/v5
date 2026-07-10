#ifndef V5_BUTTON_VISUALS_H
#define V5_BUTTON_VISUALS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void v5_button_visual_bind(lv_obj_t *button);
void v5_button_visual_release_now(lv_obj_t *button);
void v5_button_visual_set_transaction_active(lv_obj_t *button, int active);

#ifdef __cplusplus
}
#endif

#endif
