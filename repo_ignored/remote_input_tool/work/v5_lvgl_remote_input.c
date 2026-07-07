#include "v5_lvgl_remote_input.h"

#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static lv_indev_drv_t g_remote_driver;
static lv_indev_t *g_remote_indev;
static lv_point_t g_remote_point;
static lv_indev_state_t g_remote_state = LV_INDEV_STATE_REL;

static double monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

const char *v5_lvgl_remote_input_enabled_mode(void)
{
    const char *mode = getenv("V5_UI_REMOTE_INPUT");
    if (!mode || !mode[0]) {
        return "off";
    }
    return mode;
}

static int remote_input_allowed(void)
{
    return strcmp(v5_lvgl_remote_input_enabled_mode(), "layout_only") == 0;
}

static void remote_input_read_cb(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    data->point = g_remote_point;
    data->state = g_remote_state;
}

static void pump_lvgl(unsigned int ms)
{
    unsigned int elapsed = 0;
    do {
        lv_tick_inc(10);
        lv_timer_handler();
        lv_refr_now(0);
        elapsed += 10;
    } while (elapsed < ms);
}

static void log_remote_input_event(const char *kind, int x1, int y1, int x2, int y2, int ok)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/remote_input_events.jsonl", "ab");
    if (!fp) {
        return;
    }
    fprintf(fp,
            "{\"schema\":\"v5.remote_input.v1\",\"source\":\"v5_lvgl_shell\","
            "\"time_monotonic_s\":%.6f,\"mode\":\"layout_only\",\"kind\":\"%s\","
            "\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d,\"ok\":%s,"
            "\"scope\":\"non_motion_ui_layout\"}\n",
            monotonic_seconds(),
            kind ? kind : "",
            x1,
            y1,
            x2,
            y2,
            ok ? "true" : "false");
    fclose(fp);
}

int v5_lvgl_remote_input_setup(void)
{
    if (g_remote_indev) {
        return 1;
    }
    g_remote_point.x = 0;
    g_remote_point.y = 0;
    g_remote_state = LV_INDEV_STATE_REL;
    lv_indev_drv_init(&g_remote_driver);
    g_remote_driver.type = LV_INDEV_TYPE_POINTER;
    g_remote_driver.read_cb = remote_input_read_cb;
    g_remote_indev = lv_indev_drv_register(&g_remote_driver);
    return g_remote_indev ? 1 : 0;
}

int v5_lvgl_remote_input_layout_click(int x, int y)
{
    if (!remote_input_allowed() || !g_remote_indev || x < 0 || y < 0) {
        log_remote_input_event("click", x, y, x, y, 0);
        return 0;
    }
    g_remote_point.x = (lv_coord_t)x;
    g_remote_point.y = (lv_coord_t)y;
    g_remote_state = LV_INDEV_STATE_PR;
    pump_lvgl(50);
    g_remote_state = LV_INDEV_STATE_REL;
    pump_lvgl(90);
    log_remote_input_event("click", x, y, x, y, 1);
    return 1;
}

int v5_lvgl_remote_input_layout_drag(int x1, int y1, int x2, int y2, int steps)
{
    int i;
    if (!remote_input_allowed() || !g_remote_indev || x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0) {
        log_remote_input_event("drag", x1, y1, x2, y2, 0);
        return 0;
    }
    if (steps < 2) {
        steps = 8;
    }
    if (steps > 40) {
        steps = 40;
    }
    g_remote_point.x = (lv_coord_t)x1;
    g_remote_point.y = (lv_coord_t)y1;
    g_remote_state = LV_INDEV_STATE_PR;
    pump_lvgl(40);
    for (i = 1; i <= steps; ++i) {
        g_remote_point.x = (lv_coord_t)(x1 + ((x2 - x1) * i) / steps);
        g_remote_point.y = (lv_coord_t)(y1 + ((y2 - y1) * i) / steps);
        pump_lvgl(25);
    }
    g_remote_state = LV_INDEV_STATE_REL;
    pump_lvgl(90);
    log_remote_input_event("drag", x1, y1, x2, y2, 1);
    return 1;
}
