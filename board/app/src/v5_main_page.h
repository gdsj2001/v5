#ifndef V5_MAIN_PAGE_H
#define V5_MAIN_PAGE_H

#include "lvgl.h"
#include "v5_coordinate_digits.h"
#include "v5_coordinate_panel.h"
#include "v5_command_program.h"
#include "v5_main_page_actions.h"
#include "v5_motion_model_registry.h"
#include "v5_native_readback.h"
#include "v5_program_scene_ipc.h"
#include "v5_toolpath_display.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_ui_status_view.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_MAIN_PAGE_AXIS_COUNT V5_COORDINATE_AXIS_COUNT
#define V5_MAIN_PAGE_TRAJECTORY_POINT_COUNT V5_STATUS_TRAJECTORY_POINT_COUNT
#define V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT V5_PROGRAM_PREVIEW_POINT_COUNT
#define V5_MAIN_PAGE_BUTTON_COUNT 29u

enum {
    V5_MAIN_PAGE_REFRESH_DYNAMIC = 1u << 0,
    V5_MAIN_PAGE_REFRESH_BUTTONS = 1u << 1,
    V5_MAIN_PAGE_REFRESH_ESTOP = 1u << 2,
    V5_MAIN_PAGE_REFRESH_SLOW = 1u << 3,
    V5_MAIN_PAGE_REFRESH_POSE = 1u << 4,
    V5_MAIN_PAGE_REFRESH_STRUCTURE = 1u << 5,
    V5_MAIN_PAGE_REFRESH_ALL =
        V5_MAIN_PAGE_REFRESH_DYNAMIC |
        V5_MAIN_PAGE_REFRESH_BUTTONS |
        V5_MAIN_PAGE_REFRESH_ESTOP |
        V5_MAIN_PAGE_REFRESH_SLOW |
        V5_MAIN_PAGE_REFRESH_POSE |
        V5_MAIN_PAGE_REFRESH_STRUCTURE
};

enum {
    V5_MAIN_PAGE_NATIVE_READBACK_MODEL = 1u << 0,
    V5_MAIN_PAGE_NATIVE_READBACK_WCS = 1u << 1,
    V5_MAIN_PAGE_NATIVE_READBACK_PROGRAM = 1u << 2,
    V5_MAIN_PAGE_NATIVE_READBACK_SAFETY = 1u << 3,
    V5_MAIN_PAGE_NATIVE_READBACK_MODAL = 1u << 4,
    V5_MAIN_PAGE_NATIVE_READBACK_ALL =
        V5_MAIN_PAGE_NATIVE_READBACK_MODEL |
        V5_MAIN_PAGE_NATIVE_READBACK_WCS |
        V5_MAIN_PAGE_NATIVE_READBACK_PROGRAM |
        V5_MAIN_PAGE_NATIVE_READBACK_SAFETY |
        V5_MAIN_PAGE_NATIVE_READBACK_MODAL
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
    lv_obj_t *feed_override_slider;
    lv_obj_t *spindle_override_slider;
    lv_obj_t *feed_override_reset_hit;
    lv_obj_t *spindle_override_reset_hit;
    lv_obj_t *cpu0_label;
    lv_obj_t *cpu1_label;
    lv_obj_t *toolpath_clip_layer;
    lv_obj_t *trajectory_line;
    lv_obj_t *toolpath_summary_label;
    lv_obj_t *toolpath_detail_label;
    lv_obj_t *toolpath_view_label;
    lv_obj_t *toolpath_model_primary_label;
    lv_obj_t *toolpath_model_child_label;
    lv_obj_t *toolpath_wcs_label;
    lv_obj_t *program_name_label;
    lv_obj_t *program_line_bg[4];
    lv_obj_t *program_line_labels[4];
    lv_obj_t *program_edit_hit_area;
    lv_obj_t *power_on_home_popup;
    lv_obj_t *power_on_home_popup_message;
    lv_obj_t *power_on_home_popup_confirm;
    lv_obj_t *power_on_home_popup_close;
    V5UiFirstFrameGuard power_on_home_popup_frame_guard;
    lv_obj_t *buttons[V5_MAIN_PAGE_BUTTON_COUNT];
    lv_obj_t *button_labels[V5_MAIN_PAGE_BUTTON_COUNT];
    V5MainPageActionKind button_actions[V5_MAIN_PAGE_BUTTON_COUNT];
    unsigned int button_count;
    V5MainPageActionReport last_action;
    V5ProgramController *program_controller;
    V5NativeReadback native_readback;
    int command_execution_enabled;
    int override_syncing_from_status;
    int feed_override_drag_active;
    int spindle_override_drag_active;
    uint32_t feed_override_last_send_tick;
    uint32_t spindle_override_last_send_tick;
    int home_transaction_active;
    lv_timer_t *selection_idle_timer;
    lv_obj_t *jog_pressed_button;
    V5MainPageSelectionSpace jog_pressed_space;
    char jog_pressed_axis;
    int jog_pressed_positive;
    int jog_continuous_active;
    uint32_t jog_keepalive_last_tick;
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
    unsigned int trajectory_point_count;
    const V5StatusDisplayScene *toolpath_display_scene;
    int toolpath_display_scene_valid;
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
    unsigned int toolpath_fit_generation;
    unsigned int toolpath_program_view_generation;
    V5ToolpathDisplayPlane toolpath_program_plane;
    int toolpath_program_visible;
    unsigned int toolpath_program_point_count;
    uint64_t toolpath_scene_generation;
    uint64_t toolpath_last_request_program_source_identity;
    uint64_t toolpath_last_request_program_generation;
    uint64_t toolpath_last_request_view_generation;
    uint64_t toolpath_last_request_fit_generation;
    uint32_t toolpath_last_request_page_visible;
    int page_visible;
    uint32_t toolpath_request_last_send_tick;
    unsigned int toolpath_request_retry_count;
    unsigned int toolpath_static_cache_hits;
    unsigned int toolpath_static_cache_misses;
    unsigned int toolpath_dynamic_refresh_count;
    unsigned int toolpath_pose_refresh_count;
    unsigned int toolpath_structure_refresh_count;
    unsigned int toolpath_line_rewrite_count;
    unsigned int toolpath_line_set_points_count;
    unsigned int toolpath_program_rtcp_transform_count;
    unsigned int toolpath_program_fused_frame_count;
    unsigned int toolpath_line_last_dirty_rect_count;
    uint64_t toolpath_line_last_dirty_pixels;
    uint64_t toolpath_line_last_dirty_max_pixels;
} V5MainPage;

