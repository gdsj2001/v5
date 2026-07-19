#include "v5_coordinate_digits.h"
#include "v5_lvgl_headless.h"

#include <stdio.h>
#include <string.h>

static lv_color_t g_digits_buffer[V5_COORD_DIGITS_MAIN_W * V5_COORD_DIGITS_MAIN_H];

static int pixel_is(const V5CoordinateDigits *digits, int x, int y, lv_color_t expected)
{
    const lv_color_t *pixel;
    if (!digits || !digits->buffer ||
        x < 0 || x >= digits->width || y < 0 || y >= digits->height) {
        return 0;
    }
    pixel = &digits->buffer[y * digits->width + x];
    return memcmp(pixel, &expected, sizeof(expected)) == 0;
}

int main(void)
{
    V5CoordinateDigits digits;
    lv_disp_t *display;
    lv_area_t canvas_coords;
    lv_area_t expected;
    const char *cached_text;
    const lv_color_t digit_color = lv_color_make(86, 204, 252);

    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    if (!v5_coordinate_digits_create_main(&digits, lv_scr_act(), g_digits_buffer)) {
        return 2;
    }
    if (!v5_coordinate_digits_set_value(&digits, 1U, 4U, "+00359.998", lv_color_make(68, 221, 144))) {
        return 3;
    }
    lv_refr_now(NULL);
    display = lv_obj_get_disp(digits.canvas);
    if (!display || display->inv_p != 0U) {
        return 4;
    }

    v5_coordinate_digits_begin_update(&digits);
    v5_coordinate_digits_begin_update(&digits);
    if (!v5_coordinate_digits_set_value(&digits, 0U, 0U, "+00001.000", lv_color_make(86, 204, 252)) ||
        !v5_coordinate_digits_set_value(&digits, 1U, 4U, "+00359.998", lv_color_make(245, 214, 82)) ||
        display->inv_p != 0U) {
        return 5;
    }
    v5_coordinate_digits_end_update(&digits);
    if (display->inv_p != 0U) {
        return 6;
    }
    v5_coordinate_digits_end_update(&digits);
    if (display->inv_p != 1U) {
        return 7;
    }
    lv_obj_get_coords(digits.canvas, &canvas_coords);
    expected.x1 = canvas_coords.x1 + digits.col_x[0];
    expected.y1 = canvas_coords.y1;
    expected.x2 = canvas_coords.x1 + digits.col_x[1] + (lv_coord_t)digits.value_width - 1;
    expected.y2 = canvas_coords.y1 + (4 * digits.row_step) + (lv_coord_t)digits.row_height - 1;
    expected.x1 -= 5;
    expected.y1 -= 5;
    expected.x2 += 5;
    expected.y2 += 5;
    if (memcmp(&display->inv_areas[0], &expected, sizeof(expected)) != 0) {
        fprintf(
            stderr,
            "coordinate batch dirty mismatch actual=%d,%d,%d,%d expected=%d,%d,%d,%d\n",
            (int)display->inv_areas[0].x1,
            (int)display->inv_areas[0].y1,
            (int)display->inv_areas[0].x2,
            (int)display->inv_areas[0].y2,
            (int)expected.x1,
            (int)expected.y1,
            (int)expected.x2,
            (int)expected.y2);
        return 8;
    }
    lv_refr_now(display);
    if (display->inv_p != 0U) {
        return 9;
    }
    v5_coordinate_digits_begin_update(&digits);
    if (v5_coordinate_digits_set_value(&digits, 0U, 0U, "+00001.000", lv_color_make(86, 204, 252)) != 0 ||
        v5_coordinate_digits_set_value(&digits, 1U, 4U, "+00359.998", lv_color_make(245, 214, 82)) != 0) {
        return 10;
    }
    v5_coordinate_digits_end_update(&digits);
    if (display->inv_p != 0U) {
        return 11;
    }

    cached_text = digits.value_text[1][4];
    v5_lvgl_headless_reset_flush_count();
    if (!v5_coordinate_digits_set_value(&digits, 1U, 4U, cached_text, lv_color_make(68, 221, 144))) {
        return 12;
    }
    if (strcmp(digits.value_text[1][4], "+00359.998") != 0) {
        return 13;
    }
    lv_refr_now(lv_obj_get_disp(digits.canvas));
    if (v5_lvgl_headless_flush_count() == 0U) {
        return 14;
    }
    {
        const unsigned int axis = 1U;
        const int y = (int)axis * digits.row_step + 5;
        const int dot_x = digits.col_x[0] + digits.value_width - (5 + 3 * 12);
        const int sign_x = dot_x - (3 * 12 + 9);
        const int first_whole_x = dot_x - 3 * 12;
        const int last_whole_x = dot_x - 12;
        if (!v5_coordinate_digits_set_value(
                &digits, 0U, axis, "+00001.000", digit_color) ||
            !pixel_is(&digits, first_whole_x + 2, y, digit_color) ||
            !pixel_is(&digits, last_whole_x + 8, y + 2, digit_color) ||
            !pixel_is(&digits, dot_x + 1, y + 17, digit_color)) {
            fprintf(stderr, "coordinate fixed three-whole-digit mismatch\n");
            return 15;
        }
        if (!v5_coordinate_digits_set_value(
                &digits, 0U, axis, "-00001.000", digit_color) ||
            !pixel_is(&digits, sign_x + 1, y + 9, digit_color) ||
            !pixel_is(&digits, first_whole_x + 2, y, digit_color) ||
            !pixel_is(&digits, last_whole_x + 8, y + 2, digit_color) ||
            !pixel_is(&digits, dot_x + 1, y + 17, digit_color)) {
            fprintf(stderr, "coordinate fixed-minus-slot mismatch\n");
            return 16;
        }
        if (!v5_coordinate_digits_set_value(
                &digits, 0U, axis, "-00123.000", digit_color) ||
            !pixel_is(&digits, sign_x + 1, y + 9, digit_color) ||
            !pixel_is(&digits, first_whole_x + 8, y + 2, digit_color) ||
            !pixel_is(&digits, dot_x + 1, y + 17, digit_color)) {
            fprintf(stderr, "coordinate minus moved with whole digits\n");
            return 17;
        }
        if (!v5_coordinate_digits_set_value(
                &digits, 0U, axis, "+01000.000", digit_color) ||
            !pixel_is(&digits, sign_x + 1, y + 4, digit_color) ||
            !pixel_is(&digits, first_whole_x + 2, y, digit_color) ||
            !pixel_is(&digits, dot_x + 1, y + 17, digit_color) ||
            !pixel_is(&digits, dot_x + 5 + 2, y, digit_color)) {
            fprintf(stderr, "coordinate fixed-width overflow mismatch\n");
            return 18;
        }
    }
    return 0;
}
