#include "v5_lvgl_touch_input.h"

#include "lvgl.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define V5_TOUCH_DEVICE "/dev/input/by-path/z20-touchscreen"
#define V5_TOUCH_CALIBRATION "/opt/8ax/safe_ui/re_touch_calibration.json"
#define V5_TOUCH_OLD_CALIBRATION "/opt/8ax/ui/re_touch_calibration.json"
#define V5_TOUCH_WIDTH 1024
#define V5_TOUCH_HEIGHT 600

#define V5_TOUCH_ABS_MT_POSITION_X 53
#define V5_TOUCH_ABS_MT_POSITION_Y 54
#define V5_TOUCH_ABS_MT_TRACKING_ID 57

typedef struct V5TouchCal {
    double sx;
    double sxy;
    double syx;
    double sy;
    double ox;
    double oy;
} V5TouchCal;

static lv_indev_drv_t g_touch_driver;
static lv_indev_t *g_touch_indev;
static V5TouchCal g_cal;
static int g_fd = -1;
static int g_raw_x;
static int g_raw_y;
static int g_screen_x;
static int g_screen_y;
static int g_touching;
static int g_pending;

#define V5_TOUCH_MAX_SLOTS 10

typedef struct V5TouchSlot {
    int active;
    int have_raw_x;
    int have_raw_y;
    int raw_x;
    int raw_y;
    int screen_x;
    int screen_y;
} V5TouchSlot;

static V5TouchSlot g_slots[V5_TOUCH_MAX_SLOTS];
static int g_mt_slot;
static int g_lvgl_suppressed;
static V5LvglTouchPointsCallback g_points_callback;
static void *g_points_callback_user_data;

static int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long size;
    char *buf;
    if (!fp) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if (size <= 0 || size > 8192) {
        fclose(fp);
        return 0;
    }
    rewind(fp);
    buf = (char *)calloc((size_t)size + 1U, 1U);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    if (fread(buf, 1U, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return buf;
}

static int parse_double_key(const char *json, const char *key, double *out)
{
    char pattern[64];
    const char *p;
    char *end = 0;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return 0;
    }
    p = strchr(p, ':');
    if (!p) {
        return 0;
    }
    errno = 0;
    *out = strtod(p + 1, &end);
    return errno == 0 && end && end != p + 1;
}

static int load_calibration(V5TouchCal *cal)
{
    char *json;
    int ok;
    if (file_exists(V5_TOUCH_OLD_CALIBRATION)) {
        fprintf(stderr, "v5 touch input refused retired calibration path: %s\n", V5_TOUCH_OLD_CALIBRATION);
        return 0;
    }
    json = read_file(V5_TOUCH_CALIBRATION);
    if (!json) {
        fprintf(stderr, "v5 touch input missing calibration: %s\n", V5_TOUCH_CALIBRATION);
        return 0;
    }
    ok = strstr(json, "raw-evdev-cal-v2") != 0 &&
        parse_double_key(json, "sx", &cal->sx) &&
        parse_double_key(json, "sxy", &cal->sxy) &&
        parse_double_key(json, "syx", &cal->syx) &&
        parse_double_key(json, "sy", &cal->sy) &&
        parse_double_key(json, "ox", &cal->ox) &&
        parse_double_key(json, "oy", &cal->oy);
    free(json);
    if (!ok) {
        fprintf(stderr, "v5 touch input invalid calibration: %s\n", V5_TOUCH_CALIBRATION);
    }
    return ok;
}

static int clamp_int(double value, int min_value, int max_value)
{
    int out = (int)(value + (value >= 0.0 ? 0.5 : -0.5));
    if (out < min_value) {
        return min_value;
    }
    if (out > max_value) {
        return max_value;
    }
    return out;
}

static void map_raw_point(int raw_x, int raw_y, int *screen_x, int *screen_y)
{
    double x = ((double)raw_x * g_cal.sx) + ((double)raw_y * g_cal.sxy) + g_cal.ox;
    double y = ((double)raw_x * g_cal.syx) + ((double)raw_y * g_cal.sy) + g_cal.oy;
    if (screen_x) {
        *screen_x = clamp_int(x, 0, V5_TOUCH_WIDTH - 1);
    }
    if (screen_y) {
        *screen_y = clamp_int(y, 0, V5_TOUCH_HEIGHT - 1);
    }
}

static void update_screen_point(void)
{
    map_raw_point(g_raw_x, g_raw_y, &g_screen_x, &g_screen_y);
}

static void update_slot_screen(int slot)
{
    if (slot < 0 || slot >= V5_TOUCH_MAX_SLOTS) {
        return;
    }
    if (!g_slots[slot].active || !g_slots[slot].have_raw_x || !g_slots[slot].have_raw_y) {
        return;
    }
    map_raw_point(g_slots[slot].raw_x, g_slots[slot].raw_y, &g_slots[slot].screen_x, &g_slots[slot].screen_y);
}

static int collect_touch_points(lv_point_t *points, int cap)
{
    int count = 0;
    int i;
    if (!points || cap <= 0) {
        return 0;
    }
    if (g_touching && g_pending && !g_slots[0].active && g_slots[0].have_raw_x && g_slots[0].have_raw_y) {
        g_slots[0].active = 1;
    }
    for (i = 0; i < V5_TOUCH_MAX_SLOTS && count < cap; ++i) {
        if (!g_slots[i].active || !g_slots[i].have_raw_x || !g_slots[i].have_raw_y) {
            continue;
        }
        update_slot_screen(i);
        points[count].x = (lv_coord_t)g_slots[i].screen_x;
        points[count].y = (lv_coord_t)g_slots[i].screen_y;
        ++count;
    }
    return count;
}

