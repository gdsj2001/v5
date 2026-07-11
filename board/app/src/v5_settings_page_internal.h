#ifndef V5_SETTINGS_PAGE_INTERNAL_H
#define V5_SETTINGS_PAGE_INTERNAL_H

#include "v5_settings_page.h"

lv_color_t v5_settings_page_rgb(uint8_t r, uint8_t g, uint8_t b);
double v5_settings_page_monotonic_seconds(void);
void v5_settings_page_clear_obj_style(lv_obj_t *obj);
lv_obj_t *v5_settings_page_make_panel(
    lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
lv_obj_t *v5_settings_page_make_label(
    lv_obj_t *parent, const char *text, int x, int y, int w, int h,
    uint8_t r, uint8_t g, uint8_t b);
void v5_settings_page_refresh_machine_code_label(V5SettingsPage *page);
void v5_settings_page_set_status_text(
    V5SettingsPage *page, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...);
const char *v5_settings_page_status_action_label(const char *action);
void v5_settings_page_popup_show(
    V5SettingsPage *page, const char *action, const char *title,
    const char *message, int final, int ok);
void v5_settings_page_action_visual_clear(V5SettingsPage *page, int clear_binding);
void v5_settings_page_action_visual_bind(
    V5SettingsPage *page, lv_obj_t *button, const char *action);
void v5_settings_page_popup_create(V5SettingsPage *page);
void v5_settings_page_status_timer_cb(lv_timer_t *timer);
void v5_settings_page_parameter_changed_cb(void *user_data);
void v5_settings_page_axis_zero_requested_cb(
    const char *axis,
    const char *driver_mode,
    const char *target_scope,
    const char *apply_mode,
    const char *slave_index,
    const char *home_offset,
    void *user_data);
lv_obj_t *v5_settings_page_make_button(
    V5SettingsPage *page, const char *text, int x, int y, int w, int h,
    uint8_t r, uint8_t g, uint8_t b, V5MainPageActionKind action);
void v5_settings_page_make_value_cell_colored(
    lv_obj_t *parent, const char *text, int x, int y, int w, int h,
    int muted, uint8_t tr, uint8_t tg, uint8_t tb);
lv_obj_t *v5_settings_page_make_motion_model_dropdown(
    V5SettingsPage *page, lv_obj_t *parent, int x, int y, int w, int h);
void v5_settings_page_axis_color(const char *axis, uint8_t *r, uint8_t *g, uint8_t *b);
void v5_settings_page_format_mcs_value(
    double value, int valid, char *out, size_t out_size);
void v5_settings_page_make_machine_coordinate_widget(
    V5SettingsPage *page, lv_obj_t *root);

#endif
