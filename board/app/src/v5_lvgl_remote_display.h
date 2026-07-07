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

/*
 * REQ-UI-FIRST-FRAME-CACHE: every full page, modal overlay, and keyboard-like
 * surface that can replace or cover the screen needs an explicit resident
 * first-frame cache slot or an opening-frame snapshot. Navigation and return
 * paths must blit/restore that frame first, then let LVGL repaint only the
 * dirtied page or cell. Never make the first visible frame wait for a full
 * normal LVGL redraw.
 */
enum {
    V5_REMOTE_DISPLAY_CACHE_MAIN = 0,
    V5_REMOTE_DISPLAY_CACHE_SETTINGS = 1,
    V5_REMOTE_DISPLAY_CACHE_KEYBOARD = 2,
    V5_REMOTE_DISPLAY_CACHE_POPUP = 3,
    V5_REMOTE_DISPLAY_CACHE_COUNT = 4
};

int v5_lvgl_remote_display_setup(unsigned int width, unsigned int height);
void v5_lvgl_remote_display_render_now(void);
int v5_lvgl_remote_display_cache_capture(unsigned int slot);
int v5_lvgl_remote_display_cache_blit(unsigned int slot);
int v5_remote_frame_snapshot(V5RemoteFrameSnapshot *snapshot);
int v5_remote_frame_poll(unsigned short port, unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
