#ifndef V5_LVGL_REMOTE_INPUT_H
#define V5_LVGL_REMOTE_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

int v5_lvgl_remote_input_setup(void);
int v5_lvgl_remote_input_accepts_pointer(void);
int v5_lvgl_remote_input_pointer_event(const char *phase, int x, int y);
const char *v5_lvgl_remote_input_enabled_mode(void);

#ifdef __cplusplus
}
#endif

#endif
