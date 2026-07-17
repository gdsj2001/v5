#include "v5_coordinate_digits.h"

#include <stdio.h>
#include <string.h>

#define V5_COORD_DIGIT_BG_R 6
#define V5_COORD_DIGIT_BG_G 26
#define V5_COORD_DIGIT_BG_B 39
#define V5_COORD_MAIN_MCS_X 0
#define V5_COORD_MAIN_WCS_X 130
#define V5_COORD_VALUE_W 124
#define V5_COORD_DOT_W 5
#define V5_COORD_DIGIT_ADVANCE 12
#define V5_COORD_FRAC_DIGITS 3

static lv_color_t color_rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return lv_color_make(r, g, b);
}

static int color_equal(lv_color_t left, lv_color_t right)
{
    return memcmp(&left, &right, sizeof(left)) == 0;
}

static void fill_rect(lv_color_t *buf, int canvas_w, int canvas_h, int x, int y, int w, int h, lv_color_t color)
{
    int x0;
    int y0;
    int x1;
    int y1;
    if (!buf || canvas_w <= 0 || canvas_h <= 0 || w <= 0 || h <= 0) {
        return;
    }
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = (x + w) > canvas_w ? canvas_w : (x + w);
    y1 = (y + h) > canvas_h ? canvas_h : (y + h);
    for (int yy = y0; yy < y1; ++yy) {
        lv_color_t *row = &buf[yy * canvas_w];
        for (int xx = x0; xx < x1; ++xx) {
            row[xx] = color;
        }
    }
}

static int digit_segments(char ch)
{
    switch (ch) {
    case '0': return 0x3F;
    case '1': return 0x06;
    case '2': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': return 0x6F;
    default: return 0;
    }
}

static int text_width(const char *text)
{
    int width = 0;
    const char *p;
    for (p = text ? text : ""; *p; ++p) {
        if (*p == '.') {
            width += 5;
        } else if (*p == '-' || *p == '<' || *p == '>') {
            width += 9;
        } else {
            width += 12;
        }
    }
    return width;
}

static int draw_char(lv_color_t *buf, int canvas_w, int canvas_h, int x, int y, char ch, lv_color_t color)
{
    const int t = 2;
    const int w = 10;
    const int h = 20;
    if (ch >= '0' && ch <= '9') {
        int s = digit_segments(ch);
        if (s & 0x01) fill_rect(buf, canvas_w, canvas_h, x + 2, y, w - 4, t, color);
        if (s & 0x02) fill_rect(buf, canvas_w, canvas_h, x + w - t, y + 2, t, 7, color);
        if (s & 0x04) fill_rect(buf, canvas_w, canvas_h, x + w - t, y + 11, t, 7, color);
        if (s & 0x08) fill_rect(buf, canvas_w, canvas_h, x + 2, y + h - t, w - 4, t, color);
        if (s & 0x10) fill_rect(buf, canvas_w, canvas_h, x, y + 11, t, 7, color);
        if (s & 0x20) fill_rect(buf, canvas_w, canvas_h, x, y + 2, t, 7, color);
        if (s & 0x40) fill_rect(buf, canvas_w, canvas_h, x + 2, y + 9, w - 4, t, color);
        return 12;
    }
    if (ch == '.') {
        fill_rect(buf, canvas_w, canvas_h, x + 1, y + h - 3, 3, 3, color);
        return 5;
    }
    if (ch == '-') {
        fill_rect(buf, canvas_w, canvas_h, x + 1, y + 9, 7, t, color);
        return 9;
    }
    if (ch == '<') {
        fill_rect(buf, canvas_w, canvas_h, x + 5, y + 4, 2, 2, color);
        fill_rect(buf, canvas_w, canvas_h, x + 3, y + 6, 2, 2, color);
        fill_rect(buf, canvas_w, canvas_h, x + 1, y + 8, 2, 2, color);
        fill_rect(buf, canvas_w, canvas_h, x + 3, y + 10, 2, 2, color);
        fill_rect(buf, canvas_w, canvas_h, x + 5, y + 12, 2, 2, color);
        return 9;
    }
    if (ch == '>') {
        fill_rect(buf, canvas_w, canvas_h, x + 1, y + 4, 2, 2, color);
        fill_rect(buf, canvas_w, canvas_h, x + 3, y + 6, 2, 2, color);
        fill_rect(buf, canvas_w, canvas_h, x + 5, y + 8, 2, 2, color);
        fill_rect(buf, canvas_w, canvas_h, x + 3, y + 10, 2, 2, color);
        fill_rect(buf, canvas_w, canvas_h, x + 1, y + 12, 2, 2, color);
        return 9;
    }
    return 6;
}

