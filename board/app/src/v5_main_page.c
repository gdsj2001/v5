#include "v5_main_page.h"

#include "v5_command_gate_ipc.h"
#include "v5_native_wcs_status.h"
#include "v5_layout_icons.h"
#include "v5_remote_metrics.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define V5_TOOLPATH_X 3
#define V5_TOOLPATH_Y 58
#define V5_TOOLPATH_W 388
#define V5_TOOLPATH_H 378
#define V5_PROGRAM_PREVIEW_ROWS 4
#define V5_TOOLPATH_GESTURE_MIN_SCALE 0.35
#define V5_TOOLPATH_GESTURE_MAX_SCALE 4.0
#define V5_TOOLPATH_GESTURE_LEFT_INSET 132
#define V5_TOOLPATH_GESTURE_RIGHT_INSET 64
#define V5_TOOLPATH_GESTURE_BOTTOM_INSET 58
#define V5_TOOLPATH_PROGRAM_LINE_WIDTH 2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static lv_color_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    const char *safe = text ? text : "";
    const char *current;
    if (!label) {
        return;
    }
    current = lv_label_get_text(label);
    if (!current || strcmp(current, safe) != 0) {
        lv_label_set_text(label, safe);
    }
}

static int main_color_equal(lv_color_t left, lv_color_t right)
{
    return lv_color_to32(left) == lv_color_to32(right);
}

static void set_obj_bg_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector)
{
    if (obj && !main_color_equal(lv_obj_get_style_bg_color(obj, selector), color)) {
        lv_obj_set_style_bg_color(obj, color, selector);
    }
}

static void set_obj_border_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector)
{
    if (obj && !main_color_equal(lv_obj_get_style_border_color(obj, selector), color)) {
        lv_obj_set_style_border_color(obj, color, selector);
    }
}

static void set_obj_text_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector)
{
    if (obj && !main_color_equal(lv_obj_get_style_text_color(obj, selector), color)) {
        lv_obj_set_style_text_color(obj, color, selector);
    }
}

static int point_equal(const lv_point_t *a, const lv_point_t *b)
{
    return a && b && a->x == b->x && a->y == b->y;
}

static int points_equal(const lv_point_t *a, const lv_point_t *b, unsigned int count)
{
    unsigned int i;
    if (!a || !b) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (!point_equal(&a[i], &b[i])) {
            return 0;
        }
    }
    return 1;
}

