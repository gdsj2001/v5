#ifndef V5_UI_FIRST_FRAME_GUARD_H
#define V5_UI_FIRST_FRAME_GUARD_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_UI_FIRST_FRAME_DEFERRED_DIRTY_MAX 8U
#define V5_UI_POST_MODAL_INPUT_SUPPRESSION_MS 200U

/*
 * REQ-UI-FIRST-FRAME-CACHE: new page transitions, full-screen popups,
 * and keyboard-like overlays should use this guard instead of open-coding
 * per-widget cache capture/blit paths. Capture before covering the screen,
 * restore first on close, then invalidate only the object or page that changed.
 */
typedef struct V5UiFirstFrameGuard {
    unsigned int slot;
    int captured;
    int stacked;
    lv_obj_t *safety_button;
    lv_obj_t *safety_parent;
    lv_obj_t *deferred_dirty[V5_UI_FIRST_FRAME_DEFERRED_DIRTY_MAX];
    unsigned int deferred_dirty_count;
} V5UiFirstFrameGuard;

typedef struct V5UiOperatorInputSequence {
    int active;
    int blocked;
    int safety;
} V5UiOperatorInputSequence;

void v5_ui_first_frame_guard_begin(V5UiFirstFrameGuard *guard, unsigned int slot);
int v5_ui_first_frame_guard_begin_overlay(V5UiFirstFrameGuard *guard);
void v5_ui_first_frame_guard_register_safety_button(lv_obj_t *button);
void v5_ui_first_frame_guard_raise_safety(V5UiFirstFrameGuard *guard, lv_obj_t *overlay);
int v5_ui_first_frame_guard_present_overlay(V5UiFirstFrameGuard *guard, lv_obj_t *overlay);
int v5_ui_first_frame_guard_dismiss_overlay(V5UiFirstFrameGuard *guard, lv_obj_t *overlay);
int v5_ui_first_frame_guard_overlay_active(void);
int v5_ui_first_frame_guard_input_suppressed(void);
int v5_ui_first_frame_guard_operator_input_allowed_at(lv_coord_t x, lv_coord_t y);
void v5_ui_first_frame_guard_input_sequence_reset(V5UiOperatorInputSequence *sequence);
int v5_ui_first_frame_guard_input_sequence_begin(
    V5UiOperatorInputSequence *sequence,
    lv_coord_t x,
    lv_coord_t y);
int v5_ui_first_frame_guard_input_sequence_continue(V5UiOperatorInputSequence *sequence);
int v5_ui_first_frame_guard_input_sequence_end(V5UiOperatorInputSequence *sequence);
int v5_ui_first_frame_guard_is_top_overlay(const V5UiFirstFrameGuard *guard);
int v5_ui_first_frame_guard_set_label_text(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *label,
    const char *text);
int v5_ui_first_frame_guard_set_text_color(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *obj,
    lv_color_t color);
int v5_ui_first_frame_guard_set_disabled(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *obj,
    int disabled);
void v5_ui_first_frame_guard_invalidate_or_defer(V5UiFirstFrameGuard *guard, lv_obj_t *obj);
void v5_ui_first_frame_guard_restore(V5UiFirstFrameGuard *guard);
void v5_ui_first_frame_guard_restore_dirty(V5UiFirstFrameGuard *guard, lv_obj_t *dirty_obj);
void v5_ui_first_frame_guard_clear(V5UiFirstFrameGuard *guard);

#ifdef __cplusplus
}
#endif

#endif
