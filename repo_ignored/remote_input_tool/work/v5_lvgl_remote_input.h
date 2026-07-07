#ifndef V5_LVGL_REMOTE_INPUT_H
#define V5_LVGL_REMOTE_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

int v5_lvgl_remote_input_setup(void);
int v5_lvgl_remote_input_layout_click(int x, int y);
int v5_lvgl_remote_input_layout_drag(int x1, int y1, int x2, int y2, int steps);
const char *v5_lvgl_remote_input_enabled_mode(void);

#ifdef __cplusplus
}
#endif

#endif
