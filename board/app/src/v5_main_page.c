#include "v5_main_page.h"

#include "v5_command_gate_ipc.h"
#include "v5_button_visuals.h"
#include "v5_native_wcs_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_layout_icons.h"
#include "v5_lvgl_clock.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"
#include "v5_remote_metrics.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "v5_main_page_internal.h"

lv_color_t v5_main_page_internal_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

void v5_main_page_internal_set_label_text_if_changed(lv_obj_t *label, const char *text)
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

void v5_main_page_internal_set_obj_bg_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector)
{
    if (obj && !main_color_equal(lv_obj_get_style_bg_color(obj, selector), color)) {
        lv_obj_set_style_bg_color(obj, color, selector);
    }
}

void v5_main_page_internal_set_obj_border_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector)
{
    if (obj && !main_color_equal(lv_obj_get_style_border_color(obj, selector), color)) {
        lv_obj_set_style_border_color(obj, color, selector);
    }
}

void v5_main_page_internal_set_obj_text_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector)
{
    if (obj && !main_color_equal(lv_obj_get_style_text_color(obj, selector), color)) {
        lv_obj_set_style_text_color(obj, color, selector);
    }
}

static int point_equal(const lv_point_t *a, const lv_point_t *b)
{
    return a && b && a->x == b->x && a->y == b->y;
}

int v5_main_page_internal_points_equal(const lv_point_t *a, const lv_point_t *b, unsigned int count)
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

