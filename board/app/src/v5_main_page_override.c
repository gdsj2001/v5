#include "v5_main_page.h"
#include "v5_motion_model_registry.h"

#include <math.h>

#include "v5_main_page_internal.h"

#define V5_OVERRIDE_MIN_PERCENT 0
#define V5_OVERRIDE_MAX_PERCENT 200
#define V5_OVERRIDE_THROTTLE_MS 100U

static int override_is_spindle(const V5MainPage *page, const lv_obj_t *slider)
{
    return page && slider && slider == page->spindle_override_slider;
}

static int *override_drag_active(V5MainPage *page, int spindle)
{
    return spindle ? &page->spindle_override_drag_active : &page->feed_override_drag_active;
}

static uint32_t *override_last_send_tick(V5MainPage *page, int spindle)
{
    return spindle ? &page->spindle_override_last_send_tick : &page->feed_override_last_send_tick;
}

static void override_send(V5MainPage *page, int spindle, int percent)
{
    V5MainPageActionReport report;
    if (!page) {
        return;
    }
    (void)v5_main_page_trigger_override(page, spindle, percent, &report);
    *override_last_send_tick(page, spindle) = lv_tick_get();
}

static void override_slider_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_obj_t *slider = lv_event_get_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    int spindle;
    int percent;
    int *drag_active;
    uint32_t *last_send_tick;

    if (!page || !slider || page->override_syncing_from_status) {
        return;
    }
    spindle = override_is_spindle(page, slider);
    percent = (int)lv_slider_get_value(slider);
    drag_active = override_drag_active(page, spindle);
    last_send_tick = override_last_send_tick(page, spindle);

    if (code == LV_EVENT_PRESSED) {
        *drag_active = 1;
        override_send(page, spindle, percent);
        return;
    }
    if (code == LV_EVENT_VALUE_CHANGED && *drag_active) {
        if (lv_tick_elaps(*last_send_tick) >= V5_OVERRIDE_THROTTLE_MS) {
            override_send(page, spindle, percent);
        }
        return;
    }
    if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CANCEL) &&
        *drag_active) {
        override_send(page, spindle, percent);
        *drag_active = 0;
    }
}

lv_obj_t *v5_main_page_internal_create_override_slider(
    V5MainPage *page,
    int spindle,
    int x,
    int y,
    int width)
{
    lv_obj_t *slider;
    if (!page || !page->root || width <= 0) {
        return 0;
    }
    slider = lv_slider_create(page->root);
    lv_obj_set_pos(slider, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_size(slider, (lv_coord_t)width, 7);
    lv_slider_set_range(slider, V5_OVERRIDE_MIN_PERCENT, V5_OVERRIDE_MAX_PERCENT);
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    lv_obj_set_style_radius(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, v5_main_page_internal_rgb(13, 42, 59), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, v5_main_page_internal_rgb(28, 193, 238), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, v5_main_page_internal_rgb(38, 180, 230), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, override_slider_event_cb, LV_EVENT_ALL, page);
    if (spindle) {
        page->spindle_override_slider = slider;
    } else {
        page->feed_override_slider = slider;
    }
    return slider;
}

static void sync_override_slider(
    V5MainPage *page,
    lv_obj_t *slider,
    int valid,
    double actual,
    int drag_active)
{
    int percent;
    if (!page || !slider) {
        return;
    }
    if (!valid || !isfinite(actual)) {
        if (!lv_obj_has_state(slider, LV_STATE_DISABLED)) {
            lv_obj_add_state(slider, LV_STATE_DISABLED);
        }
        return;
    }
    if (lv_obj_has_state(slider, LV_STATE_DISABLED)) {
        lv_obj_clear_state(slider, LV_STATE_DISABLED);
    }
    if (drag_active) {
        return;
    }
    if (actual < (double)V5_OVERRIDE_MIN_PERCENT) {
        actual = (double)V5_OVERRIDE_MIN_PERCENT;
    } else if (actual > (double)V5_OVERRIDE_MAX_PERCENT) {
        actual = (double)V5_OVERRIDE_MAX_PERCENT;
    }
    percent = (int)lround(actual);
    page->override_syncing_from_status = 1;
    if ((int)lv_slider_get_value(slider) != percent) {
        lv_slider_set_value(slider, percent, LV_ANIM_OFF);
    }
    page->override_syncing_from_status = 0;
}

void v5_main_page_internal_sync_override_sliders(
    V5MainPage *page,
    const V5UiStatusView *status)
{
    if (!page || !status) {
        return;
    }
    sync_override_slider(
        page,
        page->spindle_override_slider,
        (status->valid_mask & V5_STATUS_VALID_SPINDLE_OVERRIDE) != 0U,
        status->spindle_override,
        page->spindle_override_drag_active);
    sync_override_slider(
        page,
        page->feed_override_slider,
        (status->valid_mask & V5_STATUS_VALID_FEED_OVERRIDE) != 0U,
        status->feedrate_override,
        page->feed_override_drag_active);
}
