#ifndef V5_LVGL_TOUCH_INPUT_H
#define V5_LVGL_TOUCH_INPUT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*V5LvglTouchPointsCallback)(const lv_point_t *points, int count, int pressed, int *changed, void *user_data);

int v5_lvgl_touch_input_setup(void);
void v5_lvgl_touch_input_set_points_callback(V5LvglTouchPointsCallback callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
