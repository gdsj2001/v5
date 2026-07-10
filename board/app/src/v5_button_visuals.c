#include "v5_button_visuals.h"

static lv_color_t color(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

void v5_button_visual_release_now(lv_obj_t *button)
{
    if (!button) {
        return;
    }
    lv_obj_clear_state(button, LV_STATE_PRESSED);
    lv_obj_invalidate(button);
    lv_refr_now(NULL);
}

static void button_visual_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST ||
        code == LV_EVENT_CANCEL || code == LV_EVENT_CLICKED) {
        v5_button_visual_release_now(lv_event_get_target(event));
    }
}

void v5_button_visual_bind(lv_obj_t *button)
{
    if (!button) {
        return;
    }
    lv_obj_set_style_bg_color(button, color(29, 151, 104), LV_STATE_USER_1);
    lv_obj_set_style_border_color(button, color(88, 204, 255), LV_STATE_USER_1);
    lv_obj_set_style_bg_color(button, color(245, 214, 82), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, color(255, 232, 120), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, button_visual_event_cb, LV_EVENT_ALL, NULL);
}

void v5_button_visual_set_transaction_active(lv_obj_t *button, int active)
{
    if (!button) {
        return;
    }
    lv_obj_clear_state(button, LV_STATE_PRESSED);
    if (active) {
        lv_obj_add_state(button, LV_STATE_USER_1);
    } else {
        lv_obj_clear_state(button, LV_STATE_USER_1);
    }
    lv_obj_invalidate(button);
    lv_refr_now(NULL);
}