static void add_hidden_flag_if_visible(lv_obj_t *obj)
{
    if (obj && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clear_hidden_flag_if_hidden(lv_obj_t *obj)
{
    if (obj && lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_obj_pos_if_changed(lv_obj_t *obj, lv_coord_t x, lv_coord_t y)
{
    if (obj && (lv_obj_get_x(obj) != x || lv_obj_get_y(obj) != y)) {
        lv_obj_set_pos(obj, x, y);
    }
}

static lv_coord_t clamp_coord(double value, lv_coord_t min_value, lv_coord_t max_value)
{
    if (value < (double)min_value) {
        return min_value;
    }
    if (value > (double)max_value) {
        return max_value;
    }
    return (lv_coord_t)value;
}

static double clamp_double(double value, double min_value, double max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static double normalize_deg(double value)
{
    while (value > 360.0) {
        value -= 720.0;
    }
    while (value < -360.0) {
        value += 720.0;
    }
    return value;
}

static double point_distance(const lv_point_t *a, const lv_point_t *b)
{
    const double dx = (double)b->x - (double)a->x;
    const double dy = (double)b->y - (double)a->y;
    return sqrt((dx * dx) + (dy * dy));
}

static double point_angle_deg(const lv_point_t *a, const lv_point_t *b)
{
    const double dx = (double)b->x - (double)a->x;
    const double dy = (double)b->y - (double)a->y;
    return atan2(dy, dx) * 180.0 / M_PI;
}

static int toolpath_point_in_graphics_zone(const lv_point_t *point)
{
    const int x0 = V5_TOOLPATH_X + V5_TOOLPATH_GESTURE_LEFT_INSET;
    const int y0 = V5_TOOLPATH_Y;
    const int x1 = V5_TOOLPATH_X + V5_TOOLPATH_W - V5_TOOLPATH_GESTURE_RIGHT_INSET;
    const int y1 = V5_TOOLPATH_Y + V5_TOOLPATH_H - V5_TOOLPATH_GESTURE_BOTTOM_INSET;
    return point && point->x >= x0 && point->x <= x1 && point->y >= y0 && point->y <= y1;
}

static int toolpath_points_in_graphics_zone(const lv_point_t *points, int count)
{
    int i;
    if (!points || count <= 0) {
        return 0;
    }
    if (count > 2) {
        count = 2;
    }
    for (i = 0; i < count; ++i) {
        if (!toolpath_point_in_graphics_zone(&points[i])) {
            return 0;
        }
    }
    return 1;
}

static V5ToolpathScreenPoint apply_toolpath_view_transform(const V5MainPage *page, V5ToolpathScreenPoint point)
{
    const double cx = (double)V5_TOOLPATH_W * 0.5;
    const double cy = (double)V5_TOOLPATH_H * 0.5;
    double scale = 1.0;
    double rad;
    double s;
    double c;
    double dx;
    double dy;
    double rx;
    double ry;
    if (!page) {
        return point;
    }
    scale = page->toolpath_manual_scale > 0.0 ? page->toolpath_manual_scale : 1.0;
    rad = page->toolpath_manual_rotate_deg * M_PI / 180.0;
    s = sin(rad);
    c = cos(rad);
    dx = point.x - cx;
    dy = point.y - cy;
    rx = (dx * c) - (dy * s);
    ry = (dx * s) + (dy * c);
    point.x = cx + (rx * scale) + page->toolpath_manual_pan_x;
    point.y = cy + (ry * scale) + page->toolpath_manual_pan_y;
    return point;
}

static void apply_toolpath_view_transform_to_snapshot(const V5MainPage *page, V5ToolpathDisplaySnapshot *display)
{
    unsigned int i;
    if (!page || !display) {
        return;
    }
    if (display->trajectory_valid) {
        for (i = 0U; i < display->point_count; ++i) {
            display->trajectory[i] = apply_toolpath_view_transform(page, display->trajectory[i]);
        }
    }
    if (display->mcs_valid) {
        display->mcs_point = apply_toolpath_view_transform(page, display->mcs_point);
    }
    if (display->cmd_valid) {
        display->cmd_point = apply_toolpath_view_transform(page, display->cmd_point);
    }
}

static void main_page_plane_values(const double axis[V5_STATUS_AXIS_COUNT], V5ToolpathDisplayPlane plane, double *u, double *v)
{
    if (plane == V5_TOOLPATH_DISPLAY_XZ) {
        *u = axis[0];
        *v = axis[2];
    } else if (plane == V5_TOOLPATH_DISPLAY_YZ) {
        *u = axis[1];
        *v = axis[2];
    } else if (plane == V5_TOOLPATH_DISPLAY_3D) {
        *u = (axis[0] * 0.7071067811865476) + (axis[1] * 0.7071067811865476);
        *v = (axis[0] * -0.4082482904638631) + (axis[1] * 0.4082482904638631) + (axis[2] * 0.8164965809277261);
    } else {
        *u = axis[0];
        *v = axis[1];
    }
}

static int main_page_axis_values_finite(const double axis[V5_STATUS_AXIS_COUNT])
{
    unsigned int i;
    if (!axis) {
        return 0;
    }
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!isfinite(axis[i])) {
            return 0;
        }
    }
    return 1;
}

static void main_page_fit_expand_world_point(V5ToolpathDisplayFit *fit, const double axis[V5_STATUS_AXIS_COUNT])
{
    double u;
    double v;
    if (!fit || !fit->valid || !main_page_axis_values_finite(axis)) {
        return;
    }
    main_page_plane_values(axis, fit->plane, &u, &v);
    if (!fit->bounds.valid) {
        fit->bounds.min_u = fit->bounds.max_u = u;
        fit->bounds.min_v = fit->bounds.max_v = v;
        fit->bounds.valid = 1;
        return;
    }
    if (u < fit->bounds.min_u) fit->bounds.min_u = u;
    if (u > fit->bounds.max_u) fit->bounds.max_u = u;
    if (v < fit->bounds.min_v) fit->bounds.min_v = v;
    if (v > fit->bounds.max_v) fit->bounds.max_v = v;
}

static int main_page_tool_length_mm(const V5MainPage *page, double *out);

static int main_page_g53_ac_center_world(const V5MainPage *page, unsigned int index, double center[V5_STATUS_AXIS_COUNT])
{
    const double *source;
    const double *active_offsets;
    unsigned int native_center;
    unsigned int i;
    if (!page || !center) {
        return 0;
    }
    if (index > 1U) {
        return 0;
    }
    active_offsets = v5_native_readback_active_wcs_offsets(&page->native_readback);
    if (!active_offsets || !isfinite(active_offsets[0]) || !isfinite(active_offsets[2])) {
        return 0;
    }
    native_center = index == 0U ? V5_NATIVE_READBACK_G53_CENTER_A : V5_NATIVE_READBACK_G53_CENTER_C;
    source = v5_native_readback_g53_center(&page->native_readback, native_center);
    if (!source) {
        return 0;
    }
    memset(center, 0, sizeof(double) * V5_STATUS_AXIS_COUNT);
    for (i = 0U; i < V5_NATIVE_READBACK_G53_AXIS_COUNT && i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!isfinite(source[i])) {
            memset(center, 0, sizeof(double) * V5_STATUS_AXIS_COUNT);
            return 0;
        }
        center[i] = source[i];
    }
    if (index == 0U) {
        center[0] = active_offsets[0];
    } else {
        center[2] = active_offsets[2];
    }
    return 1;
}

static int main_page_project_world_point_transformed(
    const V5MainPage *page,
    const double world[V5_STATUS_AXIS_COUNT],
    V5ToolpathScreenPoint *point)
{
    if (!page || !world || !point || !page->toolpath_fit.valid ||
        !v5_toolpath_display_project_world_point(world, &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, point)) {
        return 0;
    }
    *point = apply_toolpath_view_transform(page, *point);
    return 1;
}

static int main_page_status_ac_display_values(const V5UiStatusView *status, double *a_deg, double *c_deg)
{
    if (a_deg) {
        *a_deg = 0.0;
    }
    if (c_deg) {
        *c_deg = 0.0;
    }
    if (!status || (status->valid_mask & V5_STATUS_VALID_MCS) == 0U ||
        !isfinite(status->mcs[3]) || !isfinite(status->mcs[4])) {
        return 0;
    }
    if (a_deg) {
        *a_deg = status->mcs[3];
    }
    if (c_deg) {
        *c_deg = status->mcs[4];
    }
    return 1;
}

static int main_page_program_ac_projection_available(
    const V5MainPage *page,
    const V5UiStatusView *status,
    double *a_deg,
    double *c_deg,
    double a_center[V5_STATUS_AXIS_COUNT],
    double c_center[V5_STATUS_AXIS_COUNT])
{
    if (!page || !main_page_status_ac_display_values(status, a_deg, c_deg) ||
        !main_page_g53_ac_center_world(page, 0U, a_center) ||
        !main_page_g53_ac_center_world(page, 1U, c_center)) {
        return 0;
    }
    return 1;
}

static int main_page_program_ac_projection_changed(const V5MainPage *page, const V5UiStatusView *status)
{
    double a_deg = 0.0;
    double c_deg = 0.0;
    double a_center[V5_STATUS_AXIS_COUNT];
    double c_center[V5_STATUS_AXIS_COUNT];
    int valid;

    if (!page || !page->toolpath_program_visible || page->toolpath_program_point_count == 0U) {
        return 0;
    }
    valid = main_page_program_ac_projection_available(page, status, &a_deg, &c_deg, a_center, c_center);
    if (valid != page->toolpath_program_ac_valid) {
        return 1;
    }
    if (!valid) {
        return 0;
    }
    return fabs(a_deg - page->toolpath_program_ac_a_deg) > 0.0005 ||
           fabs(c_deg - page->toolpath_program_ac_c_deg) > 0.0005;
}

static int main_page_static_pose_changed(const V5MainPage *page, const V5UiStatusView *status)
{
    double a_deg = 0.0;
    double c_deg = 0.0;
    int valid = main_page_status_ac_display_values(status, &a_deg, &c_deg);
    if (!page) {
        return 0;
    }
    if (valid != page->toolpath_static_pose_valid) {
        return 1;
    }
    if (!valid) {
        return 0;
    }
    return fabs(a_deg - page->toolpath_static_pose_a_deg) > 0.0005 ||
           fabs(c_deg - page->toolpath_static_pose_c_deg) > 0.0005;
}

static void main_page_store_static_pose(V5MainPage *page, const V5UiStatusView *status)
{
    double a_deg = 0.0;
    double c_deg = 0.0;
    int valid;
    if (!page) {
        return;
    }
    valid = main_page_status_ac_display_values(status, &a_deg, &c_deg);
    page->toolpath_static_pose_valid = valid;
    page->toolpath_static_pose_a_deg = valid ? a_deg : 0.0;
    page->toolpath_static_pose_c_deg = valid ? c_deg : 0.0;
}

static void main_page_rotate_xyz_about_axis(
    double point[V5_STATUS_AXIS_COUNT],
    const double center[V5_STATUS_AXIS_COUNT],
    const double axis[3],
    double angle_rad)
{
    double vx;
    double vy;
    double vz;
    double kx;
    double ky;
    double kz;
    double norm;
    double c;
    double s;
    double dot;
    double cross_x;
    double cross_y;
    double cross_z;

    if (!point || !center || !axis) {
        return;
    }
    norm = sqrt((axis[0] * axis[0]) + (axis[1] * axis[1]) + (axis[2] * axis[2]));
    if (norm <= 1.0e-12 || !isfinite(norm) || !isfinite(angle_rad)) {
        return;
    }
    kx = axis[0] / norm;
    ky = axis[1] / norm;
    kz = axis[2] / norm;
    vx = point[0] - center[0];
    vy = point[1] - center[1];
    vz = point[2] - center[2];
    c = cos(angle_rad);
    s = sin(angle_rad);
    dot = (kx * vx) + (ky * vy) + (kz * vz);
    cross_x = (ky * vz) - (kz * vy);
    cross_y = (kz * vx) - (kx * vz);
    cross_z = (kx * vy) - (ky * vx);
    point[0] = center[0] + (vx * c) + (cross_x * s) + (kx * dot * (1.0 - c));
    point[1] = center[1] + (vy * c) + (cross_y * s) + (ky * dot * (1.0 - c));
    point[2] = center[2] + (vz * c) + (cross_z * s) + (kz * dot * (1.0 - c));
}

static void main_page_update_program_project_points(
    V5MainPage *page,
    const V5UiStatusView *status,
    unsigned int count)
{
    double a_deg = 0.0;
    double c_deg = 0.0;
    int ac_valid;
    double a_center[V5_STATUS_AXIS_COUNT];
    double c_center[V5_STATUS_AXIS_COUNT];
    double a_axis[3] = {1.0, 0.0, 0.0};
    double c_axis[3] = {0.0, 0.0, 1.0};
    double a_rad;
    double c_rad;
    unsigned int i;

    if (!page) {
        return;
    }
    if (count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    for (i = 0U; i < count; ++i) {
        page->toolpath_program_project_points[i] = page->toolpath_program_points[i];
    }
    ac_valid = main_page_program_ac_projection_available(page, status, &a_deg, &c_deg, a_center, c_center);
    page->toolpath_program_ac_valid = ac_valid;
    page->toolpath_program_ac_a_deg = ac_valid ? a_deg : 0.0;
    page->toolpath_program_ac_c_deg = ac_valid ? c_deg : 0.0;
    if (!ac_valid) {
        return;
    }
    a_rad = a_deg * M_PI / 180.0;
    c_rad = c_deg * M_PI / 180.0;
    c_axis[1] = -sin(a_rad);
    c_axis[2] = cos(a_rad);
    for (i = 0U; i < count; ++i) {
        main_page_rotate_xyz_about_axis(page->toolpath_program_project_points[i].axis, a_center, a_axis, a_rad);
        main_page_rotate_xyz_about_axis(page->toolpath_program_project_points[i].axis, c_center, c_axis, c_rad);
    }
}

static int main_page_rtcp_rotates_current_wcs(const V5MainPage *page)
{
    return page &&
        v5_native_readback_rtcp_known(&page->native_readback) &&
        page->native_readback.rtcp_enabled;
}

static int main_page_apply_ac_projection_to_world(
    const V5MainPage *page,
    const V5UiStatusView *status,
    double world[V5_STATUS_AXIS_COUNT])
{
    double a_deg = 0.0;
    double c_deg = 0.0;
    double a_center[V5_STATUS_AXIS_COUNT];
    double c_center[V5_STATUS_AXIS_COUNT];
    double a_axis[3] = {1.0, 0.0, 0.0};
    double c_axis[3] = {0.0, 0.0, 1.0};
    double a_rad;
    double c_rad;

    if (!world ||
        !main_page_program_ac_projection_available(page, status, &a_deg, &c_deg, a_center, c_center)) {
        return 0;
    }

    a_rad = a_deg * M_PI / 180.0;
    c_rad = c_deg * M_PI / 180.0;
    c_axis[1] = -sin(a_rad);
    c_axis[2] = cos(a_rad);
    main_page_rotate_xyz_about_axis(world, a_center, a_axis, a_rad);
    main_page_rotate_xyz_about_axis(world, c_center, c_axis, c_rad);
    return 1;
}

static void main_page_expand_static_geometry_fit(V5MainPage *page, const V5UiStatusView *status)
{
    double point[V5_STATUS_AXIS_COUNT];
    double a_center[V5_STATUS_AXIS_COUNT];
    double c_center[V5_STATUS_AXIS_COUNT];
    double angle;
    double c_vec_y;
    double c_vec_z;
    unsigned int i;
    if (!page || !page->toolpath_fit.valid) {
        return;
    }
    if (v5_native_readback_wcs_offset_known(&page->native_readback)) {
        const double *active_offsets = v5_native_readback_active_wcs_offsets(&page->native_readback);
        if (!active_offsets) {
            return;
        }
        memset(point, 0, sizeof(point));
        for (i = 0U; i < V5_STATUS_AXIS_COUNT && i < V5_NATIVE_READBACK_WCS_OFFSET_COUNT; ++i) {
            point[i] = active_offsets[i];
        }
        main_page_fit_expand_world_point(&page->toolpath_fit, point);
        for (i = 0U; i < 3U; ++i) {
            point[i] += 40.0;
            main_page_fit_expand_world_point(&page->toolpath_fit, point);
            point[i] -= 40.0;
        }
    }
    if (main_page_g53_ac_center_world(page, 0U, a_center)) {
        point[0] = a_center[0] - 40.0; point[1] = a_center[1]; point[2] = a_center[2]; point[3] = 0.0; point[4] = 0.0;
        main_page_fit_expand_world_point(&page->toolpath_fit, point);
        point[0] = a_center[0] + 40.0;
        main_page_fit_expand_world_point(&page->toolpath_fit, point);
    }
    if (main_page_g53_ac_center_world(page, 1U, c_center)) {
        angle = (status && (status->valid_mask & V5_STATUS_VALID_MCS) && isfinite(status->mcs[3])) ? status->mcs[3] * M_PI / 180.0 : 0.0;
        c_vec_y = -sin(angle) * 40.0;
        c_vec_z = cos(angle) * 40.0;
        point[0] = c_center[0]; point[1] = c_center[1] - c_vec_y; point[2] = c_center[2] - c_vec_z; point[3] = 0.0; point[4] = 0.0;
        main_page_fit_expand_world_point(&page->toolpath_fit, point);
        point[1] = c_center[1] + c_vec_y;
        point[2] = c_center[2] + c_vec_z;
        main_page_fit_expand_world_point(&page->toolpath_fit, point);
    }
}

static void main_page_expand_visible_toolpath_fit(V5MainPage *page, const V5UiStatusView *status)
{
    double origin[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double x_axis[V5_STATUS_AXIS_COUNT] = {40.0, 0.0, 0.0, 0.0, 0.0};
    double y_axis[V5_STATUS_AXIS_COUNT] = {0.0, 40.0, 0.0, 0.0, 0.0};
    double z_axis[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 40.0, 0.0, 0.0};
    double tool_len = 0.0;

    if (!page || !page->toolpath_fit.valid) {
        return;
    }

    main_page_fit_expand_world_point(&page->toolpath_fit, origin);
    main_page_fit_expand_world_point(&page->toolpath_fit, x_axis);
    main_page_fit_expand_world_point(&page->toolpath_fit, y_axis);
    main_page_fit_expand_world_point(&page->toolpath_fit, z_axis);

    if (status && (status->valid_mask & V5_STATUS_VALID_MCS) != 0U && main_page_axis_values_finite(status->mcs)) {
        double holder_end[V5_STATUS_AXIS_COUNT];
        main_page_fit_expand_world_point(&page->toolpath_fit, status->mcs);
        if (main_page_tool_length_mm(page, &tool_len) && fabs(tool_len) > 1.0e-9) {
            memcpy(holder_end, status->mcs, sizeof(holder_end));
            holder_end[2] -= tool_len;
            main_page_fit_expand_world_point(&page->toolpath_fit, holder_end);
        }
    }

    if (status && (status->valid_mask & V5_STATUS_VALID_CMD_MCS) != 0U && main_page_axis_values_finite(status->cmd_mcs)) {
        double cmd_tip[V5_STATUS_AXIS_COUNT];
        memcpy(cmd_tip, status->cmd_mcs, sizeof(cmd_tip));
        if (main_page_tool_length_mm(page, &tool_len) && fabs(tool_len) > 1.0e-9) {
            cmd_tip[2] -= tool_len;
        }
        main_page_fit_expand_world_point(&page->toolpath_fit, cmd_tip);
    }

    main_page_expand_static_geometry_fit(page, status);
}

static double monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static const char *main_page_wcs_code(const V5NativeReadback *readback)
{
    static const char *const names[] = {"G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"};
    if (!v5_native_readback_wcs_known(readback) || readback->wcs_index < 0 || readback->wcs_index > 8) {
        return "--";
    }
    return names[readback->wcs_index];
}

static void append_main_page_modal_text(char *out, size_t out_size, const char *text)
{
    size_t used;
    if (!out || out_size == 0U || !text) {
        return;
    }
    used = strlen(out);
    if (used + 1U >= out_size) {
        return;
    }
    snprintf(out + used, out_size - used, "%s", text);
}

static void append_main_page_modal_tokens(char *out, size_t out_size, const char *modal_text)
{
    char token[24];
    size_t token_len = 0U;
    int emitted = 0;
    const char *p = modal_text;
    if (!p || !p[0]) {
        append_main_page_modal_text(out, out_size, "--");
        return;
    }
    while (*p) {
        if (isspace((unsigned char)*p)) {
            if (token_len > 0U) {
                token[token_len] = '\0';
                if (emitted) {
                    append_main_page_modal_text(out, out_size, "\n");
                }
                append_main_page_modal_text(out, out_size, token);
                emitted = 1;
                token_len = 0U;
            }
        } else if (token_len + 1U < sizeof(token)) {
            token[token_len++] = *p;
        }
        ++p;
    }
    if (token_len > 0U) {
        token[token_len] = '\0';
        if (emitted) {
            append_main_page_modal_text(out, out_size, "\n");
        }
        append_main_page_modal_text(out, out_size, token);
        emitted = 1;
    }
    if (!emitted) {
        append_main_page_modal_text(out, out_size, "--");
    }
}

static const char *main_page_rtcp_modal_text(const V5NativeReadback *readback)
{
    if (!v5_native_readback_rtcp_known(readback)) {
        return "RTCP --";
    }
    return readback->rtcp_enabled ? "RTCP ON" : "RTCP OFF";
}

static void format_main_page_modal(char *out, size_t out_size, const V5NativeReadback *readback)
{
    char line[32];
    if (!out || out_size == 0U) {
        return;
    }
    snprintf(out, out_size, "当前模态\n");
    if (v5_native_readback_tool_known(readback)) {
        snprintf(line, sizeof(line), "T%d", readback->tool_number);
        append_main_page_modal_text(out, out_size, line);
        append_main_page_modal_text(out, out_size, "\n");
        if (v5_native_readback_tool_length_known(readback)) {
            snprintf(line, sizeof(line), "L%.3f", readback->tool_length_mm);
        } else {
            snprintf(line, sizeof(line), "L--");
        }
        append_main_page_modal_text(out, out_size, line);
    } else {
        append_main_page_modal_text(out, out_size, "T--\nL--");
    }
    append_main_page_modal_text(out, out_size, "\n");
    append_main_page_modal_text(out, out_size, main_page_rtcp_modal_text(readback));
    append_main_page_modal_text(out, out_size, "\n");
    append_main_page_modal_tokens(out, out_size, v5_native_readback_modal_known(readback) ? readback->modal_text : "--");
}

static void update_main_page_modal_label(V5MainPage *page)
{
    char modal_display_text[256];
    if (!page || !page->modal_label) {
        return;
    }
    format_main_page_modal(modal_display_text, sizeof(modal_display_text), &page->native_readback);
    set_label_text_if_changed(page->modal_label, modal_display_text);
}

static void format_main_page_wcs_coordinate(char *out, size_t out_size, const V5UiStatusView *status,
                                            const V5NativeReadback *readback, unsigned int axis)
{
    double value;
    const double *active_offsets;

    if (!out || out_size == 0U) {
        return;
    }
    active_offsets = v5_native_readback_active_wcs_offsets(readback);
    if (!status || axis >= V5_MAIN_PAGE_AXIS_COUNT || axis >= V5_STATUS_AXIS_COUNT ||
        axis >= V5_NATIVE_READBACK_WCS_OFFSET_COUNT || (status->valid_mask & V5_STATUS_VALID_MCS) == 0U ||
        !isfinite(status->mcs[axis]) || !active_offsets || !isfinite(active_offsets[axis])) {
        snprintf(out, out_size, "--.---");
        return;
    }

    value = status->mcs[axis] - active_offsets[axis];
    if (value > -0.0005 && value < 0.0005) {
        value = 0.0;
    }
    snprintf(out, out_size, "%+010.3f", value);
}

static int format_toolpath_g53_ac_text(char *out, size_t out_size, const V5NativeReadback *readback)
{
    const double *a_center;
    const double *c_center;
    const double *active_offsets;
    if (!out || out_size == 0U || !readback) {
        return 0;
    }
    a_center = v5_native_readback_g53_center(readback, V5_NATIVE_READBACK_G53_CENTER_A);
    c_center = v5_native_readback_g53_center(readback, V5_NATIVE_READBACK_G53_CENTER_C);
    active_offsets = v5_native_readback_active_wcs_offsets(readback);
    if (!a_center || !c_center ||
        !isfinite(a_center[0]) || !isfinite(a_center[1]) || !isfinite(a_center[2]) ||
        !isfinite(c_center[0]) || !isfinite(c_center[1]) || !isfinite(c_center[2]) ||
        !active_offsets || !isfinite(active_offsets[0]) || !isfinite(active_offsets[2])) {
        return 0;
    }
    snprintf(
        out,
        out_size,
        "G53 A %.2f,%.2f,%.2f  C %.2f,%.2f,%.2f",
        active_offsets[0],
        a_center[1],
        a_center[2],
        c_center[0],
        c_center[1],
        active_offsets[2]);
    return 1;
}

static int format_toolpath_wcs_offset_text(char *out, size_t out_size, const V5NativeReadback *readback)
{
    const double *offsets;
    if (!out || out_size == 0U || !readback) {
        return 0;
    }
    offsets = v5_native_readback_active_wcs_offsets(readback);
    if (!offsets ||
        !isfinite(offsets[0]) || !isfinite(offsets[1]) || !isfinite(offsets[2]) ||
        !isfinite(offsets[3]) || !isfinite(offsets[4])) {
        return 0;
    }
    snprintf(
        out,
        out_size,
        "%s偏置 X%.2f Y%.2f Z%.2f A%.2f C%.2f",
        main_page_wcs_code(readback),
        offsets[0],
        offsets[1],
        offsets[2],
        offsets[3],
        offsets[4]);
    return 1;
}

static void update_toolpath_status_text(V5MainPage *page)
{
    char text[128];
    if (!page) {
        return;
    }
    if (page->toolpath_summary_label &&
        format_toolpath_g53_ac_text(text, sizeof(text), &page->native_readback)) {
        set_label_text_if_changed(page->toolpath_summary_label, text);
    }
    if (page->toolpath_detail_label &&
        format_toolpath_wcs_offset_text(text, sizeof(text), &page->native_readback)) {
        set_label_text_if_changed(page->toolpath_detail_label, text);
    }
}

static void update_main_page_wcs_header(V5MainPage *page)
{
    char text[24];
    if (!page || !page->wcs_header_label) {
        return;
    }
    snprintf(text, sizeof(text), "加工 %s", main_page_wcs_code(&page->native_readback));
    set_label_text_if_changed(page->wcs_header_label, text);
}

static void clear_obj_style(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}



static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *panel = lv_obj_create(parent);
    clear_obj_style(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    return panel;
}

static lv_obj_t *make_toolpath_v3_dot(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b, uint8_t br, uint8_t bg, uint8_t bb)
{
    lv_obj_t *dot = lv_obj_create(parent);
    clear_obj_style(dot);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, 4, 0);
    lv_obj_set_style_bg_color(dot, rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, rgb(br, bg, bb), 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return dot;
}

static lv_obj_t *make_toolpath_v3_center_dot(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *dot = lv_obj_create(parent);
    clear_obj_style(dot);
    lv_obj_set_size(dot, 2, 2);
    lv_obj_set_style_radius(dot, 1, 0);
    lv_obj_set_style_bg_color(dot, rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return dot;
}

static void set_toolpath_v3_dot_center(lv_obj_t *dot, const V5ToolpathScreenPoint *point, int valid)
{
    lv_coord_t x;
    lv_coord_t y;
    const lv_coord_t half = 3;
    if (!dot) {
        return;
    }
    if (!valid || !point) {
        add_hidden_flag_if_visible(dot);
        return;
    }
    x = V5_TOOLPATH_X + clamp_coord(point->x, half, V5_TOOLPATH_W - half);
    y = V5_TOOLPATH_Y + clamp_coord(point->y, half, V5_TOOLPATH_H - half);
    set_obj_pos_if_changed(dot, x - half, y - half);
    clear_hidden_flag_if_hidden(dot);
}


static void set_toolpath_v3_center_dot(lv_obj_t *dot, const V5ToolpathScreenPoint *point, int valid)
{
    lv_coord_t x;
    lv_coord_t y;
    const lv_coord_t half = 1;
    if (!dot) {
        return;
    }
    if (!valid || !point) {
        add_hidden_flag_if_visible(dot);
        return;
    }
    x = V5_TOOLPATH_X + clamp_coord(point->x, half, V5_TOOLPATH_W - half);
    y = V5_TOOLPATH_Y + clamp_coord(point->y, half, V5_TOOLPATH_H - half);
    set_obj_pos_if_changed(dot, x - half, y - half);
    clear_hidden_flag_if_hidden(dot);
}

static const char *toolpath_wcs_name_for_index(unsigned int index)
{
    static const char *const names[V5_MAIN_PAGE_TOOLPATH_WCS_COUNT] = {
        "G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"};
    return index < V5_MAIN_PAGE_TOOLPATH_WCS_COUNT ? names[index] : "";
}

static lv_obj_t *make_toolpath_v3_line(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b, uint8_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_size(line, V5_TOOLPATH_W, V5_TOOLPATH_H);
    lv_obj_set_style_line_color(line, rgb(r, g, b), 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
}

static void hide_toolpath_line(lv_obj_t *line)
{
    if (line && !lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
        lv_line_set_points(line, 0, 0);
    }
}

static lv_point_t toolpath_local_point(const V5ToolpathScreenPoint *point)
{
    lv_point_t out;
    out.x = clamp_coord(point ? point->x : 0.0, 0, V5_TOOLPATH_W);
    out.y = clamp_coord(point ? point->y : 0.0, 0, V5_TOOLPATH_H);
    return out;
}

static int clip_toolpath_segment(V5ToolpathScreenPoint *start, V5ToolpathScreenPoint *end);

static void set_toolpath_axis_line(lv_obj_t *line, lv_point_t points[2], const V5ToolpathScreenPoint *start, const V5ToolpathScreenPoint *end, int valid)
{
    V5ToolpathScreenPoint clipped_start;
    V5ToolpathScreenPoint clipped_end;
    if (!line || !points) {
        return;
    }
    if (!valid || !start || !end) {
        hide_toolpath_line(line);
        return;
    }
    clipped_start = *start;
    clipped_end = *end;
    if (!clip_toolpath_segment(&clipped_start, &clipped_end)) {
        hide_toolpath_line(line);
        return;
    }
    {
        lv_point_t next[2];
        next[0] = toolpath_local_point(&clipped_start);
        next[1] = toolpath_local_point(&clipped_end);
        if (lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN) || !points_equal(points, next, 2U)) {
            points[0] = next[0];
            points[1] = next[1];
            lv_line_set_points(line, points, 2);
        }
    }
    clear_hidden_flag_if_hidden(line);
}

static int main_page_tool_length_mm(const V5MainPage *page, double *out)
{
    if (!page || !out || !v5_native_readback_tool_length_known(&page->native_readback) ||
        !isfinite(page->native_readback.tool_length_mm)) {
        return 0;
    }
    *out = page->native_readback.tool_length_mm;
    return 1;
}

static int main_page_project_cmd_tip(const V5MainPage *page, const V5UiStatusView *status, V5ToolpathScreenPoint *point)
{
    double world[V5_STATUS_AXIS_COUNT];
    double tool_len = 0.0;
    if (!page || !status || !point || (status->valid_mask & V5_STATUS_VALID_CMD_MCS) == 0U ||
        !isfinite(status->cmd_mcs[0]) || !isfinite(status->cmd_mcs[1]) || !isfinite(status->cmd_mcs[2])) {
        return 0;
    }
    memcpy(world, status->cmd_mcs, sizeof(world));
    if (main_page_tool_length_mm(page, &tool_len)) {
        world[2] -= tool_len;
    }
    return main_page_project_world_point_transformed(page, world, point);
}

static void update_toolpath_holder_line(V5MainPage *page, const V5UiStatusView *status, const V5ToolpathScreenPoint *holder_point)
{
    double tool_len;
    double world[V5_STATUS_AXIS_COUNT];
    V5ToolpathScreenPoint holder_end;
    if (!page || !status || !holder_point || (status->valid_mask & V5_STATUS_VALID_MCS) == 0U ||
        !isfinite(status->mcs[0]) || !isfinite(status->mcs[1]) || !isfinite(status->mcs[2]) ||
        !main_page_tool_length_mm(page, &tool_len) || fabs(tool_len) <= 1.0e-9) {
        hide_toolpath_line(page ? page->toolpath_holder_line : 0);
        return;
    }
    memcpy(world, status->mcs, sizeof(world));
    world[2] -= tool_len;
    if (!main_page_project_world_point_transformed(page, world, &holder_end)) {
        hide_toolpath_line(page->toolpath_holder_line);
        return;
    }
    set_toolpath_axis_line(page->toolpath_holder_line, page->toolpath_holder_points, holder_point, &holder_end, 1);
}

static void set_toolpath_origin_cross(lv_obj_t *line, lv_point_t points[5], const V5ToolpathScreenPoint *origin, int valid)
{
    lv_coord_t x;
    lv_coord_t y;
    const lv_coord_t half = 3;
    if (!line || !points) {
        return;
    }
    if (!valid || !origin) {
        hide_toolpath_line(line);
        return;
    }
    x = clamp_coord(origin->x, half, V5_TOOLPATH_W - half);
    y = clamp_coord(origin->y, half, V5_TOOLPATH_H - half);
    {
        lv_point_t next[5];
        next[0].x = x;
        next[0].y = y - half;
        next[1].x = x + half;
        next[1].y = y;
        next[2].x = x;
        next[2].y = y + half;
        next[3].x = x - half;
        next[3].y = y;
        next[4].x = x;
        next[4].y = y - half;
        if (lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN) || !points_equal(points, next, 5U)) {
            memcpy(points, next, sizeof(next));
            lv_line_set_points(line, points, 5);
        }
    }
    clear_hidden_flag_if_hidden(line);
}

static void hide_toolpath_ac_geometry(V5MainPage *page)
{
    if (!page) {
        return;
    }
    hide_toolpath_line(page->toolpath_a_axis_line);
    hide_toolpath_line(page->toolpath_c_axis_line);
    hide_toolpath_line(page->toolpath_holder_line);
    add_hidden_flag_if_visible(page->toolpath_a_center_line);
    add_hidden_flag_if_visible(page->toolpath_c_center_line);
    add_hidden_flag_if_visible(page->toolpath_a_label);
    add_hidden_flag_if_visible(page->toolpath_c_label);
}


static void hide_toolpath_program_wcs_objects(V5MainPage *page)
{
    unsigned int wcs;
    unsigned int axis;
    if (!page) {
        return;
    }
    for (wcs = 0U; wcs < V5_MAIN_PAGE_TOOLPATH_WCS_COUNT; ++wcs) {
        hide_toolpath_line(page->toolpath_program_wcs_origin_lines[wcs]);
        for (axis = 0U; axis < 3U; ++axis) {
            hide_toolpath_line(page->toolpath_program_wcs_axis_lines[wcs][axis]);
        }
        if (page->toolpath_program_wcs_labels[wcs]) {
            set_label_text_if_changed(page->toolpath_program_wcs_labels[wcs], "");
            add_hidden_flag_if_visible(page->toolpath_program_wcs_labels[wcs]);
        }
    }
}


static V5ToolpathScreenPoint toolpath_scaffold_point(double x, double y)
{
    V5ToolpathScreenPoint point;
    point.x = x;
    point.y = y;
    return point;
}

static int clip_toolpath_segment(V5ToolpathScreenPoint *start, V5ToolpathScreenPoint *end)
{
    const double xmin = 0.0;
    const double ymin = 0.0;
    const double xmax = (double)V5_TOOLPATH_W;
    const double ymax = (double)V5_TOOLPATH_H;
    const double dx = end->x - start->x;
    const double dy = end->y - start->y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {start->x - xmin, xmax - start->x, start->y - ymin, ymax - start->y};
    double u1 = 0.0;
    double u2 = 1.0;
    int i;

    for (i = 0; i < 4; ++i) {
        if (fabs(p[i]) < 1.0e-9) {
            if (q[i] < 0.0) {
                return 0;
            }
        } else {
            const double r = q[i] / p[i];
            if (p[i] < 0.0) {
                if (r > u2) {
                    return 0;
                }
                if (r > u1) {
                    u1 = r;
                }
            } else {
                if (r < u1) {
                    return 0;
                }
                if (r < u2) {
                    u2 = r;
                }
            }
        }
    }

    if (u2 < 1.0) {
        end->x = start->x + (u2 * dx);
        end->y = start->y + (u2 * dy);
    }
    if (u1 > 0.0) {
        start->x = start->x + (u1 * dx);
        start->y = start->y + (u1 * dy);
    }
    return 1;
}

static void set_toolpath_ac_geometry_from_basis(
    V5MainPage *page,
    const V5ToolpathScreenPoint *origin,
    const V5ToolpathScreenPoint *x_axis,
    const V5ToolpathScreenPoint *y_axis,
    const V5ToolpathScreenPoint *z_axis,
    const V5ToolpathScreenPoint *a_center_override,
    const V5ToolpathScreenPoint *c_center_override,
    double a_deg,
    int a_valid)
{
    V5ToolpathScreenPoint ac_center[2];
    V5ToolpathScreenPoint ac_axis_start[2];
    V5ToolpathScreenPoint ac_axis_end[2];
    double angle = 0.0;
    double x_dx;
    double x_dy;
    double y_dx;
    double y_dy;
    double z_dx;
    double z_dy;
    double c_dx;
    double c_dy;

    if (!page || !origin || !x_axis || !y_axis || !z_axis || !a_center_override || !c_center_override) {
        hide_toolpath_ac_geometry(page);
        return;
    }

    if (a_valid && isfinite(a_deg)) {
        angle = a_deg * M_PI / 180.0;
    }
    x_dx = x_axis->x - origin->x;
    x_dy = x_axis->y - origin->y;
    y_dx = y_axis->x - origin->x;
    y_dy = y_axis->y - origin->y;
    z_dx = z_axis->x - origin->x;
    z_dy = z_axis->y - origin->y;
    c_dx = (-sin(angle) * y_dx) + (cos(angle) * z_dx);
    c_dy = (-sin(angle) * y_dy) + (cos(angle) * z_dy);

    ac_center[0] = *a_center_override;
    ac_axis_start[0] = toolpath_scaffold_point(ac_center[0].x - x_dx, ac_center[0].y - x_dy);
    ac_axis_end[0] = toolpath_scaffold_point(ac_center[0].x + x_dx, ac_center[0].y + x_dy);
    ac_center[1] = *c_center_override;
    ac_axis_start[1] = toolpath_scaffold_point(ac_center[1].x - c_dx, ac_center[1].y - c_dy);
    ac_axis_end[1] = toolpath_scaffold_point(ac_center[1].x + c_dx, ac_center[1].y + c_dy);

    set_toolpath_axis_line(
        page->toolpath_a_axis_line,
        page->toolpath_ac_axis_points[0],
        &ac_axis_start[0],
        &ac_axis_end[0],
        clip_toolpath_segment(&ac_axis_start[0], &ac_axis_end[0]));
    set_toolpath_axis_line(
        page->toolpath_c_axis_line,
        page->toolpath_ac_axis_points[1],
        &ac_axis_start[1],
        &ac_axis_end[1],
        clip_toolpath_segment(&ac_axis_start[1], &ac_axis_end[1]));
    set_toolpath_v3_center_dot(page->toolpath_a_center_line, &ac_center[0], 1);
    set_toolpath_v3_center_dot(page->toolpath_c_center_line, &ac_center[1], 1);
    if (page->toolpath_a_label) {
        set_label_text_if_changed(page->toolpath_a_label, "A");
        set_obj_pos_if_changed(page->toolpath_a_label, V5_TOOLPATH_X + clamp_coord(ac_axis_end[0].x + 4.0, 0, V5_TOOLPATH_W - 24), V5_TOOLPATH_Y + clamp_coord(ac_axis_end[0].y - 12.0, 0, V5_TOOLPATH_H - 22));
        clear_hidden_flag_if_hidden(page->toolpath_a_label);
    }
    if (page->toolpath_c_label) {
        set_label_text_if_changed(page->toolpath_c_label, "C");
        set_obj_pos_if_changed(page->toolpath_c_label, V5_TOOLPATH_X + clamp_coord(ac_axis_end[1].x + 8.0, 0, V5_TOOLPATH_W - 20), V5_TOOLPATH_Y + clamp_coord(ac_axis_end[1].y - 10.0, 0, V5_TOOLPATH_H - 22));
        clear_hidden_flag_if_hidden(page->toolpath_c_label);
    }
}

static void draw_toolpath_boot_scaffold(V5MainPage *page)
{
    const double wcs_origin_x = 248.0;
    const double wcs_origin_y = 318.0;
    const double mcs_origin_x = 244.0;
    const double mcs_origin_y = 315.0;
    const double scaffold_axis_scale = 40.0;
    const double basis_right_x = 0.7071067811865476;
    const double basis_right_y = 0.7071067811865476;
    const double basis_right_z = 0.0;
    const double basis_up_x = -0.4082482904638631;
    const double basis_up_y = 0.4082482904638631;
    const double basis_up_z = 0.8164965809277261;
    const double mcs_axis_x[3] = {
        mcs_origin_x + (basis_right_x * scaffold_axis_scale),
        mcs_origin_x + (basis_right_y * scaffold_axis_scale),
        mcs_origin_x + (basis_right_z * scaffold_axis_scale)};
    const double mcs_axis_y[3] = {
        mcs_origin_y - (basis_up_x * scaffold_axis_scale),
        mcs_origin_y - (basis_up_y * scaffold_axis_scale),
        mcs_origin_y - (basis_up_z * scaffold_axis_scale)};
    V5ToolpathScreenPoint wcs_origin;
    V5ToolpathScreenPoint mcs_origin;
    V5ToolpathScreenPoint wcs_axis[3];
    V5ToolpathScreenPoint mcs_axis[3];
    unsigned int i;

    if (!page) {
        return;
    }

    wcs_origin = apply_toolpath_view_transform(page, toolpath_scaffold_point(wcs_origin_x, wcs_origin_y));
    mcs_origin = apply_toolpath_view_transform(page, toolpath_scaffold_point(mcs_origin_x, mcs_origin_y));
    for (i = 0U; i < 3U; ++i) {
        const double axis_dx = mcs_axis_x[i] - mcs_origin_x;
        const double axis_dy = mcs_axis_y[i] - mcs_origin_y;
        wcs_axis[i] = apply_toolpath_view_transform(page, toolpath_scaffold_point(wcs_origin_x + axis_dx, wcs_origin_y + axis_dy));
        mcs_axis[i] = apply_toolpath_view_transform(page, toolpath_scaffold_point(mcs_axis_x[i], mcs_axis_y[i]));
    }

    set_toolpath_origin_cross(page->toolpath_wcs_origin_line, page->toolpath_wcs_origin_points, &wcs_origin, 1);
    set_toolpath_origin_cross(page->toolpath_mcs_origin_line, page->toolpath_mcs_origin_points, &mcs_origin, 1);
    for (i = 0U; i < 3U; ++i) {
        set_toolpath_axis_line(page->toolpath_wcs_axis_lines[i], page->toolpath_wcs_axis_points[i], &wcs_origin, &wcs_axis[i], 1);
        set_toolpath_axis_line(page->toolpath_mcs_axis_lines[i], page->toolpath_mcs_axis_points[i], &mcs_origin, &mcs_axis[i], 1);
    }

    hide_toolpath_program_wcs_objects(page);

    hide_toolpath_ac_geometry(page);

    set_toolpath_v3_dot_center(page->toolpath_microkernel_marker_dot, &wcs_origin, 1);
    set_toolpath_v3_dot_center(page->toolpath_holder_marker_line, &mcs_origin, 1);

    if (page->toolpath_mcs_label) {
        set_label_text_if_changed(page->toolpath_mcs_label, "MCS");
        clear_hidden_flag_if_hidden(page->toolpath_mcs_label);
    }
    if (page->toolpath_wcs_label) {
        set_label_text_if_changed(page->toolpath_wcs_label, "WCS");
        set_obj_pos_if_changed(page->toolpath_wcs_label, 18, 326);
        clear_hidden_flag_if_hidden(page->toolpath_wcs_label);
    }
    for (i = 0U; i < 3U; ++i) {
        if (page->toolpath_mcs_axis_labels[i]) {
            set_label_text_if_changed(page->toolpath_mcs_axis_labels[i], "");
            clear_hidden_flag_if_hidden(page->toolpath_mcs_axis_labels[i]);
        }
    }
}

static void update_toolpath_state_lines(V5MainPage *page, const V5UiStatusView *status)
{
    V5ToolpathScreenPoint origin_point;
    V5ToolpathScreenPoint axis_point[3];
    V5ToolpathScreenPoint wcs_origin_point;
    V5ToolpathScreenPoint wcs_axis_point[3];
    double origin[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double axis_world[3][V5_STATUS_AXIS_COUNT] = {
        {40.0, 0.0, 0.0, 0.0, 0.0},
        {0.0, 40.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 40.0, 0.0, 0.0},
    };
    double wcs_origin[V5_STATUS_AXIS_COUNT];
    double wcs_axis[3][V5_STATUS_AXIS_COUNT];
    int mcs_valid;
    int wcs_valid;
    int wcs_projection_valid = 1;
    int axis_ok[3] = {0, 0, 0};
    unsigned int i;

    if (!page) {
        return;
    }
    mcs_valid = status && (status->valid_mask & V5_STATUS_VALID_MCS) != 0u && page->toolpath_fit.valid;
    if (!mcs_valid ||
        !v5_toolpath_display_project_world_point(origin, &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, &origin_point)) {
        draw_toolpath_boot_scaffold(page);
        return;
    } else {
        origin_point = apply_toolpath_view_transform(page, origin_point);
        set_toolpath_origin_cross(page->toolpath_mcs_origin_line, page->toolpath_mcs_origin_points, &origin_point, 1);
        for (i = 0U; i < 3U; ++i) {
            int ok = v5_toolpath_display_project_world_point(axis_world[i], &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, &axis_point[i]);
            if (ok) {
                axis_point[i] = apply_toolpath_view_transform(page, axis_point[i]);
                axis_ok[i] = 1;
            }
            set_toolpath_axis_line(page->toolpath_mcs_axis_lines[i], page->toolpath_mcs_axis_points[i], &origin_point, &axis_point[i], ok);
        }
        if (axis_ok[0] && axis_ok[1] && axis_ok[2]) {
            V5ToolpathScreenPoint a_center_point;
            V5ToolpathScreenPoint c_center_point;
            double a_center_world[V5_STATUS_AXIS_COUNT];
            double c_center_world[V5_STATUS_AXIS_COUNT];
            if (main_page_g53_ac_center_world(page, 0U, a_center_world) &&
                main_page_project_world_point_transformed(page, a_center_world, &a_center_point) &&
                main_page_g53_ac_center_world(page, 1U, c_center_world) &&
                main_page_project_world_point_transformed(page, c_center_world, &c_center_point)) {
                set_toolpath_ac_geometry_from_basis(
                    page,
                    &origin_point,
                    &axis_point[0],
                    &axis_point[1],
                    &axis_point[2],
                    &a_center_point,
                    &c_center_point,
                    status->mcs[3],
                    1);
            } else {
                hide_toolpath_ac_geometry(page);
            }
        } else {
            hide_toolpath_ac_geometry(page);
        }
    }

    hide_toolpath_program_wcs_objects(page);
    wcs_valid = mcs_valid && v5_native_readback_wcs_offset_known(&page->native_readback);
    if (!wcs_valid) {
        if (page->toolpath_wcs_label) {
            set_label_text_if_changed(page->toolpath_wcs_label, "WCS");
            clear_hidden_flag_if_hidden(page->toolpath_wcs_label);
        }
    } else {
        const double *active_offsets = v5_native_readback_active_wcs_offsets(&page->native_readback);
        if (!active_offsets) {
            return;
        }
        memset(wcs_origin, 0, sizeof(wcs_origin));
        for (i = 0U; i < V5_STATUS_AXIS_COUNT && i < V5_NATIVE_READBACK_WCS_OFFSET_COUNT; ++i) {
            wcs_origin[i] = active_offsets[i];
        }
        for (i = 0U; i < 3U; ++i) {
            memcpy(wcs_axis[i], wcs_origin, sizeof(wcs_axis[i]));
            wcs_axis[i][i] += 40.0;
        }
        if (main_page_rtcp_rotates_current_wcs(page)) {
            wcs_projection_valid = main_page_apply_ac_projection_to_world(page, status, wcs_origin);
            for (i = 0U; i < 3U && wcs_projection_valid; ++i) {
                wcs_projection_valid = main_page_apply_ac_projection_to_world(page, status, wcs_axis[i]);
            }
        }
        if (!wcs_projection_valid) {
            hide_toolpath_line(page->toolpath_wcs_origin_line);
            for (i = 0U; i < 3U; ++i) {
                hide_toolpath_line(page->toolpath_wcs_axis_lines[i]);
            }
            if (page->toolpath_wcs_label) {
                set_label_text_if_changed(page->toolpath_wcs_label, "WCS");
                clear_hidden_flag_if_hidden(page->toolpath_wcs_label);
            }
            return;
        }
        if (v5_toolpath_display_project_world_point(wcs_origin, &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, &wcs_origin_point)) {
            wcs_origin_point = apply_toolpath_view_transform(page, wcs_origin_point);
            set_toolpath_origin_cross(page->toolpath_wcs_origin_line, page->toolpath_wcs_origin_points, &wcs_origin_point, 1);
            if (page->toolpath_wcs_label) {
                set_obj_pos_if_changed(
                    page->toolpath_wcs_label,
                    V5_TOOLPATH_X + clamp_coord(wcs_origin_point.x + 5.0, 0, V5_TOOLPATH_W - 36),
                    V5_TOOLPATH_Y + clamp_coord(wcs_origin_point.y - 14.0, 0, V5_TOOLPATH_H - 18));
                set_label_text_if_changed(page->toolpath_wcs_label, main_page_wcs_code(&page->native_readback));
                clear_hidden_flag_if_hidden(page->toolpath_wcs_label);
            }
            for (i = 0U; i < 3U; ++i) {
                int ok = v5_toolpath_display_project_world_point(wcs_axis[i], &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, &wcs_axis_point[i]);
                if (ok) {
                    wcs_axis_point[i] = apply_toolpath_view_transform(page, wcs_axis_point[i]);
                }
                set_toolpath_axis_line(page->toolpath_wcs_axis_lines[i], page->toolpath_wcs_axis_points[i], &wcs_origin_point, &wcs_axis_point[i], ok);
            }
        }
    }
}

static int main_page_apply_program_preview_wcs_offset(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    V5StatusPoint *points,
    unsigned int count,
    int *wcs_index_out,
    double wcs_offset_out[3])
{
    double offset[3];
    int resolved_wcs_index = -1;
    unsigned int i;

    if (wcs_index_out) {
        *wcs_index_out = -1;
    }
    if (wcs_offset_out) {
        memset(wcs_offset_out, 0, sizeof(double) * 3U);
    }
    if (!page || !runtime || !points || count == 0U ||
        !v5_native_readback_wcs_table_known(&page->native_readback)) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        int point_wcs_index = -1;
        if (!v5_program_runtime_preview_wcs_index(runtime, i, &point_wcs_index) ||
            point_wcs_index < 0 || point_wcs_index >= (int)V5_NATIVE_READBACK_WCS_COUNT) {
            return 0;
        }
        if (resolved_wcs_index < 0) {
            resolved_wcs_index = point_wcs_index;
        } else if (resolved_wcs_index != point_wcs_index) {
            return 0;
        }
    }
    if (resolved_wcs_index < 0) {
        return 0;
    }
    for (i = 0U; i < 3U; ++i) {
        if (!isfinite(page->native_readback.wcs_offsets[resolved_wcs_index][i])) {
            return 0;
        }
        offset[i] = page->native_readback.wcs_offsets[resolved_wcs_index][i];
    }
    for (i = 0U; i < count; ++i) {
        points[i].axis[0] += offset[0];
        points[i].axis[1] += offset[1];
        points[i].axis[2] += offset[2];
    }
    if (wcs_index_out) {
        *wcs_index_out = resolved_wcs_index;
    }
    if (wcs_offset_out) {
        wcs_offset_out[0] = offset[0];
        wcs_offset_out[1] = offset[1];
        wcs_offset_out[2] = offset[2];
    }
    return 1;
}

static void hide_toolpath_program_line(V5MainPage *page)
{
    unsigned int segment;
    if (!page || !page->trajectory_line) {
        return;
    }
    page->toolpath_program_wcs_valid = 0;
    page->toolpath_program_wcs_index = -1;
    memset(page->toolpath_program_wcs_offset, 0, sizeof(page->toolpath_program_wcs_offset));
    page->toolpath_program_point_count = 0U;
    page->toolpath_program_ac_valid = 0;
    page->toolpath_program_ac_a_deg = 0.0;
    page->toolpath_program_ac_c_deg = 0.0;
    for (segment = 0U; segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS; ++segment) {
        hide_toolpath_line(page->toolpath_line_segments[segment]);
    }
    if (page->toolpath_program_visible) {
        page->toolpath_program_visible = 0;
        page->trajectory_point_count = 0U;
        page->toolpath_line_rewrite_count += 1U;
    }
}

static void mark_toolpath_static_dirty(V5MainPage *page)
{
    if (!page) {
        return;
    }
    page->toolpath_program_generation = 0U;
    page->toolpath_program_view_generation = 0U;
    page->toolpath_program_wcs_valid = 0;
    page->toolpath_program_wcs_index = -1;
    memset(page->toolpath_program_wcs_offset, 0, sizeof(page->toolpath_program_wcs_offset));
    page->toolpath_program_visible = 0;
    page->toolpath_program_point_count = 0U;
    page->toolpath_program_ac_valid = 0;
    page->toolpath_program_ac_a_deg = 0.0;
    page->toolpath_program_ac_c_deg = 0.0;
    v5_toolpath_display_fit_init(&page->toolpath_fit);
}

static void reset_toolpath_view_rotation(V5MainPage *page)
{
    if (!page) {
        return;
    }
    page->toolpath_gesture_active_count = 0;
    page->toolpath_gesture_last_distance = 0.0;
    page->toolpath_gesture_last_angle_deg = 0.0;
    page->toolpath_manual_rotate_deg = 0.0;
}

static void set_toolpath_program_line(
    V5MainPage *page,
    const V5ToolpathScreenPoint *screen_points,
    unsigned int point_count)
{
    unsigned int i;
    unsigned int segment = 0U;
    unsigned int start = 0U;
    if (!page || !page->trajectory_line || !screen_points || point_count == 0U) {
        hide_toolpath_program_line(page);
        return;
    }
    page->trajectory_point_count = point_count;
    if (page->trajectory_point_count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        page->trajectory_point_count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    for (i = 0U; i < page->trajectory_point_count; ++i) {
        page->trajectory_points[i].x = clamp_coord(screen_points[i].x, 0, V5_TOOLPATH_W);
        page->trajectory_points[i].y = clamp_coord(screen_points[i].y, 0, V5_TOOLPATH_H);
    }
    while (start < page->trajectory_point_count && segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS) {
        unsigned int local_count = 0U;
        unsigned int limit = V5_MAIN_PAGE_TOOLPATH_SEGMENT_POINT_COUNT;
        while (start + local_count < page->trajectory_point_count && local_count < limit) {
            page->toolpath_segment_points[segment][local_count] = page->trajectory_points[start + local_count];
            ++local_count;
        }
        if (local_count >= 2U) {
            lv_line_set_points(
                page->toolpath_line_segments[segment],
                page->toolpath_segment_points[segment],
                (uint16_t)local_count);
            lv_obj_clear_flag(page->toolpath_line_segments[segment], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(page->toolpath_line_segments[segment]);
        } else {
            hide_toolpath_line(page->toolpath_line_segments[segment]);
        }
        if (start + local_count >= page->trajectory_point_count) {
            ++segment;
            break;
        }
        start += local_count > 1U ? local_count - 1U : local_count;
        ++segment;
    }
    for (; segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS; ++segment) {
        hide_toolpath_line(page->toolpath_line_segments[segment]);
    }
    if (page->toolpath_holder_line) {
        lv_obj_move_foreground(page->toolpath_holder_line);
    }
    page->toolpath_program_visible = 1;
    page->toolpath_line_rewrite_count += 1U;
}

static void make_divider(lv_obj_t *parent, int x, int y, int w, int h)
{
    make_panel(parent, x, y, w, h, 33, 72, 98);
}

static lv_obj_t *make_label_ex(lv_obj_t *parent, int x, int y, int w, int h, const char *text, uint8_t r, uint8_t g, uint8_t b, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text ? text : "--");
    lv_obj_set_style_text_color(label, rgb(r, g, b), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}


static void set_program_preview_row(V5MainPage *page, unsigned int row, const char *text, int highlighted, int has_text)
{
    if (!page || row >= V5_PROGRAM_PREVIEW_ROWS) {
        return;
    }
    if (page->program_line_bg[row]) {
        set_obj_bg_color_if_changed(
            page->program_line_bg[row],
            highlighted ? rgb(43, 133, 83) : rgb(7, 31, 48),
            0);
    }
    if (page->program_line_labels[row]) {
        set_label_text_if_changed(page->program_line_labels[row], text ? text : "");
        if (highlighted || has_text) {
            set_obj_text_color_if_changed(
                page->program_line_labels[row],
                highlighted ? rgb(226, 238, 246) : rgb(156, 178, 202),
                0);
        } else {
            set_obj_text_color_if_changed(page->program_line_labels[row], rgb(95, 116, 138), 0);
        }
    }
}

static int trim_line(char *out, size_t out_size, const char *start, size_t len)
{
    if (!out || out_size == 0U || !start) {
        return 0;
    }
    while (len > 0U && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
        ++start;
        --len;
    }
    while (len > 0U &&
           (start[len - 1U] == ' ' || start[len - 1U] == '\t' || start[len - 1U] == '\r' || start[len - 1U] == '\n')) {
        --len;
    }
    if (len >= out_size) {
        len = out_size - 1U;
    }
    if (len > 0U) {
        memcpy(out, start, len);
    }
    out[len] = '\0';
    return len > 0U;
}

static unsigned int count_preview_source_lines(const char *text)
{
    const char *cursor;
    const char *segment;
    unsigned int count = 0U;
    if (!text || !text[0]) {
        return 0U;
    }
    cursor = text;
    segment = text;
    while (*cursor) {
        if (*cursor == '\n' || *cursor == '\r') {
            ++count;
            if (*cursor == '\r' && cursor[1] == '\n') {
                ++cursor;
            }
            segment = cursor + 1;
        }
        ++cursor;
    }
    if (segment && *segment) {
        ++count;
    }
    return count;
}

static int readback_execution_line_active(const V5NativeReadback *readback)
{
    if (!readback) {
        return 0;
    }
    if (v5_native_readback_safety_estop_known(readback) && readback->safety_estop_active) {
        return 0;
    }
    if (v5_native_readback_machine_enable_known(readback) && !readback->machine_enabled) {
        return 0;
    }
    if (v5_native_readback_interpreter_idle_known(readback) &&
        readback->interpreter_idle && !readback->interpreter_paused) {
        return 0;
    }
    return 1;
}

static int active_preview_line_from_readback(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    const char **native_command_out)
{
    const V5NativeReadback *readback;
    if (native_command_out) {
        *native_command_out = "";
    }
    if (!page || !runtime) {
        return 0;
    }
    readback = &page->native_readback;
    if (!readback_execution_line_active(readback)) {
        return 0;
    }
    if (v5_program_runtime_has_mdi(runtime) &&
        v5_native_readback_mdi_run_known(readback) &&
        readback->mdi_run_active && readback->mdi_run_line > 0) {
        if (native_command_out) {
            *native_command_out = readback->mdi_run_command;
        }
        return readback->mdi_run_line;
    }
    if (v5_native_readback_interpreter_known(readback) && readback->interpreter_paused) {
        if (v5_native_readback_current_line_known(readback) && readback->current_line > 0) {
            return readback->current_line;
        }
        if (v5_native_readback_motion_line_known(readback) && readback->motion_line > 0) {
            return readback->motion_line;
        }
        return 0;
    }
    if (v5_native_readback_current_line_known(readback) && readback->current_line > 0) {
        return readback->current_line;
    }
    if (v5_native_readback_motion_line_known(readback) && readback->motion_line > 0) {
        return readback->motion_line;
    }
    return 0;
}

static unsigned int preview_start_line(unsigned int total, int active)
{
    unsigned int start = 1U;
    if (active > (int)V5_PROGRAM_PREVIEW_ROWS) {
        start = (unsigned int)active - (V5_PROGRAM_PREVIEW_ROWS - 1U);
    }
    if (total > V5_PROGRAM_PREVIEW_ROWS) {
        unsigned int max_start = total - (V5_PROGRAM_PREVIEW_ROWS - 1U);
        if (start > max_start) {
            start = max_start;
        }
    }
    if (start < 1U) {
        start = 1U;
    }
    return start;
}

static void refresh_program_preview_from_text(
    V5MainPage *page,
    const char *text,
    const char *native_command,
    unsigned int total,
    int active)
{
    unsigned int row;
    unsigned int start_line;
    int shown[V5_PROGRAM_PREVIEW_ROWS] = {0, 0, 0, 0};
    const char *segment;
    const char *cursor;
    unsigned int source_line = 0U;

    if (active < 0 || (total > 0U && (unsigned int)active > total && (!native_command || !native_command[0]))) {
        active = 0;
    }
    start_line = preview_start_line(total, active);
    if (!text || !text[0]) {
        for (row = 0U; row < V5_PROGRAM_PREVIEW_ROWS; ++row) {
            set_program_preview_row(page, row, "", 0, 0);
        }
        return;
    }
    segment = text;
    cursor = text;
    while (*cursor) {
        if (*cursor == '\n' || *cursor == '\r') {
            char code[120];
            char line[192];
            ++source_line;
            (void)trim_line(code, sizeof(code), segment, (size_t)(cursor - segment));
            if (source_line >= start_line && source_line < start_line + V5_PROGRAM_PREVIEW_ROWS) {
                row = source_line - start_line;
                snprintf(line, sizeof(line), "%03u %s", source_line, code);
                set_program_preview_row(page, row, line, active == (int)source_line, 1);
                shown[row] = 1;
            }
            if (*cursor == '\r' && cursor[1] == '\n') {
                ++cursor;
            }
            segment = cursor + 1;
        }
        ++cursor;
    }
    if (segment && *segment) {
        char code[120];
        char line[192];
        ++source_line;
        (void)trim_line(code, sizeof(code), segment, strlen(segment));
        if (source_line >= start_line && source_line < start_line + V5_PROGRAM_PREVIEW_ROWS) {
            row = source_line - start_line;
            snprintf(line, sizeof(line), "%03u %s", source_line, code);
            set_program_preview_row(page, row, line, active == (int)source_line, 1);
            shown[row] = 1;
        }
    }
    if (active > 0 && (unsigned int)active >= start_line &&
        (unsigned int)active < start_line + V5_PROGRAM_PREVIEW_ROWS) {
        row = (unsigned int)active - start_line;
        if (!shown[row] && native_command && native_command[0]) {
            char line[192];
            snprintf(line, sizeof(line), "%03d %s", active, native_command);
            set_program_preview_row(page, row, line, 1, 1);
            shown[row] = 1;
        }
    }
    for (row = 0U; row < V5_PROGRAM_PREVIEW_ROWS; ++row) {
        if (!shown[row]) {
            set_program_preview_row(page, row, "", 0, 0);
        }
    }
}

static void refresh_program_preview_rows(V5MainPage *page, const V5ProgramRuntime *runtime)
{
    unsigned int row;
    int active;
    const char *native_command = "";
    if (!page) {
        return;
    }
    if (!runtime) {
        for (row = 0U; row < V5_PROGRAM_PREVIEW_ROWS; ++row) {
            set_program_preview_row(page, row, "", 0, 0);
        }
        return;
    }
    active = active_preview_line_from_readback(page, runtime, &native_command);
    if (v5_program_runtime_has_mdi(runtime)) {
        const char *text = v5_program_runtime_mdi_text(runtime);
        unsigned int total = count_preview_source_lines(text);
        refresh_program_preview_from_text(page, text, native_command, total, active);
        return;
    }
    if (v5_program_runtime_has_open_program(runtime) && runtime->gcode_text) {
        unsigned int total = count_preview_source_lines(runtime->gcode_text);
        refresh_program_preview_from_text(page, runtime->gcode_text, native_command, total, active);
        return;
    }
    for (row = 0U; row < V5_PROGRAM_PREVIEW_ROWS; ++row) {
        set_program_preview_row(page, row, "", 0, 0);
    }
}

static void write_json_text(FILE *fp, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    fputc('"', fp);
    while (*p) {
        if (*p == '"' || *p == '\\') {
            fputc('_', fp);
        } else if (*p >= 32U && *p < 127U) {
            fputc((int)*p, fp);
        }
        ++p;
    }
    fputc('"', fp);
}

static void log_button_event(V5MainPageActionKind action, int ok, const V5MainPageActionReport *report)
{
    const char *path = "/run/8ax_v5_product_ui/ui_events.jsonl";
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen(path, "ab");
    if (!fp) {
        return;
    }
    int layout_only = report && report->command.name && strcmp(report->command.name, "layout_only") == 0;
    int implemented = ok && report && !layout_only && (report->prepared || report->local_only);
    fprintf(fp, "{\"schema\":\"v5.ui_event.v1\",\"source\":\"v5_lvgl_shell\",\"time_monotonic_s\":%.6f,\"event\":\"button_clicked\",\"action\":", monotonic_seconds());
    write_json_text(fp, v5_main_page_action_label(action));
    fprintf(fp, ",\"ok\":%s,\"implemented\":%s,\"layout_only\":%s", ok ? "true" : "false", implemented ? "true" : "false", layout_only ? "true" : "false");
    if (report) {
        fprintf(fp, ",\"prepared\":%s,\"local_only\":%s,\"executed\":%s,\"send_status\":%d,\"machine_on_requested\":%s,\"machine_on_status\":%d,\"request_kind\":%d,\"command_kind\":%d,\"axis_value\":%.3f,\"command_name\":", report->prepared ? "true" : "false", report->local_only ? "true" : "false", report->executed ? "true" : "false", report->send_status, report->machine_on_requested ? "true" : "false", report->machine_on_status, (int)report->request.kind, (int)report->command.kind, report->request.axis_value);
        write_json_text(fp, report->command.name);
        fprintf(fp, ",\"command_owner\":");
        write_json_text(fp, report->command.owner);
        fprintf(fp, ",\"command_line\":");
        write_json_text(fp, report->command_line);
        fprintf(fp, ",\"readback_code\":");
        write_json_text(fp, report->readback_code);
    }
    fprintf(fp, "}\n");
    fclose(fp);
}


static lv_color_t main_coordinate_digit_color(const V5MainPage *page, unsigned int axis, int is_wcs)
{
    int selected = 0;
    if (page && axis < V5_MAIN_PAGE_AXIS_COUNT) {
        if (is_wcs) {
            selected = page->selection.space == V5_MAIN_PAGE_SELECT_WCS &&
                       !page->selection.all_axes &&
                       page->selection.axis == page->wcs_targets[axis].axis;
        } else {
            selected = page->selection.space == V5_MAIN_PAGE_SELECT_MCS &&
                       !page->selection.all_axes &&
                       page->selection.axis == page->mcs_targets[axis].axis;
        }
    }
    if (selected) {
        return rgb(245, 214, 82);
    }
    return is_wcs ? rgb(68, 221, 144) : rgb(88, 204, 255);
}

static void refresh_main_coordinate_digits(V5MainPage *page)
{
    unsigned int i;
    if (!page || !page->coordinate_digits.canvas) {
        return;
    }
    for (i = 0U; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        const char *mcs_text = page->coordinate_digits.value_valid[0][i]
            ? page->coordinate_digits.value_text[0][i]
            : (page->mcs_labels[i] ? lv_label_get_text(page->mcs_labels[i]) : "");
        const char *cmd_text = page->coordinate_digits.value_valid[1][i]
            ? page->coordinate_digits.value_text[1][i]
            : (page->cmd_labels[i] ? lv_label_get_text(page->cmd_labels[i]) : "");
        if (mcs_text) {
            v5_coordinate_digits_set_value(
                &page->coordinate_digits,
                0U,
                i,
                mcs_text,
                main_coordinate_digit_color(page, i, 0));
        }
        if (cmd_text) {
            v5_coordinate_digits_set_value(
                &page->coordinate_digits,
                1U,
                i,
                cmd_text,
                main_coordinate_digit_color(page, i, 1));
        }
    }
}

static void update_coordinate_selection_style(V5MainPage *page)
{
    unsigned int i;
    if (!page) {
        return;
    }
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        int mcs_selected = page->selection.space == V5_MAIN_PAGE_SELECT_MCS && !page->selection.all_axes && page->selection.axis == page->mcs_targets[i].axis;
        int wcs_selected = page->selection.space == V5_MAIN_PAGE_SELECT_WCS && !page->selection.all_axes && page->selection.axis == page->wcs_targets[i].axis;
        if (page->mcs_labels[i]) {
            lv_obj_set_style_bg_opa(page->mcs_labels[i], mcs_selected ? LV_OPA_40 : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(page->mcs_labels[i], rgb(245, 214, 82), 0);
        }
        if (page->cmd_labels[i]) {
            lv_obj_set_style_bg_opa(page->cmd_labels[i], wcs_selected ? LV_OPA_40 : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(page->cmd_labels[i], rgb(245, 214, 82), 0);
        }
    }
    refresh_main_coordinate_digits(page);
}

static void log_coordinate_select_event(const V5MainPage *page)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/ui_events.jsonl", "ab");
    if (!fp || !page) {
        if (fp) fclose(fp);
        return;
    }
    fprintf(fp,
            "{\"schema\":\"v5.ui_event.v1\",\"source\":\"v5_lvgl_shell\",\"time_monotonic_s\":%.6f,\"event\":\"coordinate_selected\",\"space\":\"%s\",\"axis\":\"%c\",\"all_axes\":%s}\n",
            monotonic_seconds(),
            page->selection.space == V5_MAIN_PAGE_SELECT_WCS ? "wcs" : page->selection.space == V5_MAIN_PAGE_SELECT_MCS ? "mcs" : "none",
            page->selection.axis ? page->selection.axis : '-',
            page->selection.all_axes ? "true" : "false");
    fclose(fp);
}

static void coordinate_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    unsigned int i;
    if (!page || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        if (page->mcs_labels[i] == target) {
            (void)v5_main_page_select_axis(page, V5_MAIN_PAGE_SELECT_MCS, page->mcs_targets[i].axis);
            log_coordinate_select_event(page);
            return;
        }
        if (page->cmd_labels[i] == target) {
            (void)v5_main_page_select_axis(page, V5_MAIN_PAGE_SELECT_WCS, page->wcs_targets[i].axis);
            log_coordinate_select_event(page);
            return;
        }
    }
}

static void make_coordinate_value_clickable(V5MainPage *page, lv_obj_t *label)
{
    if (!page || !label) {
        return;
    }
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(label, rgb(245, 214, 82), 0);
    lv_obj_add_event_cb(label, coordinate_event_cb, LV_EVENT_CLICKED, page);
}

static void trigger_value_reset_action(V5MainPage *page, V5MainPageActionKind action)
{
    V5MainPageActionReport report;
    int ok;
    if (!page) {
        return;
    }
    ok = v5_main_page_trigger_action(page, action, &report);
    log_button_event(action, ok, ok ? &report : 0);
}

static void spindle_override_reset_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        trigger_value_reset_action((V5MainPage *)lv_event_get_user_data(event), V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100);
    }
}

static void feed_override_reset_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        trigger_value_reset_action((V5MainPage *)lv_event_get_user_data(event), V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100);
    }
}

static void make_override_reset_hit(V5MainPage *page, int x, int y, int w, int h, lv_event_cb_t cb)
{
    lv_obj_t *hit;
    if (!page || !page->root || !cb) {
        return;
    }
    hit = lv_obj_create(page->root);
    clear_obj_style(hit);
    lv_obj_set_pos(hit, x, y);
    lv_obj_set_size(hit, w, h);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, cb, LV_EVENT_CLICKED, page);
    lv_obj_move_foreground(hit);
}

