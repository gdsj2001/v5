#ifndef V5_MAIN_PAGE_H
#define V5_MAIN_PAGE_H

#include "lvgl.h"
#include "v5_coordinate_panel.h"
#include "v5_command_program.h"
#include "v5_main_page_actions.h"
#include "v5_toolpath_display.h"
#include "v5_ui_status_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_MAIN_PAGE_AXIS_COUNT V5_COORDINATE_AXIS_COUNT
#define V5_MAIN_PAGE_TRAJECTORY_POINT_COUNT V5_STATUS_TRAJECTORY_POINT_COUNT
#define V5_MAIN_PAGE_BUTTON_COUNT 13u

typedef struct V5MainPage {
    lv_obj_t *root;
    lv_obj_t *axis_labels[V5_MAIN_PAGE_AXIS_COUNT];
    lv_obj_t *mcs_labels[V5_MAIN_PAGE_AXIS_COUNT];
    lv_obj_t *cmd_labels[V5_MAIN_PAGE_AXIS_COUNT];
    lv_obj_t *error_labels[V5_MAIN_PAGE_AXIS_COUNT];
    lv_obj_t *modal_label;
    lv_obj_t *spindle_speed_label;
    lv_obj_t *linear_velocity_label;
    lv_obj_t *feed_override_label;
    lv_obj_t *spindle_override_label;
    lv_obj_t *trajectory_line;
    lv_obj_t *mcs_marker_label;
    lv_obj_t *cmd_marker_label;
    lv_obj_t *buttons[V5_MAIN_PAGE_BUTTON_COUNT];
    V5MainPageActionKind button_actions[V5_MAIN_PAGE_BUTTON_COUNT];
    unsigned int button_count;
    V5MainPageActionReport last_action;
    V5ProgramController *program_controller;
    V5ProgramOpenResult last_program_open;
    lv_point_t trajectory_points[V5_MAIN_PAGE_TRAJECTORY_POINT_COUNT];
    unsigned int trajectory_point_count;
} V5MainPage;

void v5_main_page_init(V5MainPage *page);
int v5_main_page_create(V5MainPage *page, lv_obj_t *parent);
int v5_main_page_apply_status(V5MainPage *page, const V5UiStatusView *status);
void v5_main_page_bind_program_controller(V5MainPage *page, V5ProgramController *controller);
int v5_main_page_open_program(V5MainPage *page, const char *path, V5ProgramOpenResult *open_report);
int v5_main_page_trigger_action(V5MainPage *page, V5MainPageActionKind action, V5MainPageActionReport *report);

#ifdef __cplusplus
}
#endif

#endif