static void publish_touch_points(void)
{
    lv_point_t points[2];
    int changed = 0;
    int consumed = 0;
    int count = collect_touch_points(points, 2);
    if (count >= 2) {
        g_screen_x = ((int)points[0].x + (int)points[1].x) / 2;
        g_screen_y = ((int)points[0].y + (int)points[1].y) / 2;
        g_touching = 1;
    } else if (count == 1) {
        g_screen_x = (int)points[0].x;
        g_screen_y = (int)points[0].y;
        g_touching = 1;
    } else {
        g_touching = 0;
        update_screen_point();
    }
    if (g_points_callback) {
        consumed = g_points_callback(count > 0 ? points : 0, count, count > 0 ? 1 : 0, &changed, g_points_callback_user_data);
    }
    g_lvgl_suppressed = consumed && count > 0;
}

static void clear_touch_slots(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_mt_slot = 0;
}

static void handle_event(const struct input_event *ev)
{
    if (ev->type == EV_ABS) {
        if (ev->code == ABS_MT_SLOT) {
            if (ev->value >= 0 && ev->value < V5_TOUCH_MAX_SLOTS) {
                g_mt_slot = ev->value;
            }
        } else if (ev->code == ABS_X || ev->code == V5_TOUCH_ABS_MT_POSITION_X) {
            g_raw_x = ev->value;
            g_pending = 1;
            if (ev->code == V5_TOUCH_ABS_MT_POSITION_X) {
                if (g_mt_slot >= 0 && g_mt_slot < V5_TOUCH_MAX_SLOTS) {
                    g_slots[g_mt_slot].raw_x = ev->value;
                    g_slots[g_mt_slot].have_raw_x = 1;
                    g_slots[g_mt_slot].active = 1;
                }
            } else {
                g_slots[0].raw_x = ev->value;
                g_slots[0].have_raw_x = 1;
            }
        } else if (ev->code == ABS_Y || ev->code == V5_TOUCH_ABS_MT_POSITION_Y) {
            g_raw_y = ev->value;
            g_pending = 1;
            if (ev->code == V5_TOUCH_ABS_MT_POSITION_Y) {
                if (g_mt_slot >= 0 && g_mt_slot < V5_TOUCH_MAX_SLOTS) {
                    g_slots[g_mt_slot].raw_y = ev->value;
                    g_slots[g_mt_slot].have_raw_y = 1;
                    g_slots[g_mt_slot].active = 1;
                }
            } else {
                g_slots[0].raw_y = ev->value;
                g_slots[0].have_raw_y = 1;
            }
        } else if (ev->code == V5_TOUCH_ABS_MT_TRACKING_ID) {
            g_pending = 1;
            if (g_mt_slot >= 0 && g_mt_slot < V5_TOUCH_MAX_SLOTS) {
                if (ev->value < 0) {
                    memset(&g_slots[g_mt_slot], 0, sizeof(g_slots[g_mt_slot]));
                } else {
                    g_slots[g_mt_slot].active = 1;
                }
            }
        }
    } else if (ev->type == EV_KEY && ev->code == BTN_TOUCH) {
        g_touching = ev->value ? 1 : 0;
        if (!g_touching) {
            clear_touch_slots();
        }
        g_pending = 1;
    } else if (ev->type == EV_SYN && ev->code == SYN_REPORT && g_pending) {
        publish_touch_points();
        g_pending = 0;
    }
}

static void drain_events(void)
{
    for (;;) {
        struct input_event ev;
        ssize_t n = read(g_fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            handle_event(&ev);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }
        return;
    }
}

static void touch_read_cb(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    if (g_fd >= 0) {
        drain_events();
    }
    data->point.x = (lv_coord_t)g_screen_x;
    data->point.y = (lv_coord_t)g_screen_y;
    data->state = (g_touching && !g_lvgl_suppressed) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

int v5_lvgl_touch_input_setup(void)
{
    const char *device = getenv("V5_TOUCH_DEVICE");
    if (g_touch_indev) {
        return 1;
    }
    if (!device || !device[0]) {
        device = V5_TOUCH_DEVICE;
    }
    if (!load_calibration(&g_cal)) {
        return 0;
    }
    g_fd = open(device, O_RDONLY | O_NONBLOCK);
    if (g_fd < 0) {
        fprintf(stderr, "v5 touch input open failed: %s\n", device);
        return 0;
    }
    clear_touch_slots();
    update_screen_point();
    lv_indev_drv_init(&g_touch_driver);
    g_touch_driver.type = LV_INDEV_TYPE_POINTER;
    g_touch_driver.read_cb = touch_read_cb;
    g_touch_indev = lv_indev_drv_register(&g_touch_driver);
    if (!g_touch_indev) {
        close(g_fd);
        g_fd = -1;
        return 0;
    }
    fprintf(stderr, "v5 touch input enabled device=%s calibration=%s\n", device, V5_TOUCH_CALIBRATION);
    return 1;
}

void v5_lvgl_touch_input_set_points_callback(V5LvglTouchPointsCallback callback, void *user_data)
{
    g_points_callback = callback;
    g_points_callback_user_data = user_data;
}
