#include "v5_ui_first_frame_guard.h"
#include "v5_lvgl_remote_display.h"

#if defined(__GNUC__)
__attribute__((weak)) int v5_lvgl_remote_display_cache_capture(unsigned int slot)
{
    (void)slot;
    return 0;
}

__attribute__((weak)) int v5_lvgl_remote_display_cache_blit(unsigned int slot)
{
    (void)slot;
    return 0;
}
#endif

void v5_ui_first_frame_guard_begin(V5UiFirstFrameGuard *guard, unsigned int slot)
{
    if (!guard) {
        return;
    }
    guard->slot = slot;
    guard->captured = v5_lvgl_remote_display_cache_capture(slot);
}

void v5_ui_first_frame_guard_clear(V5UiFirstFrameGuard *guard)
{
    if (!guard) {
        return;
    }
    guard->captured = 0;
}

void v5_ui_first_frame_guard_restore(V5UiFirstFrameGuard *guard)
{
    if (!guard) {
        return;
    }
    if (guard->captured) {
        (void)v5_lvgl_remote_display_cache_blit(guard->slot);
    }
    guard->captured = 0;
}

void v5_ui_first_frame_guard_restore_dirty(V5UiFirstFrameGuard *guard, lv_obj_t *dirty_obj)
{
    v5_ui_first_frame_guard_restore(guard);
    if (dirty_obj) {
        lv_obj_invalidate(dirty_obj);
    }
}
