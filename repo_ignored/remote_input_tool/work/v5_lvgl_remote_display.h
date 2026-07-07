#ifndef V5_LVGL_REMOTE_DISPLAY_H
#define V5_LVGL_REMOTE_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5RemoteFrameSnapshot {
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    const unsigned char *pixels;
} V5RemoteFrameSnapshot;

int v5_lvgl_remote_display_setup(unsigned int width, unsigned int height);
void v5_lvgl_remote_display_render_now(void);
int v5_remote_frame_snapshot(V5RemoteFrameSnapshot *snapshot);
int v5_remote_frame_poll(unsigned short port, unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
