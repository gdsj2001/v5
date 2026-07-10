#ifndef V5_MAIN_PAGE_H
#define V5_MAIN_PAGE_H

#include "lvgl.h"
#include "v5_coordinate_digits.h"
#include "v5_coordinate_panel.h"
#include "v5_command_program.h"
#include "v5_main_page_actions.h"
#include "v5_native_readback.h"
#include "v5_toolpath_display.h"
#include "v5_ui_status_view.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_MAIN_PAGE_AXIS_COUNT V5_COORDINATE_AXIS_COUNT
#define V5_MAIN_PAGE_TRAJECTORY_POINT_COUNT V5_STATUS_TRAJECTORY_POINT_COUNT
#define V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT V5_PROGRAM_PREVIEW_POINT_COUNT
#define V5_MAIN_PAGE_BUTTON_COUNT 35u
#define V5_MAIN_PAGE_TOOLPATH_WCS_COUNT 9u
#define V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS 16u
#define V5_MAIN_PAGE_TOOLPATH_SEGMENT_POINT_COUNT 64u

enum {
    V5_MAIN_PAGE_REFRESH_DYNAMIC = 1u << 0,
    V5_MAIN_PAGE_REFRESH_BUTTONS = 1u << 1,
    V5_MAIN_PAGE_REFRESH_ESTOP = 1u << 2,
    V5_MAIN_PAGE_REFRESH_SLOW = 1u << 3,
    V5_MAIN_PAGE_REFRESH_ALL =
        V5_MAIN_PAGE_REFRESH_DYNAMIC |
        V5_MAIN_PAGE_REFRESH_BUTTONS |
        V5_MAIN_PAGE_REFRESH_ESTOP |
        V5_MAIN_PAGE_REFRESH_SLOW
};

typedef void (*V5UiNavigationCallback)(void *user_data, V5MainPageActionKind action);
typedef void (*V5MainPageNativeReadbackRefreshCallback)(void *user_data, V5MainPageActionKind action);

typedef struct V5MainPageCoordinateTarget {
    V5MainPageSelectionSpace space;
    char axis;
} V5MainPageCoordinateTarget;