static void main_program_edit_hit_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    uint32_t now;
    if (!page || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    now = lv_tick_get();
    if (page->program_edit_last_click_tick != 0U &&
        (uint32_t)(now - page->program_edit_last_click_tick) <= 550U) {
        V5MainPageActionReport report;
        int ok;
        page->program_edit_last_click_tick = 0U;
        ok = v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT, &report);
        log_button_event(V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT, ok, ok ? &report : 0);
        return;
    }
    page->program_edit_last_click_tick = now;
}

static void create_main_program_edit_hit_area(V5MainPage *page)
{
    lv_obj_t *hit;
    if (!page || !page->root) {
        return;
    }
    hit = lv_obj_create(page->root);
    clear_obj_style(hit);
    lv_obj_set_pos(hit, 0, 441);
    lv_obj_set_size(hit, 560, 154);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hit, main_program_edit_hit_event_cb, LV_EVENT_CLICKED, page);
    lv_obj_move_foreground(hit);
    page->program_edit_hit_area = hit;
}

static void clear_button_pressed_visual_now(lv_obj_t *button)
{
    if (!button) {
        return;
    }
    lv_obj_clear_state(button, LV_STATE_PRESSED);
    lv_obj_invalidate(button);
    lv_refr_now(NULL);
}

static void button_release_visual_cb(lv_event_t *event)
{
    clear_button_pressed_visual_now(lv_event_get_target(event));
}