void v5_main_page_internal_add_hidden_flag_if_visible(lv_obj_t *obj)
{
    if (obj && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void v5_main_page_internal_clear_hidden_flag_if_hidden(lv_obj_t *obj)
{
    if (obj && lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void v5_main_page_internal_set_obj_pos_if_changed(lv_obj_t *obj, lv_coord_t x, lv_coord_t y)
{
    if (obj && (lv_obj_get_x(obj) != x || lv_obj_get_y(obj) != y)) {
        lv_obj_set_pos(obj, x, y);
    }
}

lv_coord_t v5_main_page_internal_clamp_coord(double value, lv_coord_t min_value, lv_coord_t max_value)
{
    if (value < (double)min_value) {
        return min_value;
    }
    if (value > (double)max_value) {
        return max_value;
    }
    return (lv_coord_t)value;
}

double v5_main_page_internal_clamp_double(double value, double min_value, double max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

double v5_main_page_internal_normalize_deg(double value)
{
    while (value > 360.0) {
        value -= 720.0;
    }
    while (value < -360.0) {
        value += 720.0;
    }
    return value;
}

double v5_main_page_internal_point_distance(const lv_point_t *a, const lv_point_t *b)
{
    const double dx = (double)b->x - (double)a->x;
    const double dy = (double)b->y - (double)a->y;
    return sqrt((dx * dx) + (dy * dy));
}

double v5_main_page_internal_point_angle_deg(const lv_point_t *a, const lv_point_t *b)
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

int v5_main_page_internal_toolpath_points_in_graphics_zone(const lv_point_t *points, int count)
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

int v5_main_page_internal_point_in_program_preview_zone(const lv_point_t *point)
{
    if (!point) {
        return 0;
    }
    return point->x >= V5_PROGRAM_PREVIEW_X &&
           point->x < V5_PROGRAM_PREVIEW_X + V5_PROGRAM_PREVIEW_W &&
           point->y >= V5_PROGRAM_PREVIEW_Y &&
           point->y < V5_PROGRAM_PREVIEW_Y + V5_PROGRAM_PREVIEW_H;
}

int v5_main_page_internal_main_page_handle_program_preview_touch(
    V5MainPage *page,
    const lv_point_t *point,
    int pressed,
    int *changed);

V5ToolpathScreenPoint v5_main_page_internal_apply_toolpath_view_transform_prepared(
    V5ToolpathScreenPoint point,
    const V5ToolpathViewTransform *transform)
{
    const double cx = (double)V5_TOOLPATH_W * 0.5;
    const double cy = (double)V5_TOOLPATH_H * 0.5;
    double dx;
    double dy;
    double rx;
    double ry;
    if (!transform || transform->identity) {
        return point;
    }
    dx = point.x - cx;
    dy = point.y - cy;
    rx = (dx * transform->cosine) - (dy * transform->sine);
    ry = (dx * transform->sine) + (dy * transform->cosine);
    point.x = cx + (rx * transform->scale) + transform->pan_x;
    point.y = cy + (ry * transform->scale) + transform->pan_y;
    return point;
}

void v5_main_page_internal_prepare_toolpath_view_transform(
    const V5MainPage *page,
    V5ToolpathViewTransform *transform)
{
    double rad;
    if (!transform) {
        return;
    }
    memset(transform, 0, sizeof(*transform));
    transform->scale =
        page && page->toolpath_manual_scale > 0.0 ?
            page->toolpath_manual_scale :
            1.0;
    transform->cosine = 1.0;
    if (!page) {
        transform->identity = 1;
        return;
    }
    transform->pan_x = page->toolpath_manual_pan_x;
    transform->pan_y = page->toolpath_manual_pan_y;
    transform->identity =
        fabs(transform->scale - 1.0) <= 1.0e-12 &&
        fabs(page->toolpath_manual_rotate_deg) <= 1.0e-12 &&
        fabs(page->toolpath_manual_pan_x) <= 1.0e-12 &&
        fabs(page->toolpath_manual_pan_y) <= 1.0e-12;
    if (transform->identity) {
        return;
    }
    rad = page->toolpath_manual_rotate_deg * M_PI / 180.0;
    transform->sine = sin(rad);
    transform->cosine = cos(rad);
}

V5ToolpathScreenPoint v5_main_page_internal_apply_toolpath_view_transform(
    const V5MainPage *page,
    V5ToolpathScreenPoint point)
{
    V5ToolpathViewTransform transform;
    v5_main_page_internal_prepare_toolpath_view_transform(page, &transform);
    return v5_main_page_internal_apply_toolpath_view_transform_prepared(
        point, &transform);
}

void v5_main_page_internal_apply_toolpath_view_transform_points(
    const V5MainPage *page,
    V5ToolpathScreenPoint *points,
    unsigned int count)
{
    V5ToolpathViewTransform transform;
    unsigned int i;
    if (!page || !points || count == 0U) {
        return;
    }
    v5_main_page_internal_prepare_toolpath_view_transform(page, &transform);
    if (transform.identity) {
        return;
    }
    for (i = 0U; i < count; ++i) {
        points[i] =
            v5_main_page_internal_apply_toolpath_view_transform_prepared(
                points[i], &transform);
    }
}

void v5_main_page_internal_apply_toolpath_view_transform_to_snapshot(const V5MainPage *page, V5ToolpathDisplaySnapshot *display)
{
    if (!page || !display) {
        return;
    }
    if (display->trajectory_valid) {
        v5_main_page_internal_apply_toolpath_view_transform_points(
            page, display->trajectory, display->point_count);
    }
    if (display->mcs_valid) {
        display->mcs_point = v5_main_page_internal_apply_toolpath_view_transform(page, display->mcs_point);
    }
    if (display->cmd_valid) {
        display->cmd_point = v5_main_page_internal_apply_toolpath_view_transform(page, display->cmd_point);
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

int v5_main_page_internal_main_page_axis_values_finite(const double axis[V5_STATUS_AXIS_COUNT])
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

void v5_main_page_internal_main_page_fit_expand_world_point(V5ToolpathDisplayFit *fit, const double axis[V5_STATUS_AXIS_COUNT])
{
    double u;
    double v;
    if (!fit || !fit->valid || !v5_main_page_internal_main_page_axis_values_finite(axis)) {
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

int v5_main_page_internal_main_page_tool_length_mm(const V5MainPage *page, double *out);

const V5MotionModelDescriptor *v5_main_page_internal_main_page_active_motion_model(const V5MainPage *page)
{
    if (!page || !v5_native_readback_motion_model_known(&page->native_readback)) {
        return 0;
    }
    return v5_motion_model_find(page->native_readback.motion_model);
}

char v5_main_page_internal_main_page_axis_display_char(const V5MainPage *page, unsigned int axis_index)
{
    static const char linear_axes[3] = {'X', 'Y', 'Z'};
    const V5MotionModelDescriptor *model;
    char axis = '\0';
    if (axis_index >= V5_MAIN_PAGE_AXIS_COUNT) {
        return '-';
    }
    if (axis_index < 3U) {
        return linear_axes[axis_index];
    }
    model = v5_main_page_internal_main_page_active_motion_model(page);
    if (!model || !v5_motion_model_axis_for_status_slot(model, axis_index, &axis)) {
        return '-';
    }
    return axis;
}
