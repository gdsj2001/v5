#ifndef V5_UI_FIRST_FRAME_GUARD_H
#define V5_UI_FIRST_FRAME_GUARD_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * REQ-UI-FIRST-FRAME-CACHE: new page transitions, full-screen popups,
 * and keyboard-like overlays should use this guard instead of open-coding
 * per-widget cache capture/blit paths. Capture before covering the screen,
 * restore first on close, then invalidate only the object or page that changed.
 */
typedef struct V5UiFirstFrameGuard {
    unsigned int slot;
    int captured;
} V5UiFirstFrameGuard;

void v5_ui_first_frame_guard_begin(V5UiFirstFrameGuard *guard, unsigned int slot);
void v5_ui_first_frame_guard_restore(V5UiFirstFrameGuard *guard);
void v5_ui_first_frame_guard_restore_dirty(V5UiFirstFrameGuard *guard, lv_obj_t *dirty_obj);
void v5_ui_first_frame_guard_clear(V5UiFirstFrameGuard *guard);

#ifdef __cplusplus
}
#endif

#endif