static int action_needs_native_readback_refresh(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_START:
    case V5_MAIN_PAGE_ACTION_PAUSE:
    case V5_MAIN_PAGE_ACTION_RESUME:
    case V5_MAIN_PAGE_ACTION_ESTOP_FORCE:
    case V5_MAIN_PAGE_ACTION_ESTOP_RESET:
    case V5_MAIN_PAGE_ACTION_WORK_ZERO_X:
    case V5_MAIN_PAGE_ACTION_RTCP_TOGGLE:
        return 1;
    default:
        return 0;
    }
}

static void button_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    unsigned int i;

    if (!page || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    for (i = 0; i < page->button_count; ++i) {
        if (page->buttons[i] == target) {
            V5MainPageActionReport report;
            int ok;
            clear_button_pressed_visual_now(target);
            ok = v5_main_page_trigger_action(page, page->button_actions[i], &report);
            log_button_event(page->button_actions[i], ok, ok ? &report : 0);
            return;
        }
    }
}

static void make_button_rgb(V5MainPage *page, int x, int y, int w, int h, V5MainPageActionKind action, const char *text, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *button;
    lv_obj_t *label;
    if (!page || page->button_count >= V5_MAIN_PAGE_BUTTON_COUNT) {
        return;
    }
    button = lv_btn_create(page->root);
    clear_obj_style(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, rgb(r, g, b), 0);
    lv_obj_set_style_bg_color(button, rgb(245, 214, 82), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, rgb(76, 119, 146), 0);
    lv_obj_set_style_border_color(button, rgb(255, 232, 120), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, button_release_visual_cb, LV_EVENT_RELEASED, 0);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, page);

    label = lv_label_create(button);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text ? text : v5_main_page_action_label(action));
    if (x >= 920) {
        lv_obj_set_size(label, w - 38, h > 4 ? h - 4 : h);
        lv_obj_set_pos(label, 36, h > 24 ? (h - 22) / 2 : 1);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        v5_layout_add_button_icon(button, action, w, h, 1);
    } else if (action == V5_MAIN_PAGE_ACTION_NAV_NETWORK) {
        lv_label_set_text(label, "");
        lv_obj_set_size(label, w, h);
        lv_obj_set_pos(label, 0, 0);
        v5_layout_add_button_icon(button, action, w, h, 0);
    } else {
        lv_obj_set_size(label, w, h > 4 ? h - 4 : h);
        lv_obj_set_pos(label, 0, h > 24 ? (h - 22) / 2 : 1);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_set_style_text_color(label, rgb(238, 245, 248), 0);
    lv_obj_set_style_text_color(label, rgb(16, 20, 24), LV_STATE_PRESSED);

    page->buttons[page->button_count] = button;
    page->button_labels[page->button_count] = label;
    page->button_actions[page->button_count] = action;
    page->button_count += 1u;
}

