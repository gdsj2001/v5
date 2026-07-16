#ifndef V5_MAIN_PAGE_INTERNAL_H
#define V5_MAIN_PAGE_INTERNAL_H

#include "v5_main_page.h"

#define V5_TOOLPATH_X 3
#define V5_TOOLPATH_Y 58
#define V5_TOOLPATH_W 388
#define V5_TOOLPATH_H 378
#define V5_PROGRAM_PREVIEW_ROWS 4
#define V5_PROGRAM_PREVIEW_X 0
#define V5_PROGRAM_PREVIEW_Y 441
#define V5_PROGRAM_PREVIEW_W 560
#define V5_PROGRAM_PREVIEW_H 154
#define V5_PROGRAM_PREVIEW_LINE_STEP 26
#define V5_TOOLPATH_GESTURE_MIN_SCALE 0.35
#define V5_TOOLPATH_GESTURE_MAX_SCALE 4.0
#define V5_TOOLPATH_GESTURE_LEFT_INSET 132
#define V5_TOOLPATH_GESTURE_RIGHT_INSET 64
#define V5_TOOLPATH_GESTURE_BOTTOM_INSET 58
#define V5_TOOLPATH_PROGRAM_LINE_WIDTH 1
#define V5_MAIN_PAGE_SELECTION_IDLE_MS 3000U
#define V5_MAIN_PAGE_JOG_HOLD_MS 500U
#define V5_MAIN_PAGE_JOG_KEEPALIVE_MS 200U

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int v5_main_page_internal_action_needs_native_readback_refresh(V5MainPageActionKind action);

int v5_main_page_internal_action_requires_power_on_home(
    const V5MainPage *page,
    V5MainPageActionKind action);

int v5_main_page_internal_active_preview_line_from_readback(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    const char **native_command_out);

void v5_main_page_internal_add_hidden_flag_if_visible(lv_obj_t *obj);

V5ToolpathScreenPoint v5_main_page_internal_apply_toolpath_view_transform(const V5MainPage *page, V5ToolpathScreenPoint point);

void v5_main_page_internal_apply_toolpath_view_transform_to_snapshot(const V5MainPage *page, V5ToolpathDisplaySnapshot *display);

void v5_main_page_internal_block_action_for_power_on_home(
    V5MainPage *page,
    V5MainPageActionKind action,
    V5MainPageActionReport *report);

lv_coord_t v5_main_page_internal_clamp_coord(double value, lv_coord_t min_value, lv_coord_t max_value);

double v5_main_page_internal_clamp_double(double value, double min_value, double max_value);

unsigned int v5_main_page_internal_clamp_preview_start_line(unsigned int total, unsigned int start);

void v5_main_page_internal_clear_hidden_flag_if_hidden(lv_obj_t *obj);

void v5_main_page_internal_clear_obj_style(lv_obj_t *obj);

void v5_main_page_internal_clear_program_preview_highlight(V5MainPage *page);

int v5_main_page_internal_clip_toolpath_segment(V5ToolpathScreenPoint *start, V5ToolpathScreenPoint *end);

unsigned int v5_main_page_internal_count_preview_source_lines(const char *text);

void v5_main_page_internal_create_main_program_edit_hit_area(V5MainPage *page);

void v5_main_page_internal_create_power_on_home_popup(V5MainPage *page);

void v5_main_page_internal_hide_toolpath_unproven_geometry(V5MainPage *page);

void v5_main_page_internal_feed_override_reset_event_cb(lv_event_t *event);

void v5_main_page_internal_format_main_page_wcs_coordinate(char *out, size_t out_size, const V5UiStatusView *status,
                                            const V5NativeReadback *readback, unsigned int axis);

void v5_main_page_internal_hide_toolpath_ac_geometry(V5MainPage *page);

void v5_main_page_internal_hide_toolpath_line(lv_obj_t *line);

void v5_main_page_internal_hide_toolpath_program_line(V5MainPage *page);

int v5_main_page_internal_update_toolpath_program_segment(
    V5MainPage *page,
    unsigned int segment,
    const lv_point_t *points,
    unsigned int point_count);

void v5_main_page_internal_hide_toolpath_program_wcs_objects(V5MainPage *page);

int v5_main_page_internal_is_view_action(V5MainPageActionKind action);

void v5_main_page_internal_jog_button_event_cb(lv_event_t *event);

void v5_main_page_internal_jog_hold_timer_cb(lv_timer_t *timer);

void v5_main_page_internal_log_button_event(V5MainPageActionKind action, int ok, const V5MainPageActionReport *report);

lv_color_t v5_main_page_internal_main_coordinate_digit_color(const V5MainPage *page, unsigned int axis, int is_wcs);

const V5MotionModelDescriptor *v5_main_page_internal_main_page_active_motion_model(const V5MainPage *page);

