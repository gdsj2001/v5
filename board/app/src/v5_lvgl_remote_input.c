#include "v5_lvgl_remote_input.h"

#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define V5_REMOTE_CLICK_MOVE_THRESHOLD 12

static lv_indev_drv_t g_remote_driver;
static lv_indev_t *g_remote_indev;
static lv_point_t g_remote_point;
static lv_point_t g_remote_down_point;
static int g_remote_active;
static int g_remote_pressed;
static int g_remote_seen_press;
static int g_remote_release_pending;
static int g_remote_dragging;

static double monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static int abs_i(int value)
{
    return value < 0 ? -value : value;
}

const char *v5_lvgl_remote_input_enabled_mode(void)
{
    const char *mode = getenv("V5_UI_REMOTE_INPUT");
    if (!mode || !mode[0]) {
        return "off";
    }
    return mode;
}

int v5_lvgl_remote_input_accepts_pointer(void)
{
    return strcmp(v5_lvgl_remote_input_enabled_mode(), "layout_only") == 0;
}

static void remote_input_read_cb(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    data->point = g_remote_point;
    if (!g_remote_active) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (g_remote_pressed) {
        data->state = LV_INDEV_STATE_PR;
        g_remote_seen_press = 1;
        if (g_remote_release_pending) {
            g_remote_pressed = 0;
            g_remote_release_pending = 0;
        }
        return;
    }
    data->state = LV_INDEV_STATE_REL;
    g_remote_active = 0;
    g_remote_seen_press = 0;
    g_remote_release_pending = 0;
    g_remote_dragging = 0;
}

static void pump_lvgl(unsigned int ms)
{
    unsigned int elapsed = 0;
    do {
        lv_tick_inc(10);
        lv_timer_handler();
        elapsed += 10;
    } while (elapsed < ms);
}

static void log_remote_input_event(const char *phase, int x, int y, int ok)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/remote_input_events.jsonl", "ab");
    if (!fp) {
        return;
    }
    fprintf(fp,
            "{\"schema\":\"v5.remote_input.v1\",\"source\":\"v5_lvgl_shell\","
            "\"time_monotonic_s\":%.6f,\"protocol\":\"v3_pointer_ws\","
            "\"mode\":\"layout_only\",\"phase\":\"%s\",\"x\":%d,\"y\":%d,"
            "\"ok\":%s,\"scope\":\"non_motion_ui_layout\"}\n",
            monotonic_seconds(),
            phase ? phase : "",
            x,
            y,
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
    g_remote_down_point = g_remote_point;
    g_remote_active = 0;
    g_remote_pressed = 0;
    lv_indev_drv_init(&g_remote_driver);
    g_remote_driver.type = LV_INDEV_TYPE_POINTER;
    g_remote_driver.read_cb = remote_input_read_cb;
    g_remote_indev = lv_indev_drv_register(&g_remote_driver);
    return g_remote_indev ? 1 : 0;
}

int v5_lvgl_remote_input_pointer_event(const char *phase, int x, int y)
{
    unsigned int pump_ms = 30U;
    int dx;
    int dy;
    if (!v5_lvgl_remote_input_accepts_pointer() || !g_remote_indev || !phase || x < 0 || y < 0) {
        log_remote_input_event(phase, x, y, 0);
        return 0;
    }
    if (strcmp(phase, "down") == 0) {
        g_remote_point.x = (lv_coord_t)x;
        g_remote_point.y = (lv_coord_t)y;
        g_remote_down_point = g_remote_point;
        g_remote_active = 1;
        g_remote_pressed = 1;
        g_remote_seen_press = 0;
        g_remote_release_pending = 0;
        g_remote_dragging = 0;
        pump_ms = 60U;
    } else if (strcmp(phase, "move") == 0) {
        if (!g_remote_active || !g_remote_pressed) {
            log_remote_input_event(phase, x, y, 0);
            return 0;
        }
        dx = abs_i(x - (int)g_remote_down_point.x);
        dy = abs_i(y - (int)g_remote_down_point.y);
        if (dx > V5_REMOTE_CLICK_MOVE_THRESHOLD || dy > V5_REMOTE_CLICK_MOVE_THRESHOLD) {
            g_remote_dragging = 1;
        }
        if (g_remote_dragging) {
            g_remote_point.x = (lv_coord_t)x;
            g_remote_point.y = (lv_coord_t)y;
        }
        g_remote_active = 1;
        g_remote_pressed = 1;
        pump_ms = 20U;
    } else if (strcmp(phase, "up") == 0 || strcmp(phase, "cancel") == 0) {
        if (g_remote_dragging) {
            g_remote_point.x = (lv_coord_t)x;
            g_remote_point.y = (lv_coord_t)y;
        } else {
            g_remote_point = g_remote_down_point;
        }
        g_remote_active = 1;
        if (g_remote_seen_press) {
            g_remote_pressed = 0;
        } else {
            g_remote_pressed = 1;
            g_remote_release_pending = 1;
        }
        pump_ms = 120U;
    } else {
        log_remote_input_event(phase, x, y, 0);
        return 0;
    }
    pump_lvgl(pump_ms);
    log_remote_input_event(phase, x, y, 1);
    return 1;
}