static int view_action_matches_plane(V5MainPageActionKind action, V5ToolpathDisplayPlane plane)
{
    return (action == V5_MAIN_PAGE_ACTION_VIEW_XY && plane == V5_TOOLPATH_DISPLAY_XY) ||
        (action == V5_MAIN_PAGE_ACTION_VIEW_XZ && plane == V5_TOOLPATH_DISPLAY_XZ) ||
        (action == V5_MAIN_PAGE_ACTION_VIEW_YZ && plane == V5_TOOLPATH_DISPLAY_YZ) ||
        (action == V5_MAIN_PAGE_ACTION_VIEW_3D && plane == V5_TOOLPATH_DISPLAY_3D);
}

static int is_view_action(V5MainPageActionKind action)
{
    return action == V5_MAIN_PAGE_ACTION_VIEW_XY ||
        action == V5_MAIN_PAGE_ACTION_VIEW_XZ ||
        action == V5_MAIN_PAGE_ACTION_VIEW_YZ ||
        action == V5_MAIN_PAGE_ACTION_VIEW_3D;
}

static void update_toolpath_view_button_visuals(V5MainPage *page)
{
    unsigned int i;
    if (!page) {
        return;
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (!page->buttons[i] || !is_view_action(page->button_actions[i])) {
            continue;
        }
        if (view_action_matches_plane(page->button_actions[i], page->view_plane)) {
            set_obj_bg_color_if_changed(page->buttons[i], rgb(39, 113, 164), 0);
        } else {
            set_obj_bg_color_if_changed(page->buttons[i], rgb(16, 48, 77), 0);
        }
        set_obj_border_color_if_changed(page->buttons[i], rgb(76, 119, 146), 0);
    }
}

static int wcs_index_for_button_action(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_WCS_G54: return 0;
    case V5_MAIN_PAGE_ACTION_WCS_G55: return 1;
    case V5_MAIN_PAGE_ACTION_WCS_G56: return 2;
    case V5_MAIN_PAGE_ACTION_WCS_G57: return 3;
    case V5_MAIN_PAGE_ACTION_WCS_G58: return 4;
    case V5_MAIN_PAGE_ACTION_WCS_G59: return 5;
    case V5_MAIN_PAGE_ACTION_WCS_G591: return 6;
    case V5_MAIN_PAGE_ACTION_WCS_G592: return 7;
    case V5_MAIN_PAGE_ACTION_WCS_G593: return 8;
    default: return -1;
    }
}

static int jog_step_for_button_action(V5MainPageActionKind action, double *step_out)
{
    double step;
    switch (action) {
    case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
        step = 1.0;
        break;
    case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
        step = 10.0;
        break;
    case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
        step = 100.0;
        break;
    default:
        return 0;
    }
    if (step_out) {
        *step_out = step;
    }
    return 1;
}

static void set_button_state_color(lv_obj_t *button, int active, uint8_t ar, uint8_t ag, uint8_t ab, uint8_t ir, uint8_t ig, uint8_t ib)
{
    if (!button) {
        return;
    }
    set_obj_bg_color_if_changed(button, active ? rgb(ar, ag, ab) : rgb(ir, ig, ib), 0);
    set_obj_border_color_if_changed(button, active ? rgb(86, 228, 153) : rgb(76, 119, 146), 0);
}

static void update_wcs_button_visuals(V5MainPage *page)
{
    int wcs_known;
    unsigned int i;
    if (!page) {
        return;
    }
    wcs_known = v5_native_readback_wcs_known(&page->native_readback);
    for (i = 0U; i < page->button_count; ++i) {
        int wcs_index = wcs_index_for_button_action(page->button_actions[i]);
        if (wcs_index < 0) {
            continue;
        }
        set_button_state_color(
            page->buttons[i],
            wcs_known && page->native_readback.wcs_index == wcs_index,
            35, 198, 120,
            32, 52, 73);
    }
}

static void update_jog_step_button_visuals(V5MainPage *page)
{
    unsigned int i;
    if (!page) {
        return;
    }
    for (i = 0U; i < page->button_count; ++i) {
        double step = 0.0;
        if (!jog_step_for_button_action(page->button_actions[i], &step)) {
            continue;
        }
        set_button_state_color(
            page->buttons[i],
            fabs(page->jog_step - step) < 1.0e-9,
            29, 151, 104,
            32, 52, 73);
    }
}

static void update_axis_all_button_visuals(V5MainPage *page)
{
    unsigned int i;
    int selected;
    if (!page) {
        return;
    }
    selected = page->selection.space == V5_MAIN_PAGE_SELECT_MCS && page->selection.all_axes;
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_AXIS_ALL) {
            set_button_state_color(page->buttons[i], selected, 29, 151, 104, 42, 63, 85);
            return;
        }
    }
}

static void update_rtcp_button_visuals(V5MainPage *page)
{
    const char *text = "RTCP";
    int highlighted;
    unsigned int i;
    if (!page) {
        return;
    }
    highlighted = v5_native_readback_rtcp_known(&page->native_readback) && page->native_readback.rtcp_enabled;
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_RTCP_TOGGLE) {
            if (page->button_labels[i]) {
                set_label_text_if_changed(page->button_labels[i], text);
                set_obj_text_color_if_changed(page->button_labels[i], rgb(238, 245, 248), 0);
            }
            set_button_state_color(page->buttons[i], highlighted, 29, 151, 104, 42, 63, 85);
            return;
        }
    }
}

