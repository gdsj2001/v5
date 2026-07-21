#ifndef V5_LVGL_REMOTE_DISPLAY_CAPTURE_H
#define V5_LVGL_REMOTE_DISPLAY_CAPTURE_H

typedef struct V5RemoteDirtyRect {
    int x1;
    int y1;
    int x2;
    int y2;
} V5RemoteDirtyRect;

int v5_lvgl_remote_display_capture_setup(
    unsigned int width,
    unsigned int height);
int v5_lvgl_remote_display_capture_claim(void);
int v5_lvgl_remote_display_capture_available(void);
int v5_lvgl_remote_display_capture_physical_ready(void);
void v5_lvgl_remote_display_capture_write_row(
    unsigned int x,
    unsigned int y,
    const unsigned char *bgra,
    unsigned int pixels);
void v5_lvgl_remote_display_capture_write_full(
    const unsigned char *frame,
    unsigned int width,
    unsigned int height);
void v5_lvgl_remote_display_capture_add_dirty_area(
    V5RemoteDirtyRect *rects,
    unsigned int *count,
    unsigned int capacity,
    int x1,
    int y1,
    int x2,
    int y2);

#endif
