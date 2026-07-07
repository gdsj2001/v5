#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define V5_DEFAULT_DEVICE "/dev/input/by-path/z20-touchscreen"
#define V5_DEFAULT_CALIBRATION "/opt/8ax/safe_ui/re_touch_calibration.json"
#define V5_DEFAULT_OLD_CALIBRATION "/opt/8ax/ui/re_touch_calibration.json"
#define V5_DEFAULT_ENABLE "/run/8ax_v5_product_ui/enable_touch_diagnostics"
#define V5_DEFAULT_OUT "/run/8ax_v5_product_ui/touch_events.jsonl"

static volatile sig_atomic_t g_stop_requested;

typedef struct V5TouchCalibration {
    double sx;
    double sxy;
    double syx;
    double sy;
    double ox;
    double oy;
    int valid;
} V5TouchCalibration;

static void on_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static int ensure_parent_dir(const char *path)
{
    char buf[256];
    char *slash;
    size_t len;
    if (!path) {
        return 0;
    }
    len = strlen(path);
    if (len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, path, len + 1U);
    slash = strrchr(buf, '/');
    if (!slash || slash == buf) {
        return 1;
    }
    *slash = '\0';
    if (mkdir(buf, 0755) == 0 || errno == EEXIST) {
        return 1;
    }
    return 0;
}

static int file_exists(const char *path)
{
    return path && access(path, F_OK) == 0;
}

static int read_file(const char *path, char *buf, size_t size)
{
    FILE *fp;
    size_t n;
    if (!path || !buf || size == 0U) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    n = fread(buf, 1U, size - 1U, fp);
    fclose(fp);
    buf[n] = '\0';
    return n > 0U;
}

static int parse_json_number(const char *json, const char *key, double *out)
{
    const char *p;
    char pattern[32];
    if (!json || !key || !out || strlen(key) + 4U >= sizeof(pattern)) {
        return 0;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return 0;
    }
    p = strchr(p + strlen(pattern), ':');
    if (!p) {
        return 0;
    }
    errno = 0;
    *out = strtod(p + 1, 0);
    return errno == 0;
}

static int load_calibration(const char *path, V5TouchCalibration *cal)
{
    char json[4096];
    if (!cal) {
        return 0;
    }
    memset(cal, 0, sizeof(*cal));
    if (!read_file(path, json, sizeof(json))) {
        return 0;
    }
    if (!strstr(json, "raw-evdev-cal-v2")) {
        return 0;
    }
    if (!parse_json_number(json, "sx", &cal->sx) || !parse_json_number(json, "sxy", &cal->sxy) ||
        !parse_json_number(json, "syx", &cal->syx) || !parse_json_number(json, "sy", &cal->sy) ||
        !parse_json_number(json, "ox", &cal->ox) || !parse_json_number(json, "oy", &cal->oy)) {
        return 0;
    }
    cal->valid = 1;
    return 1;
}

static double monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void write_touch_event(FILE *out, const char *device, const char *calibration_path,
                              const V5TouchCalibration *cal, int raw_x, int raw_y, int touching)
{
    double x = cal->sx * (double)raw_x + cal->sxy * (double)raw_y + cal->ox;
    double y = cal->syx * (double)raw_x + cal->sy * (double)raw_y + cal->oy;
    fprintf(out,
            "{\"schema\":\"v5.touch_event.v1\",\"source\":\"v5_touch_diagnostics\","
            "\"time_monotonic_s\":%.6f,\"device\":\"%s\",\"calibration_path\":\"%s\","
            "\"calibration_enabled\":true,\"raw_x\":%d,\"raw_y\":%d,\"x\":%.2f,\"y\":%.2f,\"touching\":%s}\n",
            monotonic_seconds(), device, calibration_path, raw_x, raw_y, x, y, touching ? "true" : "false");
    fflush(out);
}

int main(int argc, char **argv)
{
    const char *device = V5_DEFAULT_DEVICE;
    const char *calibration_path = V5_DEFAULT_CALIBRATION;
    const char *old_calibration_path = V5_DEFAULT_OLD_CALIBRATION;
    const char *enable_path = V5_DEFAULT_ENABLE;
    const char *out_path = V5_DEFAULT_OUT;
    V5TouchCalibration cal;
    int fd;
    FILE *out;
    int raw_x = -1;
    int raw_y = -1;
    int touching = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device = argv[++i];
        } else if (strcmp(argv[i], "--calibration") == 0 && i + 1 < argc) {
            calibration_path = argv[++i];
        } else if (strcmp(argv[i], "--enable") == 0 && i + 1 < argc) {
            enable_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: v5_touch_diagnostics [--device PATH] [--calibration PATH] [--enable PATH] [--out PATH]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (file_exists(old_calibration_path)) {
        fprintf(stderr, "retired touch calibration path exists: %s\n", old_calibration_path);
        return 3;
    }
    if (!load_calibration(calibration_path, &cal)) {
        fprintf(stderr, "invalid touch calibration: %s\n", calibration_path);
        return 4;
    }
    if (!ensure_parent_dir(out_path)) {
        fprintf(stderr, "cannot create output parent for %s\n", out_path);
        return 5;
    }
    fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror(device);
        return 6;
    }
    out = fopen(out_path, "a");
    if (!out) {
        perror(out_path);
        close(fd);
        return 7;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    printf("v5 touch diagnostics running device=%s out=%s enable=%s calibration=%s\n", device, out_path, enable_path, calibration_path);
    fflush(stdout);

    while (!g_stop_requested) {
        struct input_event ev;
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read input event");
            break;
        }
        if ((size_t)n != sizeof(ev)) {
            continue;
        }
        if (ev.type == EV_ABS && ev.code == ABS_X) {
            raw_x = ev.value;
        } else if (ev.type == EV_ABS && ev.code == ABS_Y) {
            raw_y = ev.value;
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            touching = ev.value ? 1 : 0;
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT && raw_x >= 0 && raw_y >= 0 && file_exists(enable_path)) {
            write_touch_event(out, device, calibration_path, &cal, raw_x, raw_y, touching);
        }
    }

    fclose(out);
    close(fd);
    return 0;
}
