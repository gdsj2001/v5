#ifndef V5_POPUP_LAYOUT_H
#define V5_POPUP_LAYOUT_H

#include "lvgl.h"

#define V5_POPUP_MASK_X 0
#define V5_POPUP_MASK_Y 0
#define V5_POPUP_MASK_W 1024
#define V5_POPUP_MASK_H 600
#define V5_POPUP_PANEL_X 170
#define V5_POPUP_PANEL_Y 88
#define V5_POPUP_PANEL_W 684
#define V5_POPUP_PANEL_H 424
#define V5_POPUP_TITLE_X 24
#define V5_POPUP_TITLE_Y 22
#define V5_POPUP_TITLE_W 636
#define V5_POPUP_TITLE_H 36
#define V5_POPUP_MESSAGE_X 46
#define V5_POPUP_MESSAGE_Y 80
#define V5_POPUP_MESSAGE_W 592
#define V5_POPUP_MESSAGE_H 236
#define V5_POPUP_STATUS_X 46
#define V5_POPUP_STATUS_Y 328
#define V5_POPUP_STATUS_W 220
#define V5_POPUP_STATUS_H 24
#define V5_POPUP_CONFIRM_X 352
#define V5_POPUP_CONFIRM_Y 346
#define V5_POPUP_CONFIRM_W 132
#define V5_POPUP_CONFIRM_H 48
#define V5_POPUP_CLOSE_X 500
#define V5_POPUP_CLOSE_Y 346
#define V5_POPUP_CLOSE_W 132
#define V5_POPUP_CLOSE_H 48

#define V5_POPUP_TITLE_COLOR_RGB 0x58CCFFU
#define V5_POPUP_MESSAGE_COLOR_RGB 0xE2EEF6U
#define V5_POPUP_STATUS_COLOR_RGB 0xF5D652U

typedef struct V5PopupLayoutConfig {
    const char *title;
    const char *message;
    const char *status;
    unsigned int title_color_rgb;
    unsigned int message_color_rgb;
    unsigned int status_color_rgb;
    const char *confirm_text;
    int confirm_enabled;
    lv_event_cb_t confirm_cb;
    void *confirm_user_data;
    const char *close_text;
    int close_enabled;
    lv_event_cb_t close_cb;
    void *close_user_data;
} V5PopupLayoutConfig;

typedef struct V5PopupLayoutObjects {
    lv_obj_t *overlay;
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *message;
    lv_obj_t *status;
    lv_obj_t *confirm;
    lv_obj_t *close;
} V5PopupLayoutObjects;

static inline lv_color_t v5_popup_layout_rgb(unsigned int rgb)
{
    return lv_color_make(
        (uint8_t)((rgb >> 16U) & 0xffU),
        (uint8_t)((rgb >> 8U) & 0xffU),
        (uint8_t)(rgb & 0xffU));
}

static inline lv_obj_t *v5_popup_layout_create_overlay(lv_obj_t *parent)
{
    lv_obj_t *overlay;
    if (!parent) {
        return NULL;
    }
    overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, V5_POPUP_MASK_X, V5_POPUP_MASK_Y);
    lv_obj_set_size(overlay, V5_POPUP_MASK_W, V5_POPUP_MASK_H);
    lv_obj_set_style_bg_color(overlay, lv_color_make(3, 16, 26), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    return overlay;
}

static inline lv_obj_t *v5_popup_layout_create_panel(lv_obj_t *overlay)
{
    lv_obj_t *panel;
    if (!overlay) {
        return NULL;
    }
    panel = lv_obj_create(overlay);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, V5_POPUP_PANEL_X, V5_POPUP_PANEL_Y);
    lv_obj_set_size(panel, V5_POPUP_PANEL_W, V5_POPUP_PANEL_H);
    lv_obj_set_style_bg_color(panel, lv_color_make(7, 31, 48), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_make(76, 119, 146), 0);
    lv_obj_set_style_radius(panel, 7, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static inline lv_obj_t *v5_popup_layout_create_disabled_confirm_button(lv_obj_t *panel)
{
    lv_obj_t *button;
    lv_obj_t *label;
    if (!panel) {
        return NULL;
    }
    button = lv_btn_create(panel);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, V5_POPUP_CONFIRM_X, V5_POPUP_CONFIRM_Y);
    lv_obj_set_size(button, V5_POPUP_CONFIRM_W, V5_POPUP_CONFIRM_H);
    lv_obj_set_style_bg_color(button, lv_color_make(42, 86, 116), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_make(76, 119, 146), 0);
    lv_obj_set_style_opa(button, LV_OPA_60, LV_STATE_DISABLED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_state(button, LV_STATE_DISABLED);

    label = lv_label_create(button);
    lv_label_set_text(label, "确认继续");
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(label, 0, 11);
    lv_obj_set_size(label, V5_POPUP_CONFIRM_W, 26);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_make(238, 245, 248), 0);
    return button;
}