void v5_main_page_internal_main_page_apply_active_model_pose_to_world_point(
    const V5MotionModelDescriptor *model,
    double point[V5_STATUS_AXIS_COUNT],
    const double first_center[V5_STATUS_AXIS_COUNT],
    const double second_center[V5_STATUS_AXIS_COUNT],
    double first_deg,
    double second_deg);

int v5_main_page_internal_main_page_apply_program_preview_wcs_offset(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    V5StatusPoint *points,
    unsigned int count,
    int *wcs_index_out,
    double wcs_offset_out[3]);

char v5_main_page_internal_main_page_axis_display_char(const V5MainPage *page, unsigned int axis_index);

int v5_main_page_internal_main_page_axis_values_finite(const double axis[V5_STATUS_AXIS_COUNT]);

int v5_main_page_internal_main_page_dynamic_toolpath_outside_fit_window(
    const V5MainPage *page,
    const V5UiStatusView *status);

int v5_main_page_internal_main_page_expand_fit_on_overflow(V5MainPage *page, const V5UiStatusView *status);

void v5_main_page_internal_main_page_expand_visible_toolpath_fit(
    V5MainPage *page,
    const V5UiStatusView *status,
    V5ToolpathDisplayFit *fit);

void v5_main_page_internal_main_page_fit_expand_world_point(V5ToolpathDisplayFit *fit, const double axis[V5_STATUS_AXIS_COUNT]);

int v5_main_page_internal_main_page_g53_active_center_world(
    const V5MainPage *page,
    unsigned int index,
    double center[V5_STATUS_AXIS_COUNT]);

int v5_main_page_internal_main_page_handle_program_preview_touch(
    V5MainPage *page,
    const lv_point_t *point,
    int pressed,
    int *changed);

V5ToolpathScreenPoint v5_main_page_internal_apply_toolpath_view_transform(const V5MainPage *page, V5ToolpathScreenPoint point);

int v5_main_page_internal_main_page_program_ac_projection_changed(const V5MainPage *page, const V5UiStatusView *status);

int v5_main_page_internal_main_page_program_outside_fit_window(const V5MainPage *page);

int v5_main_page_internal_main_page_project_cmd_tip(const V5MainPage *page, const V5UiStatusView *status, V5ToolpathScreenPoint *point);

int v5_main_page_internal_main_page_project_program_with_current_fit(V5MainPage *page);

int v5_main_page_internal_main_page_project_world_point_transformed(
    const V5MainPage *page,
    const double world[V5_STATUS_AXIS_COUNT],
    V5ToolpathScreenPoint *point);

void v5_main_page_internal_main_page_root_delete_event_cb(lv_event_t *event);

void v5_main_page_internal_main_page_rotate_about_active_model_first_axis(
    const V5MotionModelDescriptor *model,
    double point[V5_STATUS_AXIS_COUNT],
    const double center[V5_STATUS_AXIS_COUNT],
    double first_deg);

int v5_main_page_internal_main_page_rtcp_wcs_follow_active_model_available(
    const V5MainPage *page,
    const V5UiStatusView *status,
    const V5MotionModelDescriptor **model,
    double *first_deg,
    double *second_deg,
    double first_center[V5_STATUS_AXIS_COUNT],
    double second_center[V5_STATUS_AXIS_COUNT]);

int v5_main_page_internal_main_page_static_geometry_outside_fit_window(
    V5MainPage *page,
    const V5UiStatusView *status);

int v5_main_page_internal_main_page_static_pose_changed(const V5MainPage *page, const V5UiStatusView *status);

void v5_main_page_internal_main_page_store_static_pose(V5MainPage *page, const V5UiStatusView *status);

int v5_main_page_internal_main_page_tool_length_mm(const V5MainPage *page, double *out);

int v5_main_page_internal_main_page_update_program_project_points(
    V5MainPage *page,
    const V5UiStatusView *status,
    unsigned int count);

const char *v5_main_page_internal_main_page_wcs_code(const V5NativeReadback *readback);

void v5_main_page_internal_make_button_rgb(V5MainPage *page, int x, int y, int w, int h, V5MainPageActionKind action, const char *text, uint8_t r, uint8_t g, uint8_t b);

void v5_main_page_internal_make_coordinate_value_clickable(V5MainPage *page, lv_obj_t *label);

void v5_main_page_internal_make_divider(lv_obj_t *parent, int x, int y, int w, int h);

lv_obj_t *v5_main_page_internal_make_label_ex(lv_obj_t *parent, int x, int y, int w, int h, const char *text, uint8_t r, uint8_t g, uint8_t b, lv_text_align_t align);

lv_obj_t *v5_main_page_internal_make_override_reset_hit(V5MainPage *page, int x, int y, int w, int h, lv_event_cb_t cb);

