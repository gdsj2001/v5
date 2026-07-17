#include "v5_coordinate_digits.h"
#include "v5_lvgl_headless.h"

#include <stdio.h>
#include <string.h>

static lv_color_t g_digits_buffer[V5_COORD_DIGITS_MAIN_W * V5_COORD_DIGITS_MAIN_H];

int main(void)
{
    V5CoordinateDigits digits;
    lv_disp_t *display;
    lv_area_t canvas_coords;
    lv_area_t expected;
    const char *cached_text;

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
    return 0;
}