typedef struct V5MainPage {
    lv_obj_t *root;
    lv_obj_t *axis_labels[V5_MAIN_PAGE_AXIS_COUNT];
    lv_obj_t *mcs_labels[V5_MAIN_PAGE_AXIS_COUNT];
    lv_obj_t *cmd_labels[V5_MAIN_PAGE_AXIS_COUNT];
    V5CoordinateDigits coordinate_digits;
    lv_color_t coordinate_digits_buffer[V5_COORD_DIGITS_MAIN_W * V5_COORD_DIGITS_MAIN_H];
    V5MainPageCoordinateTarget mcs_targets[V5_MAIN_PAGE_AXIS_COUNT];
    V5MainPageCoordinateTarget wcs_targets[V5_MAIN_PAGE_AXIS_COUNT];
    lv_obj_t *error_labels[V5_MAIN_PAGE_AXIS_COUNT];
    lv_obj_t *modal_label;
    lv_obj_t *wcs_header_label;
    lv_obj_t *spindle_speed_label;
    lv_obj_t *linear_velocity_label;
    lv_obj_t *feed_override_label;
    lv_obj_t *spindle_override_label;
    lv_obj_t *cpu0_label;
    lv_obj_t *cpu1_label;
    lv_obj_t *toolpath_clip_layer;
    lv_obj_t *trajectory_line;
    lv_obj_t *toolpath_line_segments[V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS];
    lv_obj_t *toolpath_mcs_origin_line;
    lv_obj_t *toolpath_wcs_origin_line;
    lv_obj_t *toolpath_mcs_axis_lines[3];
    lv_obj_t *toolpath_wcs_axis_lines[3];
    lv_obj_t *toolpath_a_axis_line;
    lv_obj_t *toolpath_c_axis_line;
    lv_obj_t *toolpath_program_wcs_origin_lines[V5_MAIN_PAGE_TOOLPATH_WCS_COUNT];
    lv_obj_t *toolpath_program_wcs_axis_lines[V5_MAIN_PAGE_TOOLPATH_WCS_COUNT][3];
    lv_obj_t *toolpath_holder_line;
    lv_obj_t *toolpath_a_center_line;
    lv_obj_t *toolpath_c_center_line;
    lv_obj_t *toolpath_summary_label;
    lv_obj_t *toolpath_detail_label;
    lv_obj_t *toolpath_view_label;
    lv_obj_t *toolpath_a_label;
    lv_obj_t *toolpath_c_label;
    lv_obj_t *toolpath_wcs_label;
    lv_obj_t *toolpath_mcs_label;
    lv_obj_t *toolpath_program_wcs_labels[V5_MAIN_PAGE_TOOLPATH_WCS_COUNT];
    lv_obj_t *toolpath_mcs_axis_labels[3];
    lv_obj_t *toolpath_microkernel_marker_dot;
    lv_obj_t *toolpath_holder_marker_line;
    lv_obj_t *program_name_label;
    lv_obj_t *program_line_bg[4];
    lv_obj_t *program_line_labels[4];
    lv_obj_t *program_edit_hit_area;
    lv_obj_t *buttons[V5_MAIN_PAGE_BUTTON_COUNT];
    lv_obj_t *button_labels[V5_MAIN_PAGE_BUTTON_COUNT];
    V5MainPageActionKind button_actions[V5_MAIN_PAGE_BUTTON_COUNT];
    unsigned int button_count;
    V5MainPageActionReport last_action;
    V5ProgramController *program_controller;
    V5NativeReadback native_readback;
    int command_execution_enabled;
    int home_transaction_active;
    uint32_t program_edit_last_click_tick;
    unsigned int program_preview_scroll_start_line;
    int program_preview_highlight_line;
    unsigned int program_preview_highlight_loaded_epoch;
    unsigned int program_preview_started_loaded_epoch;
    int program_preview_touch_active;
    lv_point_t program_preview_touch_last_point;
    int program_preview_touch_accum_y;
    int program_preview_dragged;
    V5UiNavigationCallback navigation_cb;
    void *navigation_user_data;
    V5MainPageNativeReadbackRefreshCallback native_readback_refresh_cb;
    void *native_readback_refresh_user_data;
    V5ProgramOpenResult last_program_open;
    V5UiStatusView last_status;
    int last_status_valid;
    V5ToolpathDisplayPlane view_plane;
    double jog_step;
    V5MainPageSelection selection;
    lv_point_t trajectory_points[V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT];
    lv_point_t toolpath_segment_points[V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS][V5_MAIN_PAGE_TOOLPATH_SEGMENT_POINT_COUNT];
    unsigned int trajectory_point_count;
    lv_point_t toolpath_mcs_origin_points[5];
    lv_point_t toolpath_wcs_origin_points[5];
    lv_point_t toolpath_mcs_axis_points[3][2];
    lv_point_t toolpath_wcs_axis_points[3][2];
    lv_point_t toolpath_ac_axis_points[2][2];
    lv_point_t toolpath_holder_points[2];
    lv_point_t toolpath_program_wcs_origin_points[V5_MAIN_PAGE_TOOLPATH_WCS_COUNT][5];
    lv_point_t toolpath_program_wcs_axis_points[V5_MAIN_PAGE_TOOLPATH_WCS_COUNT][3][2];
    lv_point_t toolpath_gesture_last_points[2];
    int toolpath_gesture_active_count;
    double toolpath_gesture_last_mid_x;
    double toolpath_gesture_last_mid_y;
    double toolpath_gesture_last_distance;
    double toolpath_gesture_last_angle_deg;
    double toolpath_manual_scale;
    double toolpath_manual_pan_x;
    double toolpath_manual_pan_y;
    double toolpath_manual_rotate_deg;
    V5ToolpathDisplayFit toolpath_fit;
    unsigned int toolpath_program_generation;
    unsigned int toolpath_view_generation;
    unsigned int toolpath_program_view_generation;
    V5ToolpathDisplayPlane toolpath_program_plane;
    int toolpath_program_wcs_valid;
    int toolpath_program_wcs_index;
    double toolpath_program_wcs_offset[3];
    int toolpath_program_visible;
    unsigned int toolpath_program_point_count;
    int toolpath_program_ac_valid;
    int toolpath_program_model_kind;
    double toolpath_program_ac_a_deg;
    double toolpath_program_ac_c_deg;
    int toolpath_static_pose_valid;
    int toolpath_static_model_kind;
    double toolpath_static_pose_a_deg;
    double toolpath_static_pose_c_deg;
    V5StatusPoint toolpath_program_points[V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT];
    V5StatusPoint toolpath_program_project_points[V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT];
    V5ToolpathScreenPoint toolpath_program_screen_points[V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT];
    unsigned int toolpath_static_cache_hits;
    unsigned int toolpath_static_cache_misses;
    unsigned int toolpath_line_rewrite_count;
} V5MainPage;

void v5_main_page_init(V5MainPage *page);
int v5_main_page_create(V5MainPage *page, lv_obj_t *parent);
int v5_main_page_apply_status(V5MainPage *page, const V5UiStatusView *status);
int v5_main_page_apply_status_flags(V5MainPage *page, const V5UiStatusView *status, unsigned int refresh_flags);
int v5_main_page_handle_touch_points(V5MainPage *page, const lv_point_t *points, int count, int pressed, int *changed);
void v5_main_page_bind_program_controller(V5MainPage *page, V5ProgramController *controller);
void v5_main_page_set_command_execution_enabled(V5MainPage *page, int enabled);
void v5_main_page_set_native_readback(V5MainPage *page, const V5NativeReadback *readback);
void v5_main_page_set_navigation_callback(V5MainPage *page, V5UiNavigationCallback cb, void *user_data);
void v5_main_page_set_native_readback_refresh_callback(
    V5MainPage *page,
    V5MainPageNativeReadbackRefreshCallback cb,
    void *user_data);
int v5_main_page_open_program(V5MainPage *page, const char *path, V5ProgramOpenResult *open_report);
int v5_main_page_set_mdi_text(V5MainPage *page, const char *line);
void v5_main_page_select_all_axes(V5MainPage *page);
int v5_main_page_select_axis(V5MainPage *page, V5MainPageSelectionSpace space, char axis);
void v5_main_page_refresh_program_status(V5MainPage *page);
int v5_main_page_trigger_action(V5MainPage *page, V5MainPageActionKind action, V5MainPageActionReport *report);

#ifdef __cplusplus
}
#endif

#endif