static void update_home_button_visuals(V5MainPage *page)
{
    unsigned int i;
    if (!page) {
        return;
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_HOME) {
            set_button_state_color(page->buttons[i], page->home_transaction_active, 29, 151, 104, 42, 63, 85);
            return;
        }
    }
}

static void set_home_transaction_active(V5MainPage *page, int active, int flush)
{
    if (!page) {
        return;
    }
    page->home_transaction_active = active ? 1 : 0;
    update_home_button_visuals(page);
    if (flush) {
        lv_refr_now(NULL);
    }
}

static void update_main_page_state_button_visuals(V5MainPage *page)
{
    update_toolpath_view_button_visuals(page);
    update_wcs_button_visuals(page);
    update_jog_step_button_visuals(page);
    update_axis_all_button_visuals(page);
    update_rtcp_button_visuals(page);
    update_home_button_visuals(page);
}

static void make_v3_main_buttons(V5MainPage *page)
{
    make_button_rgb(page, 920, 0, 104, 60, V5_MAIN_PAGE_ACTION_NAV_MAIN, "主页面", 41, 145, 107);
    make_button_rgb(page, 920, 60, 104, 60, V5_MAIN_PAGE_ACTION_NAV_TOOL, "刀具补偿", 16, 48, 77);
    make_button_rgb(page, 920, 120, 104, 60, V5_MAIN_PAGE_ACTION_NAV_PROBE, "探测", 16, 48, 77);
    make_button_rgb(page, 920, 180, 104, 60, V5_MAIN_PAGE_ACTION_NAV_OFFSET, "偏置", 16, 48, 77);
    make_button_rgb(page, 920, 240, 104, 60, V5_MAIN_PAGE_ACTION_NAV_IO, "IO设置", 16, 48, 77);
    make_button_rgb(page, 920, 300, 104, 60, V5_MAIN_PAGE_ACTION_NAV_SETTINGS, "系统设置", 16, 48, 77);
    make_button_rgb(page, 920, 420, 104, 60, V5_MAIN_PAGE_ACTION_PAUSE, "暂停", 74, 91, 111);
    make_button_rgb(page, 920, 480, 104, 60, V5_MAIN_PAGE_ACTION_START, "启动", 16, 48, 77);
    make_button_rgb(page, 920, 540, 104, 60, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "急停", 199, 70, 46);
    make_button_rgb(page, 842, 14, 38, 34, V5_MAIN_PAGE_ACTION_NAV_NETWORK, "网", 16, 48, 77);

    make_button_rgb(page, 456, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G54, "G54", 32, 52, 73);
    make_button_rgb(page, 502, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G55, "G55", 32, 52, 73);
    make_button_rgb(page, 548, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G56, "G56", 32, 52, 73);
    make_button_rgb(page, 594, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G57, "G57", 32, 52, 73);
    make_button_rgb(page, 640, 282, 42, 20, V5_MAIN_PAGE_ACTION_WCS_G58, "G58", 32, 52, 73);
    make_button_rgb(page, 456, 306, 50, 20, V5_MAIN_PAGE_ACTION_WCS_G59, "G59", 32, 52, 73);
    make_button_rgb(page, 510, 306, 50, 20, V5_MAIN_PAGE_ACTION_WCS_G591, "G59.1", 32, 52, 73);
    make_button_rgb(page, 564, 306, 50, 20, V5_MAIN_PAGE_ACTION_WCS_G592, "G59.2", 32, 52, 73);
    make_button_rgb(page, 618, 306, 50, 20, V5_MAIN_PAGE_ACTION_WCS_G593, "G59.3", 32, 52, 73);
    make_button_rgb(page, 402, 328, 108, 48, V5_MAIN_PAGE_ACTION_AXIS_ALL, "机械全轴", 42, 63, 85);
    make_button_rgb(page, 516, 328, 50, 48, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, "RTCP", 42, 63, 85);
    make_button_rgb(page, 572, 328, 80, 48, V5_MAIN_PAGE_ACTION_HOME, "回零", 42, 63, 85);
    make_button_rgb(page, 658, 328, 82, 48, V5_MAIN_PAGE_ACTION_WORK_ZERO_X, "归零", 42, 63, 85);
    make_button_rgb(page, 402, 382, 54, 48, V5_MAIN_PAGE_ACTION_JOG_STEP_1, "X1", 32, 52, 73);
    make_button_rgb(page, 462, 382, 50, 48, V5_MAIN_PAGE_ACTION_JOG_STEP_10, "X10", 32, 52, 73);
    make_button_rgb(page, 518, 382, 50, 48, V5_MAIN_PAGE_ACTION_JOG_STEP_100, "X100", 32, 52, 73);
    make_button_rgb(page, 574, 382, 78, 48, V5_MAIN_PAGE_ACTION_JOG_PLUS, "点动+", 32, 52, 73);
    make_button_rgb(page, 658, 382, 82, 48, V5_MAIN_PAGE_ACTION_JOG_MINUS, "点动-", 32, 52, 73);
    make_button_rgb(page, 328, 262, 58, 30, V5_MAIN_PAGE_ACTION_VIEW_XY, "XY", 25, 45, 62);
    make_button_rgb(page, 328, 298, 58, 30, V5_MAIN_PAGE_ACTION_VIEW_XZ, "XZ", 25, 45, 62);
    make_button_rgb(page, 328, 334, 58, 30, V5_MAIN_PAGE_ACTION_VIEW_YZ, "YZ", 25, 45, 62);
    make_button_rgb(page, 328, 370, 58, 30, V5_MAIN_PAGE_ACTION_VIEW_3D, "3D", 25, 45, 62);
    make_button_rgb(page, 570, 444, 82, 44, V5_MAIN_PAGE_ACTION_NAV_PROGRAM, "打开程序", 16, 48, 77);
    make_button_rgb(page, 570, 494, 82, 44, V5_MAIN_PAGE_ACTION_NAV_MDI, "手动输入", 16, 48, 77);
    make_button_rgb(page, 656, 494, 82, 44, V5_MAIN_PAGE_ACTION_FIRST_POINT, "首点", 16, 48, 77);
}

void v5_main_page_init(V5MainPage *page)
{
    if (!page) {
        return;
    }
    memset(page, 0, sizeof(*page));
}

static void update_estop_button_text(V5MainPage *page);
static void update_main_page_state_button_visuals(V5MainPage *page);
static void update_axis_all_button_visuals(V5MainPage *page);

