#include "v5_lvgl_remote_input.h"
#include "v5_ui_first_frame_guard.h"

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
static V5UiOperatorInputSequence g_remote_post_modal_sequence;

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

static void pump_lvgl(unsigned int reads)
{
    unsigned int i;
    if (!g_remote_driver.read_timer) {
        return;
    }
    for (i = 0U; i < reads; ++i) {
        lv_timer_ready(g_remote_driver.read_timer);
        lv_timer_handler();
    }
}

static void reset_remote_pointer_state(void)
{
    g_remote_active = 0;
    g_remote_pressed = 0;
    g_remote_seen_press = 0;
    g_remote_release_pending = 0;
    g_remote_dragging = 0;
}

static int ignore_post_modal_remote_event(const char *phase, int x, int y)
{
    int is_down = strcmp(phase, "down") == 0;
    int is_up = strcmp(phase, "up") == 0 || strcmp(phase, "cancel") == 0;
    int allowed;
    if (is_down) {
        allowed = v5_ui_first_frame_guard_input_sequence_begin(
            &g_remote_post_modal_sequence,
            (lv_coord_t)x,
            (lv_coord_t)y);
    } else if (is_up) {
        allowed = v5_ui_first_frame_guard_input_sequence_end(&g_remote_post_modal_sequence);
    } else {
        allowed = v5_ui_first_frame_guard_input_sequence_continue(&g_remote_post_modal_sequence);
    }
    if (allowed) {
        return 0;
    }
    if (g_remote_indev) {
        lv_indev_reset(g_remote_indev, NULL);
    }
    reset_remote_pointer_state();
    return 1;
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
    v5_ui_first_frame_guard_input_sequence_reset(&g_remote_post_modal_sequence);
    lv_indev_drv_init(&g_remote_driver);
    g_remote_driver.type = LV_INDEV_TYPE_POINTER;
    g_remote_driver.read_cb = remote_input_read_cb;
    g_remote_indev = lv_indev_drv_register(&g_remote_driver);
    return g_remote_indev ? 1 : 0;
}

int v5_lvgl_remote_input_pointer_event(const char *phase, int x, int y)
{
    unsigned int pump_reads = 1U;
    int is_down;
    int is_move;
    int is_up;
    int dx;
    int dy;
    if (!v5_lvgl_remote_input_accepts_pointer() || !g_remote_indev || !phase || x < 0 || y < 0) {
        log_remote_input_event(phase, x, y, 0);
        return 0;
    }
    is_down = strcmp(phase, "down") == 0;
    is_move = strcmp(phase, "move") == 0;
    is_up = strcmp(phase, "up") == 0 || strcmp(phase, "cancel") == 0;
    if (!is_down && !is_move && !is_up) {
        log_remote_input_event(phase, x, y, 0);
        return 0;
    }
    if (ignore_post_modal_remote_event(phase, x, y)) {
        log_remote_input_event(phase, x, y, 0);
        return 1;
    }
    if (is_down) {
        g_remote_point.x = (lv_coord_t)x;
        g_remote_point.y = (lv_coord_t)y;
        g_remote_down_point = g_remote_point;
        g_remote_active = 1;
        g_remote_pressed = 1;
        g_remote_seen_press = 0;
        g_remote_release_pending = 0;
        g_remote_dragging = 0;
    } else if (is_move) {
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
    } else if (is_up) {
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
        pump_reads = 2U;
    }
    pump_lvgl(pump_reads);
    log_remote_input_event(phase, x, y, 1);
    return 1;
}