lv_obj_t *v5_main_page_internal_create_override_slider(
    V5MainPage *page,
    int spindle,
    int x,
    int y,
    int width);

void v5_main_page_internal_sync_override_sliders(
    V5MainPage *page,
    const V5UiStatusView *status);

lv_obj_t *v5_main_page_internal_make_panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);

lv_obj_t *v5_main_page_internal_make_toolpath_v3_center_dot(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b);

lv_obj_t *v5_main_page_internal_make_toolpath_v3_dot(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b, uint8_t br, uint8_t bg, uint8_t bb);

lv_obj_t *v5_main_page_internal_make_toolpath_v3_line(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b, uint8_t width);

void v5_main_page_internal_make_v3_main_buttons(V5MainPage *page);

void v5_main_page_internal_mark_toolpath_static_dirty(V5MainPage *page);

double v5_main_page_internal_monotonic_seconds(void);

double v5_main_page_internal_normalize_deg(double value);

double v5_main_page_internal_point_angle_deg(const lv_point_t *a, const lv_point_t *b);

double v5_main_page_internal_point_distance(const lv_point_t *a, const lv_point_t *b);

int v5_main_page_internal_point_in_program_preview_zone(const lv_point_t *point);

int v5_main_page_internal_points_equal(const lv_point_t *a, const lv_point_t *b, unsigned int count);

unsigned int v5_main_page_internal_program_preview_highlight_epoch(const V5ProgramRuntime *runtime);

void v5_main_page_internal_refresh_coordinate_selection_now(V5MainPage *page);

void v5_main_page_internal_refresh_program_preview_rows(V5MainPage *page, const V5ProgramRuntime *runtime);

int v5_main_page_internal_remembered_program_preview_highlight_line(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    unsigned int total);

void v5_main_page_internal_reset_selection_idle_timer(V5MainPage *page);

void v5_main_page_internal_reset_toolpath_view_rotation(V5MainPage *page);

lv_color_t v5_main_page_internal_rgb(uint8_t r, uint8_t g, uint8_t b);

void v5_main_page_internal_selection_idle_timer_cb(lv_timer_t *timer);

void v5_main_page_internal_set_home_transaction_active(V5MainPage *page, int active, int flush);

void v5_main_page_internal_set_label_text_if_changed(lv_obj_t *label, const char *text);

void v5_main_page_internal_set_obj_bg_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector);

void v5_main_page_internal_set_obj_border_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector);

void v5_main_page_internal_set_obj_pos_if_changed(lv_obj_t *obj, lv_coord_t x, lv_coord_t y);

void v5_main_page_internal_set_obj_text_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector);

void v5_main_page_internal_set_toolpath_axis_line(lv_obj_t *line, lv_point_t points[2], const V5ToolpathScreenPoint *start, const V5ToolpathScreenPoint *end, int valid);

void v5_main_page_internal_set_toolpath_origin_cross(lv_obj_t *line, lv_point_t points[5], const V5ToolpathScreenPoint *origin, int valid);

void v5_main_page_internal_set_toolpath_v3_center_dot(lv_obj_t *dot, const V5ToolpathScreenPoint *point, int valid);

void v5_main_page_internal_set_toolpath_v3_dot_center(lv_obj_t *dot, const V5ToolpathScreenPoint *point, int valid);

void v5_main_page_internal_show_home_precondition_popup(V5MainPage *page, const char *alias_code);

void v5_main_page_internal_spindle_override_reset_event_cb(lv_event_t *event);

int v5_main_page_internal_toolpath_points_in_graphics_zone(const lv_point_t *points, int count);

V5ToolpathScreenPoint v5_main_page_internal_toolpath_scaffold_point(double x, double y);

void v5_main_page_internal_update_axis_all_button_visuals(V5MainPage *page);

void v5_main_page_internal_update_coordinate_selection_style(V5MainPage *page);

void v5_main_page_internal_update_coordinate_target_axes(V5MainPage *page);

void v5_main_page_internal_update_estop_button_text(V5MainPage *page);

void v5_main_page_internal_update_main_page_modal_label(V5MainPage *page);

void v5_main_page_internal_update_main_page_state_button_visuals(V5MainPage *page);

void v5_main_page_internal_update_main_page_wcs_header(V5MainPage *page);

void v5_main_page_internal_update_toolpath_holder_line(V5MainPage *page, const V5UiStatusView *status, const V5ToolpathScreenPoint *holder_point);

void v5_main_page_internal_update_toolpath_state_lines(V5MainPage *page, const V5UiStatusView *status);

void v5_main_page_internal_update_toolpath_status_text(V5MainPage *page);

void v5_main_page_internal_update_wcs_button_visuals(V5MainPage *page);

int v5_main_page_internal_view_action_matches_plane(V5MainPageActionKind action, V5ToolpathDisplayPlane plane);

#endif