static int chars_width_until(const char *begin, const char *end)
{
    int width = 0;
    const char *p;
    if (!begin || !end || end < begin) {
        return 0;
    }
    for (p = begin; p < end; ++p) {
        char ch = *p;
        if (ch == '.') {
            width += V5_COORD_DOT_W;
        } else if (ch == '-' || ch == '<' || ch == '>') {
            width += 9;
        } else if (ch >= '0' && ch <= '9') {
            width += V5_COORD_DIGIT_ADVANCE;
        } else if (ch == '+') {
            width += 6;
        } else {
            width += 6;
        }
    }
    return width;
}

static void draw_value(V5CoordinateDigits *digits, int base_x, int axis, const char *text, lv_color_t color)
{
    int y;
    int x;
    int dot_x;
    const char *safe = text ? text : "";
    const char *dot;
    const char *p;
    if (!digits || !digits->buffer || axis < 0 || axis >= (int)V5_COORD_DIGITS_AXIS_COUNT) {
        return;
    }
    y = axis * digits->row_step + 5;
    dot = strchr(safe, '.');
    if (!dot) {
        x = base_x + digits->value_width - text_width(safe);
        for (p = safe; *p; ++p) {
            x += draw_char(digits->buffer, digits->width, digits->height, x, y, *p, color);
        }
        return;
    }
    dot_x = base_x + digits->value_width - (V5_COORD_DOT_W + V5_COORD_FRAC_DIGITS * V5_COORD_DIGIT_ADVANCE);
    x = dot_x - chars_width_until(safe, dot);
    for (p = safe; p < dot; ++p) {
        x += draw_char(digits->buffer, digits->width, digits->height, x, y, *p, color);
    }
    (void)draw_char(digits->buffer, digits->width, digits->height, dot_x, y, '.', color);
    x = dot_x + V5_COORD_DOT_W;
    for (p = dot + 1; *p; ++p) {
        x += draw_char(digits->buffer, digits->width, digits->height, x, y, *p, color);
    }
}