void v5_main_page_init(V5MainPage *page);
int v5_main_page_create(V5MainPage *page, lv_obj_t *parent);
int v5_main_page_apply_status(V5MainPage *page, const V5UiStatusView *status);
int v5_main_page_apply_status_flags(V5MainPage *page, const V5UiStatusView *status, unsigned int refresh_flags);
int v5_main_page_handle_touch_points(V5MainPage *page, const lv_point_t *points, int count, int pressed, int *changed);
void v5_main_page_bind_program_controller(V5MainPage *page, V5ProgramController *controller);
void v5_main_page_set_command_execution_enabled(V5MainPage *page, int enabled);
unsigned int v5_main_page_native_readback_change_flags(
    const V5NativeReadback *before,
    const V5NativeReadback *after);
void v5_main_page_set_native_readback_flags(
    V5MainPage *page,
    const V5NativeReadback *readback,
    unsigned int change_flags);
void v5_main_page_set_native_readback(V5MainPage *page, const V5NativeReadback *readback);
void v5_main_page_store_native_readback_during_modal(
    V5MainPage *page,
    const V5NativeReadback *readback);
void v5_main_page_set_navigation_callback(V5MainPage *page, V5UiNavigationCallback cb, void *user_data);
void v5_main_page_set_native_readback_refresh_callback(
    V5MainPage *page,
    V5MainPageNativeReadbackRefreshCallback cb,
    void *user_data);
int v5_main_page_open_program(V5MainPage *page, const char *path, V5ProgramOpenResult *open_report);
int v5_main_page_set_mdi_text(V5MainPage *page, const char *line);
void v5_main_page_select_all_axes(V5MainPage *page);
int v5_main_page_select_axis(V5MainPage *page, V5MainPageSelectionSpace space, char axis);
void v5_main_page_set_page_visible(V5MainPage *page, int visible);
void v5_main_page_refresh_program_status(V5MainPage *page);
int v5_main_page_trigger_action(V5MainPage *page, V5MainPageActionKind action, V5MainPageActionReport *report);
int v5_main_page_trigger_override(
    V5MainPage *page,
    int spindle,
    int percent,
    V5MainPageActionReport *report);

#ifdef __cplusplus
}
#endif

#endif
