#ifndef V5_SETTINGS_PAGE_H
#define V5_SETTINGS_PAGE_H

#include "lvgl.h"
#include "v5_main_page.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_boot_closure.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_SETTINGS_PAGE_BUTTON_COUNT 9u

typedef struct V5SettingsPage {
    lv_obj_t *root;
    lv_obj_t *buttons[V5_SETTINGS_PAGE_BUTTON_COUNT];
    V5MainPageActionKind button_actions[V5_SETTINGS_PAGE_BUTTON_COUNT];
    unsigned int button_count;
    lv_obj_t *status_label;
    lv_obj_t *machine_code_label;
    lv_obj_t *motion_model_dropdown;
    lv_obj_t *mcs_labels[V5_MAIN_PAGE_AXIS_COUNT];
    V5CoordinateDigits mcs_digits;
    lv_color_t mcs_digits_buffer[V5_COORD_DIGITS_SETTINGS_W * V5_COORD_DIGITS_SETTINGS_H];
    lv_timer_t *status_timer;
    lv_obj_t *popup_overlay;
    lv_obj_t *popup_title;
    lv_obj_t *popup_message;
    lv_obj_t *popup_eta;
    lv_obj_t *popup_close;
    V5UiFirstFrameGuard popup_frame_guard;
    int popup_active;
    int popup_final;
    double popup_started_s;
    int popup_eta_seconds;
    char popup_action[64];
    char popup_run_id[64];
    int popup_cancel_pending;
    V5MainPageActionReport last_action;
    char last_axis_table_refresh_run_id[64];
    V5UiNavigationCallback navigation_cb;
    void *navigation_user_data;
} V5SettingsPage;

void v5_settings_page_init(V5SettingsPage *page);
void v5_settings_page_set_boot_closure(const V5BootClosure *closure);
int v5_settings_page_create(V5SettingsPage *page, lv_obj_t *parent);
int v5_settings_page_apply_status(V5SettingsPage *page, const V5UiStatusView *status);
void v5_settings_page_set_navigation_callback(V5SettingsPage *page, V5UiNavigationCallback cb, void *user_data);
int v5_settings_page_trigger_action(V5SettingsPage *page, V5MainPageActionKind action, V5MainPageActionReport *report);

#ifdef __cplusplus
}
#endif

#endif
