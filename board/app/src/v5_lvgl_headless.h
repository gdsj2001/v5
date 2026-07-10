#ifndef V5_LVGL_HEADLESS_H
#define V5_LVGL_HEADLESS_H

#ifdef __cplusplus
extern "C" {
#endif

int v5_lvgl_headless_display_setup(void);
unsigned int v5_lvgl_headless_flush_count(void);
void v5_lvgl_headless_reset_flush_count(void);

#ifdef __cplusplus
}
#endif

#endif
