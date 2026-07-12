#include "v5_ui_first_frame_guard.h"
#include "v5_lvgl_remote_display.h"

#include <stdint.h>
#include <string.h>

static unsigned int g_overlay_depth;
static V5UiFirstFrameGuard *g_overlay_guards[V5_REMOTE_DISPLAY_CACHE_OVERLAY_COUNT];
static lv_obj_t *g_safety_button;
static lv_obj_t *g_safety_layer;
static uint32_t g_input_suppression_started_at;
static int g_input_suppression_armed;

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

__attribute__((weak)) int v5_lvgl_remote_display_publish_current_frame(void)
{
    return 0;
}

__attribute__((weak)) int v5_lvgl_remote_display_set_output_suppressed(int suppressed)
{
    (void)suppressed;
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
    guard->stacked = 0;
    guard->safety_button = NULL;
    guard->safety_parent = NULL;
    guard->deferred_dirty_count = 0U;
}

int v5_ui_first_frame_guard_begin_overlay(V5UiFirstFrameGuard *guard)
{
    unsigned int slot;
    if (!guard) {
        return 0;
    }
    guard->captured = 0;
    guard->stacked = 0;
    guard->safety_button = NULL;
    guard->safety_parent = NULL;
    guard->deferred_dirty_count = 0U;
    if (g_overlay_depth >= V5_REMOTE_DISPLAY_CACHE_OVERLAY_COUNT) {
        return 0;
    }
    slot = V5_REMOTE_DISPLAY_CACHE_OVERLAY_BASE + g_overlay_depth;
    if (!v5_lvgl_remote_display_cache_capture(slot)) {
        return 0;
    }
    guard->slot = slot;
    guard->captured = 1;
    guard->stacked = 1;
    g_overlay_guards[g_overlay_depth] = guard;
    ++g_overlay_depth;
    return 1;
}