int v5_main_page_create(V5MainPage *page, lv_obj_t *parent)
{
    unsigned int i;
    static const char *axis_text[V5_MAIN_PAGE_AXIS_COUNT] = {"X", "Y", "Z", "A", "C"};

    if (!page || !parent) {
        return 0;
    }
    v5_main_page_init(page);

    page->view_plane = V5_TOOLPATH_DISPLAY_3D;
    page->toolpath_program_plane = page->view_plane;
    page->toolpath_manual_scale = 1.0;
    v5_toolpath_display_fit_init(&page->toolpath_fit);
    page->jog_step = 1.0;
    v5_main_page_select_all_axes(page);
    v5_native_readback_set_unavailable(&page->native_readback, "native_readback_unavailable");

    page->root = lv_obj_create(parent);
    clear_obj_style(page->root);
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_set_size(page->root, 1024, 600);
    lv_obj_set_style_bg_color(page->root, rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);

    make_panel(page->root, 0, 0, 920, 55, 4, 24, 36);
    make_label_ex(page->root, 28, 25, 120, 24, "精密数控", 86, 204, 252, LV_TEXT_ALIGN_LEFT);
    make_divider(page->root, 0, 55, 920, 1);
    make_divider(page->root, 394, 55, 1, 386);
    make_divider(page->root, 397, 278, 348, 1);
    make_divider(page->root, 0, 441, 745, 1);
    make_divider(page->root, 560, 441, 1, 154);
    make_divider(page->root, 745, 55, 1, 540);
    make_divider(page->root, 920, 0, 1, 600);

    make_panel(page->root, 0, 55, 394, 386, 4, 24, 36);
    page->modal_label = make_label_ex(page->root, 12, 68, 150, 300, "", 0, 142, 146, LV_TEXT_ALIGN_LEFT);
    page->toolpath_clip_layer = lv_obj_create(page->root);
    clear_obj_style(page->toolpath_clip_layer);
    lv_obj_set_pos(page->toolpath_clip_layer, V5_TOOLPATH_X, V5_TOOLPATH_Y);
    lv_obj_set_size(page->toolpath_clip_layer, V5_TOOLPATH_W, V5_TOOLPATH_H);
    lv_obj_set_style_bg_opa(page->toolpath_clip_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page->toolpath_clip_layer, LV_OBJ_FLAG_CLICKABLE);
    page->toolpath_mcs_origin_line = make_toolpath_v3_line(page->toolpath_clip_layer, 210, 235, 255, 2);
    page->toolpath_mcs_axis_lines[0] = make_toolpath_v3_line(page->toolpath_clip_layer, 255, 150, 156, 1);
    page->toolpath_mcs_axis_lines[1] = make_toolpath_v3_line(page->toolpath_clip_layer, 120, 255, 190, 1);
    page->toolpath_mcs_axis_lines[2] = make_toolpath_v3_line(page->toolpath_clip_layer, 180, 226, 255, 1);
    page->toolpath_a_axis_line = make_toolpath_v3_line(page->toolpath_clip_layer, 255, 113, 118, 1);
    page->toolpath_c_axis_line = make_toolpath_v3_line(page->toolpath_clip_layer, 120, 240, 255, 1);
    page->toolpath_wcs_origin_line = make_toolpath_v3_line(page->toolpath_clip_layer, 68, 221, 144, 1);
    page->toolpath_wcs_axis_lines[0] = make_toolpath_v3_line(page->toolpath_clip_layer, 255, 100, 106, 1);
    page->toolpath_wcs_axis_lines[1] = make_toolpath_v3_line(page->toolpath_clip_layer, 0, 232, 150, 1);
    page->toolpath_wcs_axis_lines[2] = make_toolpath_v3_line(page->toolpath_clip_layer, 86, 204, 252, 1);
    for (i = 0U; i < V5_MAIN_PAGE_TOOLPATH_WCS_COUNT; ++i) {
        page->toolpath_program_wcs_origin_lines[i] = make_toolpath_v3_line(page->toolpath_clip_layer, 68, 221, 144, 1);
        for (unsigned int axis = 0U; axis < 3U; ++axis) {
            static const uint8_t axis_colors[3][3] = {{255, 100, 106}, {0, 232, 150}, {86, 204, 252}};
            page->toolpath_program_wcs_axis_lines[i][axis] = make_toolpath_v3_line(
                page->toolpath_clip_layer,
                axis_colors[axis][0],
                axis_colors[axis][1],
                axis_colors[axis][2],
                1);
        }
    }
    page->trajectory_line = make_toolpath_v3_line(page->toolpath_clip_layer, 255, 214, 64, V5_TOOLPATH_PROGRAM_LINE_WIDTH);
    page->toolpath_line_segments[0] = page->trajectory_line;
    for (i = 1U; i < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS; ++i) {
        page->toolpath_line_segments[i] = make_toolpath_v3_line(
            page->toolpath_clip_layer,
            255,
            214,
            64,
            V5_TOOLPATH_PROGRAM_LINE_WIDTH);
    }
    page->toolpath_holder_line = make_toolpath_v3_line(page->toolpath_clip_layer, 96, 176, 255, 5);
    page->toolpath_summary_label = make_label_ex(page->root, 12, 401, 374, 18, "", 68, 221, 144, LV_TEXT_ALIGN_LEFT);
    page->toolpath_detail_label = make_label_ex(page->root, 12, 420, 374, 18, "", 86, 204, 252, LV_TEXT_ALIGN_LEFT);
    page->toolpath_view_label = make_label_ex(page->root, 18, 358, 260, 24, "", 86, 204, 252, LV_TEXT_ALIGN_LEFT);
    page->toolpath_a_label = make_label_ex(page->root, 132, 162, 24, 22, "A", 255, 113, 118, LV_TEXT_ALIGN_CENTER);
    page->toolpath_c_label = make_label_ex(page->root, 160, 162, 20, 22, "C", 120, 240, 255, LV_TEXT_ALIGN_CENTER);
    page->toolpath_wcs_label = make_label_ex(page->root, 18, 326, 42, 22, "", 68, 221, 144, LV_TEXT_ALIGN_LEFT);
    page->toolpath_mcs_label = make_label_ex(page->root, 10, 62, 42, 22, "", 96, 176, 255, LV_TEXT_ALIGN_LEFT);
    for (i = 0U; i < V5_MAIN_PAGE_TOOLPATH_WCS_COUNT; ++i) {
        page->toolpath_program_wcs_labels[i] = make_label_ex(page->root, 18, 326, 42, 22, "", 68, 221, 144, LV_TEXT_ALIGN_LEFT);
    }
    for (i = 0U; i < 3U; ++i) {
        page->toolpath_mcs_axis_labels[i] = make_label_ex(page->root, 10, 62 + (int)i * 18, 32, 18, "", 180, 226, 255, LV_TEXT_ALIGN_LEFT);
    }
    lv_obj_add_flag(page->toolpath_a_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->toolpath_c_label, LV_OBJ_FLAG_HIDDEN);
    page->toolpath_microkernel_marker_dot = make_toolpath_v3_dot(page->root, 255, 64, 64, 255, 230, 230);
    page->toolpath_holder_marker_line = make_toolpath_v3_dot(page->root, 68, 221, 144, 220, 255, 235);
    page->toolpath_a_center_line = make_toolpath_v3_center_dot(page->root, 255, 113, 118);
    page->toolpath_c_center_line = make_toolpath_v3_center_dot(page->root, 120, 240, 255);
    draw_toolpath_boot_scaffold(page);

    make_panel(page->root, 397, 55, 348, 230, 6, 26, 39);
    make_label_ex(page->root, 410, 79, 32, 24, "轴", 156, 178, 202, LV_TEXT_ALIGN_CENTER);
    make_label_ex(page->root, 449, 79, 124, 24, "机械坐标", 88, 204, 255, LV_TEXT_ALIGN_CENTER);
    make_panel(page->root, 579, 73, 124, 28, 25, 72, 62);
    page->wcs_header_label = make_label_ex(page->root, 579, 77, 124, 22, "加工 --", 68, 221, 144, LV_TEXT_ALIGN_CENTER);
    v5_coordinate_digits_create_main(&page->coordinate_digits, page->root, page->coordinate_digits_buffer);
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        int y = 113 + (int)i * 32;
        page->axis_labels[i] = make_label_ex(page->root, 414, 115 + (int)i * 32, 32, 24, axis_text[i], 218, 232, 242, LV_TEXT_ALIGN_CENTER);
        page->mcs_targets[i].space = V5_MAIN_PAGE_SELECT_MCS;
        page->mcs_targets[i].axis = axis_text[i][0];
        page->wcs_targets[i].space = V5_MAIN_PAGE_SELECT_WCS;
        page->wcs_targets[i].axis = axis_text[i][0];
        page->mcs_labels[i] = make_label_ex(page->root, 449, y, 124, 30, "000.000", 88, 204, 255, LV_TEXT_ALIGN_RIGHT);
        page->cmd_labels[i] = make_label_ex(page->root, 579, y, 124, 30, "000.000", 68, 221, 144, LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_style_text_opa(page->mcs_labels[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_opa(page->cmd_labels[i], LV_OPA_TRANSP, 0);
        make_coordinate_value_clickable(page, page->mcs_labels[i]);
        make_coordinate_value_clickable(page, page->cmd_labels[i]);
        lv_obj_move_foreground(page->mcs_labels[i]);
        lv_obj_move_foreground(page->cmd_labels[i]);
    }
    update_coordinate_selection_style(page);

    make_panel(page->root, 746, 56, 174, 276, 5, 24, 39);
    make_label_ex(page->root, 762, 64, 82, 24, "主轴转速", 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    make_label_ex(page->root, 872, 64, 34, 22, "rpm", 28, 193, 238, LV_TEXT_ALIGN_LEFT);
    page->spindle_speed_label = make_label_ex(page->root, 762, 88, 142, 30, "--", 28, 193, 238, LV_TEXT_ALIGN_CENTER);
    make_label_ex(page->root, 762, 124, 76, 24, "主轴倍率", 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    page->spindle_override_label = make_label_ex(page->root, 850, 124, 52, 24, "100%", 28, 193, 238, LV_TEXT_ALIGN_RIGHT);
    make_override_reset_hit(page, 762, 84, 142, 38, spindle_override_reset_event_cb);
    make_override_reset_hit(page, 840, 120, 70, 32, spindle_override_reset_event_cb);
    make_panel(page->root, 762, 150, 142, 7, 13, 42, 59);
    make_panel(page->root, 762, 150, 72, 7, 28, 193, 238);
    make_panel(page->root, 823, 143, 22, 22, 38, 180, 230);
    make_divider(page->root, 758, 220, 150, 1);
    make_label_ex(page->root, 768, 218, 78, 24, "进给速度", 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    make_label_ex(page->root, 850, 218, 66, 22, "mm/min", 28, 193, 238, LV_TEXT_ALIGN_LEFT);
    page->linear_velocity_label = make_label_ex(page->root, 762, 240, 142, 30, "0.0", 28, 193, 238, LV_TEXT_ALIGN_CENTER);
    make_label_ex(page->root, 762, 275, 76, 24, "进给倍率", 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    page->feed_override_label = make_label_ex(page->root, 850, 275, 52, 24, "100%", 28, 193, 238, LV_TEXT_ALIGN_RIGHT);
    make_override_reset_hit(page, 762, 238, 142, 38, feed_override_reset_event_cb);
    make_override_reset_hit(page, 840, 271, 70, 32, feed_override_reset_event_cb);
    make_panel(page->root, 762, 307, 142, 7, 13, 42, 59);
    make_panel(page->root, 762, 307, 72, 7, 28, 193, 238);
    make_panel(page->root, 823, 300, 22, 22, 38, 180, 230);

    make_panel(page->root, 746, 342, 174, 188, 5, 24, 39);
    make_label_ex(page->root, 754, 350, 158, 22, "跟随误差 mm/deg", 210, 220, 226, LV_TEXT_ALIGN_LEFT);
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "%s: --.---", axis_text[i]);
        page->error_labels[i] = make_label_ex(page->root, 758, 378 + (int)i * 28, 150, 26, text, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    }
    make_panel(page->root, 746, 540, 174, 60, 5, 24, 39);
    page->cpu0_label = make_label_ex(page->root, 754, 545, 158, 24, "cpu0  --%", 255, 86, 86, LV_TEXT_ALIGN_LEFT);
    page->cpu1_label = make_label_ex(page->root, 754, 571, 158, 24, "cpu1  --%", 42, 221, 128, LV_TEXT_ALIGN_LEFT);

    make_panel(page->root, 0, 441, 560, 154, 7, 31, 48);
    make_label_ex(page->root, 18, 456, 110, 24, "行号  程序名:", 156, 178, 202, LV_TEXT_ALIGN_LEFT);
    page->program_name_label = make_label_ex(page->root, 132, 456, 180, 24, "手动输入", 218, 232, 242, LV_TEXT_ALIGN_LEFT);
    for (i = 0; i < V5_PROGRAM_PREVIEW_ROWS; ++i) {
        int y = 475 + (int)i * 26 + (i > 0U ? 1 : 0);
        page->program_line_bg[i] = make_panel(page->root, 14, y, 540, 26, i == 0U ? 43 : 7, i == 0U ? 133 : 31, i == 0U ? 83 : 48);
        page->program_line_labels[i] = make_label_ex(page->root, 24, 480 + (int)i * 26 + (i > 0U ? 1 : 0), 520, 20, "", i == 0U ? 226 : 156, i == 0U ? 238 : 178, i == 0U ? 246 : 202, LV_TEXT_ALIGN_LEFT);
    }
    create_main_program_edit_hit_area(page);

    make_v3_main_buttons(page);
    update_main_page_state_button_visuals(page);
    update_estop_button_text(page);
    return page->button_count == V5_MAIN_PAGE_BUTTON_COUNT;
}

int v5_main_page_apply_status_flags(V5MainPage *page, const V5UiStatusView *status, unsigned int refresh_flags)
{
    V5CoordinatePanelSnapshot panel;
    V5ToolpathDisplaySnapshot dynamic_display;
    const V5ProgramRuntime *runtime = page && page->program_controller ? v5_program_controller_runtime(page->program_controller) : 0;
    unsigned int preview_count = 0U;
    unsigned int runtime_generation = 0U;
    int program_wcs_index = -1;
    double program_wcs_offset[3] = {0.0, 0.0, 0.0};
    int program_wcs_changed = 0;
    int program_fit_dirty = 0;
    int program_projection_dirty = 0;
    int static_toolpath_due = 0;
    int program_ac_changed = 0;
    int static_pose_changed = 0;
    int program_refresh_due = 0;
    int runtime_has_program = runtime && v5_program_runtime_has_open_program(runtime);
    unsigned int i;

    if (!page || !page->root) {
        return 0;
    }
    page->last_status_valid = status != 0;
    if (status) {
        page->last_status = *status;
    }
    if (refresh_flags == 0U) {
        return 1;
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_DYNAMIC) != 0U) {
        v5_coordinate_panel_from_status(status, &panel);
        for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
            if (page->axis_labels[i]) {
                char axis[2] = {panel.lines[i].axis ? panel.lines[i].axis : '-', '\0'};
                set_label_text_if_changed(page->axis_labels[i], axis);
            }
            {
                char wcs_text[32];
                format_main_page_wcs_coordinate(wcs_text, sizeof(wcs_text), status, &page->native_readback, i);
                v5_coordinate_digits_set_value(
                    &page->coordinate_digits,
                    0U,
                    i,
                    panel.lines[i].mcs_text,
                    main_coordinate_digit_color(page, i, 0));
                v5_coordinate_digits_set_value(
                    &page->coordinate_digits,
                    1U,
                    i,
                    wcs_text,
                    main_coordinate_digit_color(page, i, 1));
            }
            if (page->error_labels[i]) {
                char error_text[32];
                snprintf(error_text, sizeof(error_text), "%c: %s", panel.lines[i].axis ? panel.lines[i].axis : '-', panel.lines[i].following_error_text);
                set_label_text_if_changed(page->error_labels[i], error_text);
            }
        }
        set_label_text_if_changed(page->spindle_speed_label, panel.spindle_speed_text);
        set_label_text_if_changed(page->linear_velocity_label, panel.linear_velocity_text);
        set_label_text_if_changed(page->feed_override_label, panel.feed_override_text);
        set_label_text_if_changed(page->spindle_override_label, panel.spindle_override_text);
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_SLOW) != 0U) {
        update_main_page_wcs_header(page);
        update_toolpath_status_text(page);
        update_main_page_modal_label(page);
        if (page->cpu0_label && page->cpu1_label) {
            char cpu0_text[24];
            char cpu1_text[24];
            v5_remote_metrics_display_text(cpu0_text, sizeof(cpu0_text), cpu1_text, sizeof(cpu1_text));
            set_label_text_if_changed(page->cpu0_label, cpu0_text);
            set_label_text_if_changed(page->cpu1_label, cpu1_text);
        }
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_DYNAMIC) != 0U) {
        static_toolpath_due =
            ((refresh_flags & V5_MAIN_PAGE_REFRESH_SLOW) != 0U) ||
            (runtime_has_program && !page->toolpath_program_visible) ||
            !page->toolpath_fit.valid ||
            page->toolpath_program_view_generation != page->toolpath_view_generation;
        program_ac_changed = main_page_program_ac_projection_changed(page, status);
        static_pose_changed = main_page_static_pose_changed(page, status);
        program_refresh_due = static_toolpath_due || program_ac_changed;

        if (!page->toolpath_fit.valid && status) {
            if (v5_toolpath_display_fit_from_status(status, page->view_plane, &page->toolpath_fit)) {
                main_page_expand_visible_toolpath_fit(page, status);
                page->toolpath_static_cache_misses += 1U;
            }
        }

        if (program_refresh_due && runtime_has_program) {
            if (static_toolpath_due) {
                runtime_generation = v5_program_runtime_loaded_epoch(runtime);
                preview_count = v5_program_runtime_preview_trajectory(
                    runtime,
                    page->toolpath_program_points,
                    V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT);
                if (preview_count > 0U &&
                    !main_page_apply_program_preview_wcs_offset(
                        page, runtime, page->toolpath_program_points, preview_count, &program_wcs_index, program_wcs_offset)) {
                    preview_count = 0U;
                }
                page->toolpath_program_point_count = preview_count;
            } else {
                runtime_generation = page->toolpath_program_generation;
                preview_count = page->toolpath_program_point_count;
                program_wcs_index = page->toolpath_program_wcs_index;
                program_wcs_offset[0] = page->toolpath_program_wcs_offset[0];
                program_wcs_offset[1] = page->toolpath_program_wcs_offset[1];
                program_wcs_offset[2] = page->toolpath_program_wcs_offset[2];
            }
            if (preview_count > 0U) {
                main_page_update_program_project_points(page, status, preview_count);
                program_wcs_changed =
                    !page->toolpath_program_wcs_valid ||
                    page->toolpath_program_wcs_index != program_wcs_index ||
                    fabs(page->toolpath_program_wcs_offset[0] - program_wcs_offset[0]) > 0.0005 ||
                    fabs(page->toolpath_program_wcs_offset[1] - program_wcs_offset[1]) > 0.0005 ||
                    fabs(page->toolpath_program_wcs_offset[2] - program_wcs_offset[2]) > 0.0005;
            }
            program_fit_dirty =
                static_toolpath_due &&
                (!page->toolpath_program_visible ||
                 page->toolpath_program_generation != runtime_generation ||
                 page->toolpath_program_plane != page->view_plane ||
                 program_wcs_changed ||
                 !page->toolpath_fit.valid);
            program_projection_dirty =
                program_fit_dirty ||
                program_ac_changed ||
                page->toolpath_program_view_generation != page->toolpath_view_generation;
            if (preview_count > 0U && program_projection_dirty) {
                if (!program_fit_dirty ||
                    v5_toolpath_display_fit_from_points(
                        page->toolpath_program_project_points,
                        preview_count,
                        page->view_plane,
                        &page->toolpath_fit)) {
                    if (program_fit_dirty) {
                        main_page_expand_visible_toolpath_fit(page, status);
                    }
                    unsigned int projected_count = v5_toolpath_display_project_points_with_fit(
                        page->toolpath_program_project_points,
                        preview_count,
                        &page->toolpath_fit,
                        (double)V5_TOOLPATH_W,
                        (double)V5_TOOLPATH_H,
                        page->toolpath_program_screen_points,
                        V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT);
                    for (i = 0U; i < projected_count; ++i) {
                        page->toolpath_program_screen_points[i] =
                            apply_toolpath_view_transform(page, page->toolpath_program_screen_points[i]);
                    }
                    set_toolpath_program_line(page, page->toolpath_program_screen_points, projected_count);
                    page->toolpath_program_generation = runtime_generation;
                    page->toolpath_program_view_generation = page->toolpath_view_generation;
                    page->toolpath_program_plane = page->view_plane;
                    page->toolpath_program_wcs_valid = 1;
                    page->toolpath_program_wcs_index = program_wcs_index;
                    page->toolpath_program_wcs_offset[0] = program_wcs_offset[0];
                    page->toolpath_program_wcs_offset[1] = program_wcs_offset[1];
                    page->toolpath_program_wcs_offset[2] = program_wcs_offset[2];
                    page->toolpath_program_point_count = preview_count;
                    if (program_fit_dirty) {
                        page->toolpath_static_cache_misses += 1U;
                    }
                } else {
                    hide_toolpath_program_line(page);
                }
            } else if (preview_count > 0U && page->toolpath_program_visible) {
                page->toolpath_static_cache_hits += 1U;
            } else {
                hide_toolpath_program_line(page);
            }
    } else if (static_toolpath_due) {
        hide_toolpath_program_line(page);
        page->toolpath_program_generation = 0U;
        page->toolpath_program_view_generation = 0U;
        page->toolpath_program_wcs_valid = 0;
        page->toolpath_program_wcs_index = -1;
        memset(page->toolpath_program_wcs_offset, 0, sizeof(page->toolpath_program_wcs_offset));
        page->toolpath_program_point_count = 0U;
        page->toolpath_program_ac_valid = 0;
        page->toolpath_program_ac_a_deg = 0.0;
        page->toolpath_program_ac_c_deg = 0.0;
    }

    if (static_toolpath_due || program_ac_changed || static_pose_changed ||
        (refresh_flags & V5_MAIN_PAGE_REFRESH_SLOW) != 0U) {
        update_toolpath_state_lines(page, status);
        main_page_store_static_pose(page, status);
    }

    v5_toolpath_display_from_status_with_fit(
        status,
        &page->toolpath_fit,
        (double)V5_TOOLPATH_W,
        (double)V5_TOOLPATH_H,
        &dynamic_display);
    apply_toolpath_view_transform_to_snapshot(page, &dynamic_display);
    {
        V5ToolpathScreenPoint cmd_tip_point;
        int cmd_tip_valid = main_page_project_cmd_tip(page, status, &cmd_tip_point);
        set_toolpath_v3_dot_center(page->toolpath_microkernel_marker_dot, &cmd_tip_point, cmd_tip_valid);
    }
    set_toolpath_v3_dot_center(page->toolpath_holder_marker_line, &dynamic_display.mcs_point, dynamic_display.mcs_valid);
    update_toolpath_holder_line(page, status, dynamic_display.mcs_valid ? &dynamic_display.mcs_point : 0);
    if (!dynamic_display.mcs_valid) {
        draw_toolpath_boot_scaffold(page);
    }
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_BUTTONS) != 0U) {
        update_main_page_state_button_visuals(page);
    }
    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_ESTOP) != 0U) {
        update_estop_button_text(page);
    }

    return 1;
}

int v5_main_page_apply_status(V5MainPage *page, const V5UiStatusView *status)
{
    return v5_main_page_apply_status_flags(page, status, V5_MAIN_PAGE_REFRESH_ALL);
}

int v5_main_page_handle_touch_points(V5MainPage *page, const lv_point_t *points, int count, int pressed, int *changed)
{
    double mid_x;
    double mid_y;
    double distance;
    double angle;
    double ratio;
    int local_changed = 0;
    if (changed) {
        *changed = 0;
    }
    if (!page || !page->root) {
        return 0;
    }
    if (!pressed || !points || count <= 0) {
        const int was_active = page->toolpath_gesture_active_count > 0;
        page->toolpath_gesture_active_count = 0;
        return was_active;
    }
    if (count > 2) {
        count = 2;
    }
    if (!toolpath_points_in_graphics_zone(points, count)) {
        page->toolpath_gesture_active_count = 0;
        return 0;
    }
    if (count == 1) {
        if (page->toolpath_gesture_active_count != 1) {
            page->toolpath_gesture_active_count = 1;
            page->toolpath_gesture_last_points[0] = points[0];
            return 1;
        }
        page->toolpath_manual_pan_x += (double)points[0].x - (double)page->toolpath_gesture_last_points[0].x;
        page->toolpath_manual_pan_y += (double)points[0].y - (double)page->toolpath_gesture_last_points[0].y;
        local_changed = points[0].x != page->toolpath_gesture_last_points[0].x || points[0].y != page->toolpath_gesture_last_points[0].y;
        page->toolpath_gesture_last_points[0] = points[0];
    } else {
        mid_x = ((double)points[0].x + (double)points[1].x) * 0.5;
        mid_y = ((double)points[0].y + (double)points[1].y) * 0.5;
        distance = point_distance(&points[0], &points[1]);
        angle = point_angle_deg(&points[0], &points[1]);
        if (page->toolpath_gesture_active_count != 2 || page->toolpath_gesture_last_distance <= 1.0) {
            page->toolpath_gesture_active_count = 2;
            page->toolpath_gesture_last_points[0] = points[0];
            page->toolpath_gesture_last_points[1] = points[1];
            page->toolpath_gesture_last_mid_x = mid_x;
            page->toolpath_gesture_last_mid_y = mid_y;
            page->toolpath_gesture_last_distance = distance > 1.0 ? distance : 1.0;
            page->toolpath_gesture_last_angle_deg = angle;
            return 1;
        }
        ratio = distance > 1.0 ? distance / page->toolpath_gesture_last_distance : 1.0;
        page->toolpath_manual_scale = clamp_double(
            (page->toolpath_manual_scale > 0.0 ? page->toolpath_manual_scale : 1.0) * ratio,
            V5_TOOLPATH_GESTURE_MIN_SCALE,
            V5_TOOLPATH_GESTURE_MAX_SCALE);
        page->toolpath_manual_pan_x += mid_x - page->toolpath_gesture_last_mid_x;
        page->toolpath_manual_pan_y += mid_y - page->toolpath_gesture_last_mid_y;
        page->toolpath_manual_rotate_deg = normalize_deg(page->toolpath_manual_rotate_deg + angle - page->toolpath_gesture_last_angle_deg);
        local_changed = fabs(ratio - 1.0) > 1.0e-6 ||
            fabs(mid_x - page->toolpath_gesture_last_mid_x) > 1.0e-6 ||
            fabs(mid_y - page->toolpath_gesture_last_mid_y) > 1.0e-6 ||
            fabs(angle - page->toolpath_gesture_last_angle_deg) > 1.0e-6;
        page->toolpath_gesture_last_points[0] = points[0];
        page->toolpath_gesture_last_points[1] = points[1];
        page->toolpath_gesture_last_mid_x = mid_x;
        page->toolpath_gesture_last_mid_y = mid_y;
        page->toolpath_gesture_last_distance = distance > 1.0 ? distance : 1.0;
        page->toolpath_gesture_last_angle_deg = angle;
    }
    if (local_changed) {
        page->toolpath_view_generation += 1U;
        if (page->toolpath_view_generation == 0U) {
            page->toolpath_view_generation = 1U;
        }
        if (changed) {
            *changed = 1;
        }
    }
    return 1;
}

