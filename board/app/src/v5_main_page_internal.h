#ifndef V5_MAIN_PAGE_INTERNAL_H
#define V5_MAIN_PAGE_INTERNAL_H

#include "v5_main_page.h"
#include "v5_toolpath_viewport.h"

#define V5_PROGRAM_PREVIEW_ROWS 4
#define V5_PROGRAM_PREVIEW_X 0
#define V5_PROGRAM_PREVIEW_Y 441
#define V5_PROGRAM_PREVIEW_W 560
#define V5_PROGRAM_PREVIEW_H 154
#define V5_PROGRAM_PREVIEW_LINE_STEP 26
#define V5_TOOLPATH_GESTURE_MIN_SCALE 0.35
#define V5_TOOLPATH_GESTURE_MAX_SCALE 4.0
#define V5_TOOLPATH_PROGRAM_LINE_WIDTH 1
#define V5_MAIN_PAGE_SELECTION_IDLE_MS 3000U
#define V5_MAIN_PAGE_JOG_KEEPALIVE_MS 200U

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct V5ToolpathViewTransform {
    double scale;
    double sine;
    double cosine;
    double pan_x;
    double pan_y;
    int identity;
} V5ToolpathViewTransform;

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
V5ToolpathScreenPoint v5_main_page_internal_apply_toolpath_view_transform_prepared(
    V5ToolpathScreenPoint point,
    const V5ToolpathViewTransform *transform);
void v5_main_page_internal_apply_toolpath_view_transform_points(
    const V5MainPage *page,
    V5ToolpathScreenPoint *points,
    unsigned int count);
void v5_main_page_internal_prepare_toolpath_view_transform(
    const V5MainPage *page,
    V5ToolpathViewTransform *transform);

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

unsigned int v5_main_page_internal_count_preview_source_lines(const char *text);

void v5_main_page_internal_create_main_program_edit_hit_area(V5MainPage *page);

void v5_main_page_internal_create_power_on_home_popup(V5MainPage *page);

void v5_main_page_internal_hide_toolpath_unproven_geometry(V5MainPage *page);

void v5_main_page_internal_feed_override_reset_event_cb(lv_event_t *event);

void v5_main_page_internal_format_main_page_wcs_coordinate(char *out, size_t out_size, const V5UiStatusView *status,
                                            const V5NativeReadback *readback, unsigned int axis);

void v5_main_page_internal_coalesce_toolpath_invalidations(V5MainPage *page);

void v5_main_page_internal_hide_toolpath_program_line(V5MainPage *page);
void v5_main_page_internal_clear_program_raster(V5MainPage *page);
void v5_main_page_internal_update_program_raster(
    V5MainPage *page,
    const V5StatusDisplayScene *scene,
    uint64_t scene_generation);
int v5_main_page_internal_program_raster_pixel(
    const V5MainPage *page,
    int x,
    int y);

lv_obj_t *v5_main_page_internal_create_toolpath_scene_layer(
    V5MainPage *page,
    lv_obj_t *parent);

int v5_main_page_internal_is_view_action(V5MainPageActionKind action);

void v5_main_page_internal_jog_button_event_cb(lv_event_t *event);

void v5_main_page_internal_log_button_event(V5MainPageActionKind action, int ok, const V5MainPageActionReport *report);

lv_color_t v5_main_page_internal_main_coordinate_digit_color(const V5MainPage *page, unsigned int axis, int is_wcs);

const V5MotionModelDescriptor *v5_main_page_internal_main_page_active_motion_model(const V5MainPage *page);

char v5_main_page_internal_main_page_axis_display_char(const V5MainPage *page, unsigned int axis_index);

int v5_main_page_internal_main_page_handle_program_preview_touch(
    V5MainPage *page,
    const lv_point_t *point,
    int pressed,
    int *changed);

void v5_main_page_internal_main_page_root_delete_event_cb(lv_event_t *event);

const char *v5_main_page_internal_main_page_wcs_code(const V5NativeReadback *readback);

int v5_main_page_internal_publish_program_scene_request(V5MainPage *page);
int v5_main_page_internal_apply_program_display_scene(
    V5MainPage *page,
    const V5StatusDisplayScene *scene);
void v5_main_page_internal_apply_display_scene(
    V5MainPage *page,
    const V5StatusDisplayScene *scene,
    uint64_t scene_generation);

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

void v5_main_page_internal_sync_program_preview_after_execution(
    V5MainPage *page,
    V5CommandKind request_kind);

int v5_main_page_internal_remembered_program_preview_highlight_line(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    unsigned int total);

void v5_main_page_internal_reset_selection_idle_timer(V5MainPage *page);

void v5_main_page_internal_reset_toolpath_view(V5MainPage *page);

lv_color_t v5_main_page_internal_rgb(uint8_t r, uint8_t g, uint8_t b);

void v5_main_page_internal_selection_idle_timer_cb(lv_timer_t *timer);

void v5_main_page_internal_set_home_transaction_active(V5MainPage *page, int active, int flush);

void v5_main_page_internal_set_label_text_if_changed(lv_obj_t *label, const char *text);

void v5_main_page_internal_set_obj_bg_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector);

void v5_main_page_internal_set_obj_border_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector);

void v5_main_page_internal_set_obj_pos_if_changed(lv_obj_t *obj, lv_coord_t x, lv_coord_t y);

void v5_main_page_internal_set_obj_text_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector);

void v5_main_page_internal_show_home_precondition_popup(V5MainPage *page, const char *alias_code);

void v5_main_page_internal_spindle_override_reset_event_cb(lv_event_t *event);

int v5_main_page_internal_toolpath_points_in_graphics_zone(const lv_point_t *points, int count);

void v5_main_page_internal_update_axis_all_button_visuals(V5MainPage *page);

void v5_main_page_internal_update_coordinate_selection_style(V5MainPage *page);

void v5_main_page_internal_update_coordinate_target_axes(V5MainPage *page);

void v5_main_page_internal_update_estop_button_text(V5MainPage *page);

void v5_main_page_internal_update_main_page_modal_label(V5MainPage *page);

void v5_main_page_internal_update_main_page_state_button_visuals(V5MainPage *page);

void v5_main_page_internal_update_main_page_wcs_header(V5MainPage *page);

void v5_main_page_internal_update_toolpath_state_lines(V5MainPage *page, const V5UiStatusView *status);

void v5_main_page_internal_update_toolpath_status_text(V5MainPage *page);

void v5_main_page_internal_update_wcs_button_visuals(V5MainPage *page);

int v5_main_page_internal_view_action_matches_plane(V5MainPageActionKind action, V5ToolpathDisplayPlane plane);

#endif
