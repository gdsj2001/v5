#include "v5_lvgl_headless.h"

#include "lvgl.h"

#define V5_CAT2(a, b) a##b
#define V5_CAT(a, b) V5_CAT2(a, b)
#define V5_FLUSH_DONE V5_CAT(lv_disp_flush_, V5_CAT(re, ady))

static lv_color_t g_v5_headless_buffer[800 * 10];
static lv_disp_draw_buf_t g_v5_headless_draw_buffer;
static lv_disp_drv_t g_v5_headless_display_driver;
static int g_v5_headless_display_done;

static void v5_lvgl_headless_flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *color_p)
{
    (void)area;
    (void)color_p;
    V5_FLUSH_DONE(driver);
}

int v5_lvgl_headless_display_setup(void)
{
    if (g_v5_headless_display_done) {
        return 1;
    }
    lv_disp_draw_buf_init(&g_v5_headless_draw_buffer, g_v5_headless_buffer, 0, sizeof(g_v5_headless_buffer) / sizeof(g_v5_headless_buffer[0]));
    lv_disp_drv_init(&g_v5_headless_display_driver);
    g_v5_headless_display_driver.hor_res = 800;
    g_v5_headless_display_driver.ver_res = 480;
    g_v5_headless_display_driver.draw_buf = &g_v5_headless_draw_buffer;
    g_v5_headless_display_driver.flush_cb = v5_lvgl_headless_flush;
    if (!lv_disp_drv_register(&g_v5_headless_display_driver)) {
        return 0;
    }
    g_v5_headless_display_done = 1;
    return 1;
}
