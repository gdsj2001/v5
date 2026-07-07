#ifndef V5_SETTINGS_PAGE_H
#define V5_SETTINGS_PAGE_H

#include "lvgl.h"
#include "v5_main_page.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_SETTINGS_PAGE_BUTTON_COUNT 9u

typedef struct V5SettingsPage {
    lv_obj_t *root;
    lv_obj_t *buttons[V5_SETTINGS_PAGE_BUTTON_COUNT];
    V5MainPageActionKind button_actions[V5_SETTINGS_PAGE_BUTTON_COUNT];
    unsigned int button_count;
    V5MainPageActionReport last_action;
    V5UiNavigationCallback navigation_cb;
    void *navigation_user_data;
} V5SettingsPage;

void v5_settings_page_init(V5SettingsPage *page);
int v5_settings_page_create(V5SettingsPage *page, lv_obj_t *parent);
void v5_settings_page_set_navigation_callback(V5SettingsPage *page, V5UiNavigationCallback cb, void *user_data);
int v5_settings_page_trigger_action(V5SettingsPage *page, V5MainPageActionKind action, V5MainPageActionReport *report);

#ifdef __cplusplus
}
#endif

#endif
