#ifndef V5_LVGL_REMOTE_DISPLAY_H
#define V5_LVGL_REMOTE_DISPLAY_H

#include <stddef.h>

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
    V5_REMOTE_DISPLAY_CACHE_TOOL = 2,
    V5_REMOTE_DISPLAY_CACHE_PROBE = 3,
    V5_REMOTE_DISPLAY_CACHE_OFFSET = 4,
    V5_REMOTE_DISPLAY_CACHE_IO = 5,
    V5_REMOTE_DISPLAY_CACHE_NETWORK = 6,
    V5_REMOTE_DISPLAY_CACHE_PROGRAM = 7,
    V5_REMOTE_DISPLAY_CACHE_MDI = 8,
    V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT = 9,
    V5_REMOTE_DISPLAY_CACHE_OVERLAY_0 = 9,
    V5_REMOTE_DISPLAY_CACHE_OVERLAY_1 = 10,
    V5_REMOTE_DISPLAY_CACHE_OVERLAY_2 = 11,
    V5_REMOTE_DISPLAY_CACHE_OVERLAY_3 = 12,
    V5_REMOTE_DISPLAY_CACHE_OVERLAY_BASE = V5_REMOTE_DISPLAY_CACHE_OVERLAY_0,
    V5_REMOTE_DISPLAY_CACHE_OVERLAY_COUNT = 4,
    V5_REMOTE_DISPLAY_CACHE_COUNT = 13
};

int v5_lvgl_remote_display_setup(unsigned int width, unsigned int height);
int v5_lvgl_remote_display_claim_physical_framebuffer(void);
void v5_lvgl_remote_display_render_now(void);
int v5_lvgl_remote_display_cache_capture(unsigned int slot);
int v5_lvgl_remote_display_cache_blit(unsigned int slot);
int v5_lvgl_remote_display_publish_current_frame(void);
int v5_lvgl_remote_display_blackout_for_restart(void);
int v5_lvgl_remote_display_cache_valid(unsigned int slot);
void v5_lvgl_remote_display_cache_invalidate(unsigned int slot);
size_t v5_lvgl_remote_display_cache_budget_bytes(void);
int v5_lvgl_remote_display_set_output_suppressed(int suppressed);
int v5_lvgl_remote_display_output_suppressed(void);
void v5_lvgl_remote_display_publish_cpu_metrics(
    double cpu0_percent,
    double cpu1_percent,
    unsigned long long sample_generation,
    unsigned long long sample_monotonic_ns);
int v5_remote_frame_snapshot(V5RemoteFrameSnapshot *snapshot);
int v5_remote_frame_ipc_pump(void);

#ifdef __cplusplus
}
#endif

#endif