void v5_ui_first_frame_guard_register_safety_button(lv_obj_t *button)
{
    lv_obj_t *screen;
    g_safety_button = button;
    if (!button || g_safety_layer) {
        return;
    }
    screen = lv_obj_get_screen(button);
    if (!screen) {
        return;
    }
    g_safety_layer = lv_obj_create(screen);
    lv_obj_remove_style_all(g_safety_layer);
    lv_obj_set_pos(g_safety_layer, 0, 0);
    lv_obj_set_size(g_safety_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(g_safety_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(g_safety_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_safety_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_safety_layer, LV_OBJ_FLAG_HIDDEN);
}

void v5_ui_first_frame_guard_raise_safety(V5UiFirstFrameGuard *guard, lv_obj_t *overlay)
{
    if (!guard || !overlay || !g_safety_button || !g_safety_layer || guard->safety_button) {
        return;
    }
    guard->safety_button = g_safety_button;
    guard->safety_parent = lv_obj_get_parent(g_safety_button);
    lv_obj_set_parent(g_safety_button, g_safety_layer);
    lv_obj_clear_flag(g_safety_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_safety_layer);
    lv_obj_move_foreground(g_safety_button);
}

static int overlay_guard_is_top(const V5UiFirstFrameGuard *guard)
{
    return guard && guard->captured && guard->stacked && g_overlay_depth > 0U &&
        guard->slot == V5_REMOTE_DISPLAY_CACHE_OVERLAY_BASE + g_overlay_depth - 1U;
}

int v5_ui_first_frame_guard_is_top_overlay(const V5UiFirstFrameGuard *guard)
{
    return overlay_guard_is_top(guard);
}

static void overlay_guard_restore_safety(V5UiFirstFrameGuard *guard, unsigned int remaining_depth)
{
    if (guard->safety_button && guard->safety_parent) {
        lv_obj_set_parent(guard->safety_button, guard->safety_parent);
        lv_obj_move_foreground(guard->safety_button);
    }
    if (remaining_depth == 0U && g_safety_layer) {
        lv_obj_add_flag(g_safety_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

int v5_ui_first_frame_guard_present_overlay(V5UiFirstFrameGuard *guard, lv_obj_t *overlay)
{
    int previous_suppressed;
    int published;
    if (!guard || !overlay || !overlay_guard_is_top(guard)) {
        return 0;
    }
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
    v5_ui_first_frame_guard_raise_safety(guard, overlay);
    previous_suppressed = v5_lvgl_remote_display_set_output_suppressed(1);
    lv_obj_update_layout(overlay);
    lv_obj_invalidate(overlay);
    lv_refr_now(lv_obj_get_disp(overlay));
    (void)v5_lvgl_remote_display_set_output_suppressed(previous_suppressed);
    if (previous_suppressed) {
        return 1;
    }
    published = v5_lvgl_remote_display_publish_current_frame();
    if (!published) {
        lv_disp_t *display = lv_obj_get_disp(overlay);
        uint16_t pending_count = display ? display->inv_p : 0U;
        lv_area_t pending_areas[LV_INV_BUF_SIZE];
        uint8_t pending_joined[LV_INV_BUF_SIZE];
        if (display) {
            memcpy(pending_areas, display->inv_areas, sizeof(pending_areas));
            memcpy(pending_joined, display->inv_area_joined, sizeof(pending_joined));
        }
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        --g_overlay_depth;
        g_overlay_guards[g_overlay_depth] = NULL;
        overlay_guard_restore_safety(guard, g_overlay_depth);
        if (display) {
            memcpy(display->inv_areas, pending_areas, sizeof(pending_areas));
            memcpy(display->inv_area_joined, pending_joined, sizeof(pending_joined));
            display->inv_p = pending_count;
        }
        v5_ui_first_frame_guard_clear(guard);
    }
    return published;
}

static void overlay_guard_flush_deferred(V5UiFirstFrameGuard *guard)
{
    unsigned int index;
    if (!guard) {
        return;
    }
    for (index = 0U; index < guard->deferred_dirty_count; ++index) {
        if (guard->deferred_dirty[index]) {
            lv_obj_invalidate(guard->deferred_dirty[index]);
        }
    }
    guard->deferred_dirty_count = 0U;
}

static void overlay_guard_arm_input_suppression(void)
{
    g_input_suppression_started_at = lv_tick_get();
    g_input_suppression_armed = 1;
}

int v5_ui_first_frame_guard_dismiss_overlay(V5UiFirstFrameGuard *guard, lv_obj_t *overlay)
{
    lv_disp_t *display;
    lv_area_t pending_areas[LV_INV_BUF_SIZE];
    uint8_t pending_joined[LV_INV_BUF_SIZE];
    uint16_t pending_count;
    unsigned int slot;
    if (!overlay_guard_is_top(guard) || !overlay) {
        return 0;
    }
    display = lv_obj_get_disp(overlay);
    if (!display) {
        return 0;
    }
    slot = guard->slot;
    pending_count = display->inv_p;
    memcpy(pending_areas, display->inv_areas, sizeof(pending_areas));
    memcpy(pending_joined, display->inv_area_joined, sizeof(pending_joined));
    if (!v5_lvgl_remote_display_cache_blit(slot)) {
        return 0;
    }
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    --g_overlay_depth;
    g_overlay_guards[g_overlay_depth] = NULL;
    overlay_guard_restore_safety(guard, g_overlay_depth);
    memcpy(display->inv_areas, pending_areas, sizeof(pending_areas));
    memcpy(display->inv_area_joined, pending_joined, sizeof(pending_joined));
    display->inv_p = pending_count;
    if (guard->safety_button) {
        lv_obj_invalidate(guard->safety_button);
    }
    v5_ui_first_frame_guard_clear(guard);
    if (g_overlay_depth > 0U) {
        overlay_guard_flush_deferred(g_overlay_guards[g_overlay_depth - 1U]);
    }
    overlay_guard_arm_input_suppression();
    return 1;
}

int v5_ui_first_frame_guard_overlay_active(void)
{
    return g_overlay_depth > 0U;
}

int v5_ui_first_frame_guard_input_suppressed(void)
{
    uint32_t elapsed;
    if (!g_input_suppression_armed) {
        return 0;
    }
    elapsed = (uint32_t)(lv_tick_get() - g_input_suppression_started_at);
    if (elapsed < V5_UI_POST_MODAL_INPUT_SUPPRESSION_MS) {
        return 1;
    }
    g_input_suppression_armed = 0;
    return 0;
}

static int operator_input_hits_safety(lv_coord_t x, lv_coord_t y)
{
    lv_area_t area;
    if (!g_safety_button || !lv_obj_is_visible(g_safety_button)) {
        return 0;
    }
    lv_obj_get_coords(g_safety_button, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

int v5_ui_first_frame_guard_operator_input_allowed_at(lv_coord_t x, lv_coord_t y)
{
    return !v5_ui_first_frame_guard_input_suppressed() || operator_input_hits_safety(x, y);
}

void v5_ui_first_frame_guard_input_sequence_reset(V5UiOperatorInputSequence *sequence)
{
    if (sequence) {
        memset(sequence, 0, sizeof(*sequence));
    }
}

int v5_ui_first_frame_guard_input_sequence_begin(
    V5UiOperatorInputSequence *sequence,
    lv_coord_t x,
    lv_coord_t y)
{
    if (!sequence) {
        return 0;
    }
    sequence->active = 1;
    sequence->safety = operator_input_hits_safety(x, y);
    sequence->blocked = v5_ui_first_frame_guard_input_suppressed() && !sequence->safety;
    return !sequence->blocked;
}

int v5_ui_first_frame_guard_input_sequence_continue(V5UiOperatorInputSequence *sequence)
{
    if (!sequence || !sequence->active) {
        return !v5_ui_first_frame_guard_input_suppressed();
    }
    if (sequence->blocked) {
        return 0;
    }
    if (!sequence->safety && v5_ui_first_frame_guard_input_suppressed()) {
        sequence->blocked = 1;
        return 0;
    }
    return 1;
}

int v5_ui_first_frame_guard_input_sequence_end(V5UiOperatorInputSequence *sequence)
{
    int allowed;
    if (!sequence) {
        return 0;
    }
    allowed = v5_ui_first_frame_guard_input_sequence_continue(sequence);
    v5_ui_first_frame_guard_input_sequence_reset(sequence);
    return allowed;
}

static int overlay_guard_begin_object_mutation(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *obj,
    lv_disp_t **display_out)
{
    lv_disp_t *display;
    int covered;
    if (display_out) {
        *display_out = NULL;
    }
    if (!guard || !obj || !guard->captured || !guard->stacked) {
        return 0;
    }
    covered = !overlay_guard_is_top(guard);
    if (!covered) {
        return 0;
    }
    display = lv_obj_get_disp(obj);
    if (!display) {
        return 0;
    }
    lv_disp_enable_invalidation(display, false);
    if (display_out) {
        *display_out = display;
    }
    return 1;
}

static void overlay_guard_end_object_mutation(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *obj,
    lv_disp_t *display,
    int covered,
    int changed)
{
    if (covered && display) {
        lv_disp_enable_invalidation(display, true);
    }
    /* A visible/top object's LVGL setter already invalidates itself.  Only a
     * covered lower layer had invalidation suppressed and must be deferred. */
    if (covered && changed) {
        v5_ui_first_frame_guard_invalidate_or_defer(guard, obj);
    }
}

int v5_ui_first_frame_guard_set_label_text(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *label,
    const char *text)
{
    const char *current;
    const char *next = text ? text : "";
    lv_disp_t *display;
    int covered;
    if (!label) {
        return 0;
    }
    current = lv_label_get_text(label);
    if (current && strcmp(current, next) == 0) {
        return 0;
    }
    covered = overlay_guard_begin_object_mutation(guard, label, &display);
    lv_label_set_text(label, next);
    overlay_guard_end_object_mutation(guard, label, display, covered, 1);
    return 1;
}

int v5_ui_first_frame_guard_set_text_color(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *obj,
    lv_color_t color)
{
    lv_disp_t *display;
    int covered;
    if (!obj || lv_obj_get_style_text_color(obj, 0).full == color.full) {
        return 0;
    }
    covered = overlay_guard_begin_object_mutation(guard, obj, &display);
    lv_obj_set_style_text_color(obj, color, 0);
    overlay_guard_end_object_mutation(guard, obj, display, covered, 1);
    return 1;
}

int v5_ui_first_frame_guard_set_disabled(
    V5UiFirstFrameGuard *guard,
    lv_obj_t *obj,
    int disabled)
{
    int currently_disabled;
    lv_disp_t *display;
    int covered;
    if (!obj) {
        return 0;
    }
    currently_disabled = lv_obj_has_state(obj, LV_STATE_DISABLED);
    if ((disabled && currently_disabled) || (!disabled && !currently_disabled)) {
        return 0;
    }
    covered = overlay_guard_begin_object_mutation(guard, obj, &display);
    if (disabled) {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
    }
    overlay_guard_end_object_mutation(guard, obj, display, covered, 1);
    return 1;
}

void v5_ui_first_frame_guard_invalidate_or_defer(V5UiFirstFrameGuard *guard, lv_obj_t *obj)
{
    unsigned int index;
    if (!guard || !obj || !guard->captured || !guard->stacked) {
        return;
    }
    if (overlay_guard_is_top(guard)) {
        lv_obj_invalidate(obj);
        return;
    }
    for (index = 0U; index < guard->deferred_dirty_count; ++index) {
        if (guard->deferred_dirty[index] == obj) {
            return;
        }
    }
    if (guard->deferred_dirty_count < V5_UI_FIRST_FRAME_DEFERRED_DIRTY_MAX) {
        guard->deferred_dirty[guard->deferred_dirty_count++] = obj;
    }
}

void v5_ui_first_frame_guard_clear(V5UiFirstFrameGuard *guard)
{
    if (!guard) {
        return;
    }
    guard->captured = 0;
    guard->stacked = 0;
    guard->safety_button = NULL;
    guard->safety_parent = NULL;
    guard->deferred_dirty_count = 0U;
}

void v5_ui_first_frame_guard_restore(V5UiFirstFrameGuard *guard)
{
    if (!guard) {
        return;
    }
    if (guard->captured && guard->stacked && !overlay_guard_is_top(guard)) {
        return;
    }
    if (guard->captured && guard->stacked) {
        --g_overlay_depth;
        g_overlay_guards[g_overlay_depth] = NULL;
        overlay_guard_restore_safety(guard, g_overlay_depth);
        (void)v5_lvgl_remote_display_cache_blit(guard->slot);
    } else if (guard->captured) {
        (void)v5_lvgl_remote_display_cache_blit(guard->slot);
    }
    guard->captured = 0;
    guard->stacked = 0;
    guard->safety_button = NULL;
    guard->safety_parent = NULL;
}

void v5_ui_first_frame_guard_restore_dirty(V5UiFirstFrameGuard *guard, lv_obj_t *dirty_obj)
{
    v5_ui_first_frame_guard_restore(guard);
    if (dirty_obj) {
        lv_obj_invalidate(dirty_obj);
    }
}
