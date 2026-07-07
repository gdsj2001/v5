#ifndef V5_COORDINATE_DIGITS_H
#define V5_COORDINATE_DIGITS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_COORD_DIGITS_AXIS_COUNT 5u
#define V5_COORD_DIGITS_COL_COUNT 2u
#define V5_COORD_DIGITS_MAIN_W 254
#define V5_COORD_DIGITS_MAIN_H 158
#define V5_COORD_DIGITS_SETTINGS_W 124
#define V5_COORD_DIGITS_SETTINGS_H 130

typedef struct V5CoordinateDigits {
    lv_obj_t *canvas;
    lv_color_t *buffer;
    int width;
    int height;
    int value_width;
    int row_step;
    int row_height;
    int col_count;
    int col_x[V5_COORD_DIGITS_COL_COUNT];
    char value_text[V5_COORD_DIGITS_COL_COUNT][V5_COORD_DIGITS_AXIS_COUNT][24];
    lv_color_t value_color[V5_COORD_DIGITS_COL_COUNT][V5_COORD_DIGITS_AXIS_COUNT];
    unsigned char value_valid[V5_COORD_DIGITS_COL_COUNT][V5_COORD_DIGITS_AXIS_COUNT];
} V5CoordinateDigits;

void v5_coordinate_digits_init(V5CoordinateDigits *digits);
int v5_coordinate_digits_create_main(V5CoordinateDigits *digits, lv_obj_t *parent, lv_color_t *buffer);
int v5_coordinate_digits_create_settings(V5CoordinateDigits *digits, lv_obj_t *parent, lv_color_t *buffer);
int v5_coordinate_digits_set_value(V5CoordinateDigits *digits, unsigned int col, unsigned int axis, const char *text, lv_color_t color);
void v5_coordinate_digits_invalidate(V5CoordinateDigits *digits);

#ifdef __cplusplus
}
#endif

#endif