static inline lv_obj_t *v5_popup_layout_create_label(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    int height,
    const char *text,
    unsigned int color_rgb,
    lv_text_align_t align,
    lv_label_long_mode_t long_mode)
{
    lv_obj_t *label;
    if (!parent) {
        return NULL;
    }
    label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_color(label, v5_popup_layout_rgb(color_rgb), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, long_mode);
    lv_label_set_text(label, text ? text : "");
    return label;
}

static inline lv_obj_t *v5_popup_layout_create_button(
    lv_obj_t *panel,
    int x,
    int y,
    int width,
    int height,
    const char *text,
    int enabled,
    lv_event_cb_t callback,
    void *user_data)
{
    lv_obj_t *button;
    lv_obj_t *label;
    if (!panel) {
        return NULL;
    }
    button = lv_btn_create(panel);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_make(42, 86, 116), 0);
    lv_obj_set_style_bg_color(button, lv_color_make(29, 151, 104), LV_STATE_USER_1);
    lv_obj_set_style_bg_color(button, lv_color_make(245, 214, 82), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_make(76, 119, 146), 0);
    lv_obj_set_style_border_color(button, lv_color_make(88, 204, 255), LV_STATE_USER_1);
    lv_obj_set_style_border_color(button, lv_color_make(255, 232, 120), LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_60, LV_STATE_DISABLED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    if (!enabled) {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    if (callback) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_RELEASED, user_data);
    }

    label = lv_label_create(button);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(label, 0, 11);
    lv_obj_set_size(label, width, 26);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_make(238, 245, 248), 0);
    return button;
}

/*
 * Canonical product-popup constructor. Product pages provide only content,
 * button state and released-event handlers; geometry and widget styling stay
 * owned here so every full-screen popup has exactly the same object tree.
 */
static inline int v5_popup_layout_create(
    lv_obj_t *parent,
    const V5PopupLayoutConfig *config,
    V5PopupLayoutObjects *objects)
{
    unsigned int title_color;
    unsigned int message_color;
    unsigned int status_color;
    if (!parent || !config || !objects) {
        return 0;
    }
    objects->overlay = NULL;
    objects->panel = NULL;
    objects->title = NULL;
    objects->message = NULL;
    objects->status = NULL;
    objects->confirm = NULL;
    objects->close = NULL;
    title_color = config->title_color_rgb ? config->title_color_rgb : V5_POPUP_TITLE_COLOR_RGB;
    message_color = config->message_color_rgb ? config->message_color_rgb : V5_POPUP_MESSAGE_COLOR_RGB;
    status_color = config->status_color_rgb ? config->status_color_rgb : V5_POPUP_STATUS_COLOR_RGB;

    objects->overlay = v5_popup_layout_create_overlay(parent);
    objects->panel = v5_popup_layout_create_panel(objects->overlay);
    objects->title = v5_popup_layout_create_label(
        objects->panel,
        V5_POPUP_TITLE_X, V5_POPUP_TITLE_Y, V5_POPUP_TITLE_W, V5_POPUP_TITLE_H,
        config->title, title_color, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_CLIP);
    objects->message = v5_popup_layout_create_label(
        objects->panel,
        V5_POPUP_MESSAGE_X, V5_POPUP_MESSAGE_Y,
        V5_POPUP_MESSAGE_W, V5_POPUP_MESSAGE_H,
        config->message, message_color, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_WRAP);
    objects->status = v5_popup_layout_create_label(
        objects->panel,
        V5_POPUP_STATUS_X, V5_POPUP_STATUS_Y,
        V5_POPUP_STATUS_W, V5_POPUP_STATUS_H,
        config->status, status_color, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_CLIP);
    objects->confirm = v5_popup_layout_create_button(
        objects->panel,
        V5_POPUP_CONFIRM_X, V5_POPUP_CONFIRM_Y,
        V5_POPUP_CONFIRM_W, V5_POPUP_CONFIRM_H,
        config->confirm_text ? config->confirm_text : "确认继续",
        config->confirm_enabled,
        config->confirm_cb,
        config->confirm_user_data);
    objects->close = v5_popup_layout_create_button(
        objects->panel,
        V5_POPUP_CLOSE_X, V5_POPUP_CLOSE_Y,
        V5_POPUP_CLOSE_W, V5_POPUP_CLOSE_H,
        config->close_text ? config->close_text : "关闭",
        config->close_enabled,
        config->close_cb,
        config->close_user_data);
    if (!objects->overlay || !objects->panel || !objects->title ||
        !objects->message || !objects->status || !objects->confirm || !objects->close) {
        return 0;
    }
    lv_obj_add_flag(objects->overlay, LV_OBJ_FLAG_HIDDEN);
    return 1;
}

#endif