void v5_main_page_refresh_program_status(V5MainPage *page)
{
    const V5ProgramRuntime *runtime;

    if (!page) {
        return;
    }
    runtime = page->program_controller ? v5_program_controller_runtime(page->program_controller) : 0;
    refresh_program_preview_rows(page, runtime);
    if (runtime && v5_program_runtime_has_mdi(runtime)) {
        if (page->program_name_label) {
            set_label_text_if_changed(page->program_name_label, "手动输入");
        }
    } else if (runtime && v5_program_runtime_has_open_program(runtime)) {
        if (page->program_name_label) {
            set_label_text_if_changed(page->program_name_label, runtime->display_name);
        }
    } else {
        if (page->program_name_label) {
            set_label_text_if_changed(page->program_name_label, "手动输入");
        }
    }
}


void v5_main_page_select_all_axes(V5MainPage *page)
{
    if (!page) {
        return;
    }
    page->selection.space = V5_MAIN_PAGE_SELECT_MCS;
    page->selection.axis = '*';
    page->selection.all_axes = 1;
    update_coordinate_selection_style(page);
    update_axis_all_button_visuals(page);
}

int v5_main_page_select_axis(V5MainPage *page, V5MainPageSelectionSpace space, char axis)
{
    char up;
    if (!page) {
        return 0;
    }
    up = (char)toupper((unsigned char)axis);
    if ((space != V5_MAIN_PAGE_SELECT_MCS && space != V5_MAIN_PAGE_SELECT_WCS) ||
        (up != 'X' && up != 'Y' && up != 'Z' && up != 'A' && up != 'C')) {
        return 0;
    }
    page->selection.space = space;
    page->selection.axis = up;
    page->selection.all_axes = 0;
    update_coordinate_selection_style(page);
    update_axis_all_button_visuals(page);
    return 1;
}

static int native_readback_requests_estop_reset(const V5NativeReadback *readback)
{
    if (v5_native_readback_safety_estop_known(readback) && readback->safety_estop_active) {
        return 1;
    }
    if (v5_native_readback_machine_enable_known(readback) && !readback->machine_enabled) {
        return 1;
    }
    return 0;
}

static void update_estop_button_text(V5MainPage *page)
{
    const char *text = "急停";
    unsigned int i;
    if (!page) {
        return;
    }
    if (native_readback_requests_estop_reset(&page->native_readback)) {
        text = "取消急停";
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_ESTOP_FORCE && page->button_labels[i]) {
            set_label_text_if_changed(page->button_labels[i], text);
            return;
        }
    }
}

void v5_main_page_bind_program_controller(V5MainPage *page, V5ProgramController *controller)
{
    if (!page) {
        return;
    }
    page->program_controller = controller;
    v5_main_page_refresh_program_status(page);
}

void v5_main_page_set_command_execution_enabled(V5MainPage *page, int enabled)
{
    if (!page) {
        return;
    }
    page->command_execution_enabled = enabled ? 1 : 0;
}

void v5_main_page_set_native_readback(V5MainPage *page, const V5NativeReadback *readback)
{
    if (!page) {
        return;
    }
    if (readback) {
        page->native_readback = *readback;
    } else {
        v5_native_readback_set_unavailable(&page->native_readback, "native_readback_unavailable");
    }
    update_estop_button_text(page);
    update_main_page_state_button_visuals(page);
    update_main_page_wcs_header(page);
    update_main_page_modal_label(page);
    update_toolpath_status_text(page);
    v5_main_page_refresh_program_status(page);
    if (page->last_status_valid) {
        update_toolpath_state_lines(page, &page->last_status);
    }
}

void v5_main_page_set_navigation_callback(V5MainPage *page, V5UiNavigationCallback cb, void *user_data)
{
    if (!page) {
        return;
    }
    page->navigation_cb = cb;
    page->navigation_user_data = user_data;
}

void v5_main_page_set_native_readback_refresh_callback(
    V5MainPage *page,
    V5MainPageNativeReadbackRefreshCallback cb,
    void *user_data)
{
    if (!page) {
        return;
    }
    page->native_readback_refresh_cb = cb;
    page->native_readback_refresh_user_data = user_data;
}

int v5_main_page_set_mdi_text(V5MainPage *page, const char *line)
{
    if (!page || !page->program_controller) {
        return 0;
    }
    if (!v5_program_runtime_set_mdi_line(&page->program_controller->runtime, line)) {
        return 0;
    }
    mark_toolpath_static_dirty(page);
    v5_main_page_refresh_program_status(page);
    return 1;
}

int v5_main_page_open_program(V5MainPage *page, const char *path, V5ProgramOpenResult *open_report)
{
    V5ProgramOpenResult local_open;
    V5ProgramOpenResult *out = open_report ? open_report : &local_open;

    if (!page || !page->program_controller) {
        return 0;
    }
    if (!v5_command_program_open(page->program_controller, path, out)) {
        return 0;
    }
    page->last_program_open = *out;
    mark_toolpath_static_dirty(page);
    v5_main_page_refresh_program_status(page);
    return 1;
}

static void main_page_sleep_ms(unsigned int ms)
{
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000U);
    req.tv_nsec = (long)(ms % 1000U) * 1000000L;
    (void)nanosleep(&req, 0);
}

static int main_page_confirm_wcs_readback_once(
    V5MainPage *page,
    V5MainPageActionReport *report,
    V5NativeReadback *confirmed)
{
    V5NativeReadback readback;
    int expected_wcs;
    unsigned int before_epoch;

    if (!page || !report || !confirmed) {
        return 0;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_wcs_status_read(0, V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS, &readback) ||
        !v5_native_readback_wcs_table_known(&readback)) {
        return 0;
    }
    if (report->request.kind == V5_COMMAND_WCS_SELECT) {
        expected_wcs = report->request.index_value;
        if (expected_wcs >= 0 && expected_wcs < (int)V5_NATIVE_READBACK_WCS_COUNT &&
            readback.wcs_index == expected_wcs) {
            *confirmed = readback;
            return 1;
        }
        return 0;
    }
    if (report->request.kind != V5_COMMAND_WORK_ZERO) {
        return 0;
    }
    expected_wcs = report->request.index_value - 1;
    before_epoch = report->wcs_offsets_epoch;
    if (expected_wcs < 0 || expected_wcs >= (int)V5_NATIVE_READBACK_WCS_COUNT ||
        readback.wcs_index != expected_wcs ||
        !v5_native_readback_wcs_table_known(&readback) ||
        (before_epoch != 0U && readback.wcs_offsets_epoch == before_epoch)) {
        return 0;
    }
    *confirmed = readback;
    return 1;
}

static int main_page_confirm_wcs_readback_after_send(V5MainPage *page, V5MainPageActionReport *report)
{
    unsigned int attempt;
    V5NativeReadback confirmed;

    if (!page || !report ||
        (report->request.kind != V5_COMMAND_WCS_SELECT && report->request.kind != V5_COMMAND_WORK_ZERO)) {
        return 1;
    }
    report->pending_readback = 1;
    snprintf(report->readback_code, sizeof(report->readback_code), "pending_native_readback");
    for (attempt = 0U; attempt < 12U; ++attempt) {
        main_page_sleep_ms(100U);
        if (main_page_confirm_wcs_readback_once(page, report, &confirmed)) {
            page->native_readback = confirmed;
            report->readback_confirmed = 1;
            report->pending_readback = 0;
            report->wcs_offsets_epoch = confirmed.wcs_offsets_epoch;
            report->active_wcs = confirmed.wcs_index;
            snprintf(report->readback_code, sizeof(report->readback_code), "native_readback_confirmed");
            update_main_page_wcs_header(page);
            update_wcs_button_visuals(page);
            return 1;
        }
    }
    report->readback_confirmed = 0;
    report->pending_readback = 1;
    snprintf(report->readback_code, sizeof(report->readback_code), "native_readback_not_confirmed");
    return 0;
}

static int main_page_refresh_safety_readback_from_gate(
    V5MainPage *page,
    int fallback_machine_known,
    int fallback_machine_enabled)
{
    V5NativeReadback readback;
    V5CommandGateResult gate_result;
    int estop_ok;
    int machine_ok;

    if (!page) {
        return 0;
    }
    readback = page->native_readback;
    readback.safety_estop_available = 0;
    readback.machine_enable_available = 0;
    v5_command_gate_result_init(&gate_result);
    (void)v5_command_gate_probe_safety(&gate_result, 500U);
    estop_ok = gate_result.safety_estop_known;
    machine_ok = gate_result.machine_enable_known;
    if (estop_ok) {
        v5_native_readback_set_safety_estop(&readback, gate_result.safety_estop_active);
    }
    if (machine_ok) {
        v5_native_readback_set_machine_enabled(&readback, gate_result.machine_enabled);
    } else if (fallback_machine_known) {
        v5_native_readback_set_machine_enabled(&readback, fallback_machine_enabled);
        machine_ok = 1;
    }
    page->native_readback = readback;
    update_estop_button_text(page);
    return estop_ok || machine_ok;
}


static void execute_prepared_command_if_enabled(V5MainPage *page, V5MainPageActionReport *report)
{
    V5CommandGateResult gate_result;
    unsigned int gate_timeout_ms = 1000U;
    int home_button_transaction = 0;

    if (!page || !report || report->local_only || !report->prepared || !report->command.accepted) {
        return;
    }
    if (strcmp(report->command.owner ? report->command.owner : "", "native_linuxcncrsh") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_home_mode_gate") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_safety") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_first_point") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_rotary_gate") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_rtcp_control") != 0) {
        report->send_status = 0;
        return;
    }
    if (!page->command_execution_enabled && report->request.kind != V5_COMMAND_ESTOP_FORCE) {
        report->send_status = 0;
        return;
    }

    v5_command_gate_result_init(&gate_result);
    if (report->request.kind == V5_COMMAND_ESTOP_RESET || report->request.kind == V5_COMMAND_ESTOP_FORCE) {
        gate_timeout_ms = 3000U;
    } else if (report->request.kind == V5_COMMAND_RTCP_SET) {
        gate_timeout_ms = 2500U;
    } else if (report->request.kind == V5_COMMAND_HOME) {
        gate_timeout_ms = 120000U;
    } else if (report->request.kind == V5_COMMAND_ROTARY_EQUIV_ZERO) {
        gate_timeout_ms = 5000U;
    }
    home_button_transaction = report->action == V5_MAIN_PAGE_ACTION_HOME &&
        (report->request.kind == V5_COMMAND_HOME || report->request.kind == V5_COMMAND_ROTARY_EQUIV_ZERO);
    if (home_button_transaction) {
        set_home_transaction_active(page, 1, 1);
    }
    if (!v5_command_gate_send_prepared(&report->command, &report->request, &gate_result, gate_timeout_ms)) {
        if (home_button_transaction) {
            set_home_transaction_active(page, 0, 1);
        }
        report->send_status = gate_result.send_status;
        return;
    }
    report->send_status = gate_result.send_status;
    report->executed = gate_result.executed && gate_result.send_status == V5_COMMAND_GATE_SEND_SENT;
    report->machine_on_status = gate_result.machine_on_status;
    report->machine_on_requested = gate_result.machine_on_requested;
    snprintf(report->command_line, sizeof(report->command_line), "%.*s",
             (int)sizeof(report->command_line) - 1, gate_result.command_line);
    snprintf(report->readback_code, sizeof(report->readback_code), "%.*s",
             (int)sizeof(report->readback_code) - 1, gate_result.readback_code);
    if (home_button_transaction) {
        set_home_transaction_active(page, 0, 1);
    }

    if (report->executed &&
        (report->request.kind == V5_COMMAND_WCS_SELECT || report->request.kind == V5_COMMAND_WORK_ZERO)) {
        report->executed = main_page_confirm_wcs_readback_after_send(page, report);
    }
    if (report->executed && strcmp(report->command.owner ? report->command.owner : "", "native_safety") == 0) {
        V5NativeReadback readback = page->native_readback;
        if (gate_result.safety_estop_known) {
            v5_native_readback_set_safety_estop(&readback, gate_result.safety_estop_active);
        }
        if (gate_result.machine_enable_known) {
            v5_native_readback_set_machine_enabled(&readback, gate_result.machine_enabled);
        }
        page->native_readback = readback;
        update_estop_button_text(page);
        if (!gate_result.safety_estop_known && !gate_result.machine_enable_known) {
            if (report->request.kind == V5_COMMAND_ESTOP_FORCE) {
                (void)main_page_refresh_safety_readback_from_gate(page, 1, 0);
            } else if (report->request.kind == V5_COMMAND_ESTOP_RESET) {
                (void)main_page_refresh_safety_readback_from_gate(page, 0, 0);
            }
        }
    }
    if (report->executed && report->request.kind == V5_COMMAND_RTCP_SET) {
        if (page->native_readback_refresh_cb) {
            page->native_readback_refresh_cb(page->native_readback_refresh_user_data, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE);
        } else {
            update_main_page_modal_label(page);
            update_main_page_state_button_visuals(page);
        }
    }
}


static void apply_local_action_state(V5MainPage *page, const V5MainPageActionReport *report)
{
    if (!page || !report || !report->local_only) {
        return;
    }
    switch (report->action) {
    case V5_MAIN_PAGE_ACTION_AXIS_ALL:
        v5_main_page_select_all_axes(page);
        break;
    case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
    case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
    case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
        page->jog_step = report->request.axis_value;
        break;
    case V5_MAIN_PAGE_ACTION_VIEW_XY:
        page->view_plane = V5_TOOLPATH_DISPLAY_XY;
        reset_toolpath_view_rotation(page);
        mark_toolpath_static_dirty(page);
        break;
    case V5_MAIN_PAGE_ACTION_VIEW_XZ:
        page->view_plane = V5_TOOLPATH_DISPLAY_XZ;
        reset_toolpath_view_rotation(page);
        mark_toolpath_static_dirty(page);
        break;
    case V5_MAIN_PAGE_ACTION_VIEW_YZ:
        page->view_plane = V5_TOOLPATH_DISPLAY_YZ;
        reset_toolpath_view_rotation(page);
        mark_toolpath_static_dirty(page);
        break;
    case V5_MAIN_PAGE_ACTION_VIEW_3D:
        page->view_plane = V5_TOOLPATH_DISPLAY_3D;
        reset_toolpath_view_rotation(page);
        mark_toolpath_static_dirty(page);
        break;
    default:
        break;
    }
    update_main_page_state_button_visuals(page);
    if (page->navigation_cb) {
        switch (report->action) {
        case V5_MAIN_PAGE_ACTION_NAV_MAIN:
        case V5_MAIN_PAGE_ACTION_NAV_TOOL:
        case V5_MAIN_PAGE_ACTION_NAV_PROBE:
        case V5_MAIN_PAGE_ACTION_NAV_OFFSET:
        case V5_MAIN_PAGE_ACTION_NAV_IO:
        case V5_MAIN_PAGE_ACTION_NAV_SETTINGS:
        case V5_MAIN_PAGE_ACTION_NAV_NETWORK:
        case V5_MAIN_PAGE_ACTION_NAV_PROGRAM:
        case V5_MAIN_PAGE_ACTION_NAV_MDI:
        case V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT:
            page->navigation_cb(page->navigation_user_data, report->action);
            break;
        default:
            break;
        }
    }
}

int v5_main_page_trigger_action(V5MainPage *page, V5MainPageActionKind action, V5MainPageActionReport *report)
{
    V5MainPageActionReport local_report;
    V5MainPageActionReport *out = report ? report : &local_report;
    const V5ProgramRuntime *runtime = 0;

    if (!page) {
        return 0;
    }
    if (action_needs_native_readback_refresh(action) && page->native_readback_refresh_cb) {
        page->native_readback_refresh_cb(page->native_readback_refresh_user_data, action);
    }
    if (page->program_controller) {
        runtime = v5_program_controller_runtime(page->program_controller);
    }
    if (!v5_main_page_action_prepare(action, runtime, &page->native_readback, &page->selection, page->jog_step, out)) {
        memset(out, 0, sizeof(*out));
        out->action = action;
        page->last_action = *out;
        return 0;
    }
    if (out->request.kind == V5_COMMAND_WORK_ZERO &&
        v5_native_readback_wcs_table_known(&page->native_readback)) {
        out->wcs_offsets_epoch = page->native_readback.wcs_offsets_epoch;
    }
    execute_prepared_command_if_enabled(page, out);
    apply_local_action_state(page, out);
    page->last_action = *out;
    return 1;
}
