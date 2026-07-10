#include "v5_coordinate_digits.h"
#include "v5_lvgl_headless.h"

#include <string.h>

static lv_color_t g_digits_buffer[V5_COORD_DIGITS_MAIN_W * V5_COORD_DIGITS_MAIN_H];

int main(void)
{
    V5CoordinateDigits digits;
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

    cached_text = digits.value_text[1][4];
    v5_lvgl_headless_reset_flush_count();
    if (!v5_coordinate_digits_set_value(&digits, 1U, 4U, cached_text, lv_color_make(245, 214, 82))) {
        return 4;
    }
    if (strcmp(digits.value_text[1][4], "+00359.998") != 0) {
        return 5;
    }
    lv_refr_now(lv_obj_get_disp(digits.canvas));
    if (v5_lvgl_headless_flush_count() == 0U) {
        return 6;
    }
    return 0;
}