static int create_canvas(V5CoordinateDigits *digits, lv_obj_t *parent, lv_color_t *buffer)
{
    if (!digits || !parent || !buffer || digits->width <= 0 || digits->height <= 0) {
        return 0;
    }
    digits->buffer = buffer;
    digits->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(digits->canvas, buffer, digits->width, digits->height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_clear_flag(digits->canvas, LV_OBJ_FLAG_SCROLLABLE);
    fill_rect(buffer, digits->width, digits->height, 0, 0, digits->width, digits->height, color_rgb(V5_COORD_DIGIT_BG_R, V5_COORD_DIGIT_BG_G, V5_COORD_DIGIT_BG_B));
    memset(digits->value_text, 0, sizeof(digits->value_text));
    memset(digits->value_valid, 0, sizeof(digits->value_valid));
    return 1;
}

void v5_coordinate_digits_init(V5CoordinateDigits *digits)
{
    if (digits) {
        memset(digits, 0, sizeof(*digits));
    }
}

int v5_coordinate_digits_create_main(V5CoordinateDigits *digits, lv_obj_t *parent, lv_color_t *buffer)
{
    v5_coordinate_digits_init(digits);
    if (!digits) {
        return 0;
    }
    digits->width = V5_COORD_DIGITS_MAIN_W;
    digits->height = V5_COORD_DIGITS_MAIN_H;
    digits->value_width = V5_COORD_VALUE_W;
    digits->row_step = 32;
    digits->row_height = 30;
    digits->col_count = 2;
    digits->col_x[0] = V5_COORD_MAIN_MCS_X;
    digits->col_x[1] = V5_COORD_MAIN_WCS_X;
    if (!create_canvas(digits, parent, buffer)) {
        return 0;
    }
    lv_obj_set_pos(digits->canvas, 449, 113);
    return 1;
}

int v5_coordinate_digits_create_settings(V5CoordinateDigits *digits, lv_obj_t *parent, lv_color_t *buffer)
{
    v5_coordinate_digits_init(digits);
    if (!digits) {
        return 0;
    }
    digits->width = V5_COORD_DIGITS_SETTINGS_W;
    digits->height = V5_COORD_DIGITS_SETTINGS_H;
    digits->value_width = V5_COORD_VALUE_W;
    digits->row_step = 25;
    digits->row_height = 25;
    digits->col_count = 1;
    digits->col_x[0] = 0;
    if (!create_canvas(digits, parent, buffer)) {
        return 0;
    }
    lv_obj_set_pos(digits->canvas, 76, 39);
    return 1;
}

void v5_coordinate_digits_begin_update(V5CoordinateDigits *digits)
{
    if (!digits) {
        return;
    }
    if (digits->update_batch_depth == 0U) {
        digits->update_dirty_valid = 0U;
        memset(&digits->update_dirty, 0, sizeof(digits->update_dirty));
    }
    ++digits->update_batch_depth;
}

static void invalidate_value_area(V5CoordinateDigits *digits, const lv_area_t *dirty)
{
    if (!digits || !digits->canvas || !dirty) {
        return;
    }
    if (digits->update_batch_depth == 0U) {
        lv_obj_invalidate_area(digits->canvas, dirty);
        return;
    }
    if (!digits->update_dirty_valid) {
        digits->update_dirty = *dirty;
        digits->update_dirty_valid = 1U;
        return;
    }
    if (dirty->x1 < digits->update_dirty.x1) digits->update_dirty.x1 = dirty->x1;
    if (dirty->y1 < digits->update_dirty.y1) digits->update_dirty.y1 = dirty->y1;
    if (dirty->x2 > digits->update_dirty.x2) digits->update_dirty.x2 = dirty->x2;
    if (dirty->y2 > digits->update_dirty.y2) digits->update_dirty.y2 = dirty->y2;
}

void v5_coordinate_digits_end_update(V5CoordinateDigits *digits)
{
    if (!digits || digits->update_batch_depth == 0U) {
        return;
    }
    --digits->update_batch_depth;
    if (digits->update_batch_depth == 0U && digits->update_dirty_valid) {
        lv_obj_invalidate_area(digits->canvas, &digits->update_dirty);
        digits->update_dirty_valid = 0U;
    }
}

int v5_coordinate_digits_set_value(V5CoordinateDigits *digits, unsigned int col, unsigned int axis, const char *text, lv_color_t color)
{
    char text_snapshot[24];
    char *cached_text;
    lv_color_t *cached_color;
    unsigned char *cached_valid;
    const char *safe;
    int base_x;
    int y;
    if (!digits || !digits->canvas || !digits->buffer || col >= (unsigned int)digits->col_count || axis >= V5_COORD_DIGITS_AXIS_COUNT) {
        return 0;
    }
    snprintf(text_snapshot, sizeof(text_snapshot), "%s", text ? text : "");
    safe = text_snapshot;
    cached_text = digits->value_text[col][axis];
    cached_color = &digits->value_color[col][axis];
    cached_valid = &digits->value_valid[col][axis];
    if (*cached_valid && strcmp(cached_text, safe) == 0 && color_equal(*cached_color, color)) {
        return 0;
    }
    base_x = digits->col_x[col];
    y = (int)axis * digits->row_step;
    fill_rect(digits->buffer, digits->width, digits->height, base_x, y, digits->value_width, digits->row_height, color_rgb(V5_COORD_DIGIT_BG_R, V5_COORD_DIGIT_BG_G, V5_COORD_DIGIT_BG_B));
    draw_value(digits, base_x, (int)axis, safe, color);
    snprintf(cached_text, 24, "%s", safe);
    *cached_color = color;
    *cached_valid = 1U;
    {
        lv_area_t coords;
        lv_area_t dirty;
        lv_obj_get_coords(digits->canvas, &coords);
        dirty.x1 = coords.x1 + (lv_coord_t)base_x;
        dirty.y1 = coords.y1 + (lv_coord_t)y;
        dirty.x2 = dirty.x1 + (lv_coord_t)digits->value_width - 1;
        dirty.y2 = dirty.y1 + (lv_coord_t)digits->row_height - 1;
        invalidate_value_area(digits, &dirty);
    }
    return 1;
}

void v5_coordinate_digits_invalidate(V5CoordinateDigits *digits)
{
    if (digits && digits->canvas) {
        lv_obj_invalidate(digits->canvas);
    }
}
