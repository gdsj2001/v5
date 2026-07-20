#include "v5_lvgl_remote_display.h"

#include "lvgl.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define V5_UI_MAX_WIDTH 1024U
#define V5_UI_MAX_HEIGHT 600U
#define V5_FB_MODE_PATH "/sys/class/graphics/fb0/modes"
#define V5_FB_STRIDE_PATH "/sys/class/graphics/fb0/stride"
#define V5_FB_BPP_PATH "/sys/class/graphics/fb0/bits_per_pixel"
#define V5_REMOTE_RUN_DIR "/run/8ax_v5_product_ui"
#define V5_REMOTE_FRAMEBUFFER_PATH V5_REMOTE_RUN_DIR "/remote_framebuffer.bgra"
#define V5_REMOTE_DIRTY_FIFO_PATH V5_REMOTE_RUN_DIR "/remote_dirty"
#define V5_REMOTE_DIRTY_RECT_CAPACITY 16U
#define V5_UI_DRAW_BUFFER_ROWS 160U

typedef struct {
    int x1;
    int y1;
    int x2;
    int y2;
} V5RemoteDirtyRect;

static lv_color_t g_draw_buffer[V5_UI_MAX_WIDTH * V5_UI_DRAW_BUFFER_ROWS];
static unsigned char g_frame[V5_UI_MAX_WIDTH * V5_UI_MAX_HEIGHT * 4U];
static unsigned char g_cached_frame[V5_REMOTE_DISPLAY_CACHE_COUNT][V5_UI_MAX_WIDTH * V5_UI_MAX_HEIGHT * 4U];
static int g_cached_valid[V5_REMOTE_DISPLAY_CACHE_COUNT];
static lv_disp_draw_buf_t g_draw;
static lv_disp_drv_t g_driver;
static unsigned int g_width;
static unsigned int g_height;
static int g_display_ready;
static int g_fb_fd = -1;
static unsigned char *g_fb;
static unsigned int g_fb_width;
static unsigned int g_fb_height;
static unsigned int g_fb_stride;
static unsigned int g_fb_bpp;
static size_t g_fb_size;
static int g_remote_fb_fd = -1;
static unsigned char *g_remote_fb;
static size_t g_remote_fb_size;
static int g_remote_dirty_fd = -1;
static unsigned long long g_frame_id;
static int g_output_suppressed = 1;
static int g_physical_framebuffer_claimed;
static V5RemoteDirtyRect g_pending_dirty_rects[V5_REMOTE_DIRTY_RECT_CAPACITY];
static unsigned int g_pending_dirty_count;

static int read_u32_file(const char *path, unsigned int *out)
{
    FILE *fp = fopen(path, "r");
    unsigned int value;
    if (!fp) {
        return 0;
    }
    if (fscanf(fp, "%u", &value) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    *out = value;
    return 1;
}

static int read_fb_mode(unsigned int *width, unsigned int *height)
{
    FILE *fp = fopen(V5_FB_MODE_PATH, "r");
    char buf[64];
    unsigned int w;
    unsigned int h;
    if (!fp) {
        return 0;
    }
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    if (sscanf(buf, "U:%ux%up", &w, &h) != 2 && sscanf(buf, "%ux%u", &w, &h) != 2) {
        return 0;
    }
    *width = w;
    *height = h;
    return 1;
}

static int init_framebuffer(unsigned int width, unsigned int height)
{
    const char *path = getenv("V5_UI_FRAMEBUFFER");
    size_t minimum_stride;
    if (!path || !path[0]) {
        path = "/dev/fb0";
    }
    if (strcmp(path, "off") == 0) {
        fprintf(stderr, "v5 physical framebuffer disabled; refusing UI ready\n");
        return 0;
    }
    if (!read_fb_mode(&g_fb_width, &g_fb_height) ||
        !read_u32_file(V5_FB_STRIDE_PATH, &g_fb_stride) ||
        !read_u32_file(V5_FB_BPP_PATH, &g_fb_bpp)) {
        fprintf(stderr, "v5 physical framebuffer metadata unavailable\n");
        return 0;
    }
    if (g_fb_width != width || g_fb_height != height ||
        g_fb_width == 0U || g_fb_height == 0U ||
        g_fb_width > V5_UI_MAX_WIDTH || g_fb_height > V5_UI_MAX_HEIGHT) {
        fprintf(stderr,
                "v5 physical framebuffer mode mismatch actual=%ux%u expected=%ux%u\n",
                g_fb_width,
                g_fb_height,
                width,
                height);
        return 0;
    }
    if (g_fb_bpp != 16U && g_fb_bpp != 24U && g_fb_bpp != 32U) {
        fprintf(stderr, "v5 physical framebuffer bpp unsupported actual=%u\n", g_fb_bpp);
        return 0;
    }
    minimum_stride = (size_t)g_fb_width * (size_t)(g_fb_bpp / 8U);
    if ((size_t)g_fb_stride < minimum_stride ||
        (size_t)g_fb_height > SIZE_MAX / (size_t)g_fb_stride) {
        fprintf(stderr,
                "v5 physical framebuffer stride invalid stride=%u minimum=%zu height=%u\n",
                g_fb_stride,
                minimum_stride,
                g_fb_height);
        return 0;
    }
    g_fb_fd = open(path, O_RDWR | O_CLOEXEC);
    if (g_fb_fd < 0) {
        fprintf(stderr,
                "v5 physical framebuffer open failed path=%s errno=%d\n",
                path,
                errno);
        return 0;
    }
    g_fb_size = (size_t)g_fb_stride * g_fb_height;
    g_fb = (unsigned char *)mmap(0, g_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb == MAP_FAILED) {
        close(g_fb_fd);
        g_fb_fd = -1;
        g_fb = 0;
        fprintf(stderr,
                "v5 physical framebuffer mmap failed path=%s size=%zu errno=%d\n",
                path,
                g_fb_size,
                errno);
        return 0;
    }
    return 1;
}

static int ensure_remote_run_dir(void)
{
    if (mkdir(V5_REMOTE_RUN_DIR, 0755) != 0 && errno != EEXIST) {
        return 0;
    }
    return 1;
}

static size_t remote_frame_size(void)
{
    return (size_t)g_width * (size_t)g_height * 4U;
}

static int lock_framebuffer_fd(int fd, int operation)
{
    int rc;
    if (fd < 0) {
        return 0;
    }
    do {
        rc = flock(fd, operation);
    } while (rc != 0 && errno == EINTR);
    return rc == 0;
}

static int ensure_remote_framebuffer(void)
{
    size_t size = remote_frame_size();
    int fd;
    unsigned char *mapped;
    if (size == 0U) {
        return 0;
    }
    if (g_remote_fb && g_remote_fb_size == size) {
        return 1;
    }
    if (!ensure_remote_run_dir()) {
        return 0;
    }
    if (g_remote_fb && g_remote_fb != MAP_FAILED) {
        munmap(g_remote_fb, g_remote_fb_size);
        g_remote_fb = 0;
    }
    if (g_remote_fb_fd >= 0) {
        close(g_remote_fb_fd);
        g_remote_fb_fd = -1;
    }
    fd = open(V5_REMOTE_FRAMEBUFFER_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        return 0;
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        return 0;
    }
    mapped = (unsigned char *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return 0;
    }
    g_remote_fb_fd = fd;
    g_remote_fb = mapped;
    g_remote_fb_size = size;
    if (!g_output_suppressed && lock_framebuffer_fd(g_remote_fb_fd, LOCK_EX)) {
        memcpy(g_remote_fb, g_frame, size);
        (void)lock_framebuffer_fd(g_remote_fb_fd, LOCK_UN);
    }
    return 1;
}

static int ensure_remote_dirty_fifo(void)
{
    int fd;
    if (g_remote_dirty_fd >= 0) {
        return 1;
    }
    if (!ensure_remote_run_dir()) {
        return 0;
    }
    if (mkfifo(V5_REMOTE_DIRTY_FIFO_PATH, 0600) != 0 && errno != EEXIST) {
        return 0;
    }
    fd = open(V5_REMOTE_DIRTY_FIFO_PATH, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return errno == ENXIO ? 1 : 0;
    }
    g_remote_dirty_fd = fd;
    return 1;
}

static void notify_remote_dirty(unsigned long long base_frame_id,
                                unsigned long long frame_id,
                                unsigned int x,
                                unsigned int y,
                                unsigned int w,
                                unsigned int h)
{
    char line[128];
    int len;
    ssize_t written;
    if (w == 0U || h == 0U) {
        return;
    }
    if (!ensure_remote_dirty_fifo() || g_remote_dirty_fd < 0) {
        return;
    }
    len = snprintf(line, sizeof(line), "%llu %llu 1 %u %u %u %u\n",
                   frame_id, base_frame_id, x, y, w, h);
    if (len <= 0 || (size_t)len >= sizeof(line)) {
        return;
    }
    written = write(g_remote_dirty_fd, line, (size_t)len);
    if (written < 0 && (errno == EPIPE || errno == ENXIO)) {
        close(g_remote_dirty_fd);
        g_remote_dirty_fd = -1;
    }
}

static void notify_remote_dirty_rects(unsigned long long base_frame_id,
                                      unsigned long long frame_id,
                                      const V5RemoteDirtyRect *rects,
                                      unsigned int rect_count)
{
    char line[V5_REMOTE_DIRTY_RECT_CAPACITY * 96U];
    size_t offset = 0U;
    unsigned int i;
    ssize_t written;
    if (!rects || rect_count == 0U || rect_count > V5_REMOTE_DIRTY_RECT_CAPACITY) {
        return;
    }
    {
        int len = snprintf(line, sizeof(line), "%llu %llu %u",
                           frame_id, base_frame_id, rect_count);
        if (len <= 0 || (size_t)len >= sizeof(line)) {
            return;
        }
        offset += (size_t)len;
    }
    for (i = 0U; i < rect_count; ++i) {
        int len = snprintf(&line[offset], sizeof(line) - offset,
                           " %d %d %d %d",
                           rects[i].x1, rects[i].y1,
                           rects[i].x2 - rects[i].x1 + 1,
                           rects[i].y2 - rects[i].y1 + 1);
        if (len <= 0 || (size_t)len >= sizeof(line) - offset) {
            return;
        }
        offset += (size_t)len;
    }
    if (offset + 1U >= sizeof(line)) {
        return;
    }
    line[offset++] = '\n';
    if (!ensure_remote_dirty_fifo() || g_remote_dirty_fd < 0) {
        return;
    }
    written = write(g_remote_dirty_fd, line, offset);
    if (written != (ssize_t)offset) {
        close(g_remote_dirty_fd);
        g_remote_dirty_fd = -1;
    }
}

static void copy_row_to_fb(unsigned int x, unsigned int y, const unsigned char *bgra, unsigned int pixels)
{
    unsigned char *dst;
    unsigned int i;
    if (!g_fb || y >= g_fb_height || x >= g_fb_width || pixels == 0U) {
        return;
    }
    if (x + pixels > g_fb_width) {
        pixels = g_fb_width - x;
    }
    dst = &g_fb[(size_t)y * g_fb_stride + (size_t)x * (g_fb_bpp / 8U)];
    if (g_fb_bpp == 32U) {
        for (i = 0; i < pixels; ++i) {
            dst[i * 4U + 0U] = bgra[i * 4U + 2U];
            dst[i * 4U + 1U] = bgra[i * 4U + 1U];
            dst[i * 4U + 2U] = bgra[i * 4U + 0U];
            dst[i * 4U + 3U] = 0xffU;
        }
    } else if (g_fb_bpp == 24U) {
        for (i = 0; i < pixels; ++i) {
            dst[i * 3U + 0U] = bgra[i * 4U + 2U];
            dst[i * 3U + 1U] = bgra[i * 4U + 1U];
            dst[i * 3U + 2U] = bgra[i * 4U + 0U];
        }
    } else {
        for (i = 0; i < pixels; ++i) {
            unsigned char r = bgra[i * 4U + 2U];
            unsigned char g = bgra[i * 4U + 1U];
            unsigned char b = bgra[i * 4U + 0U];
            uint16_t rgb565 = (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
            dst[i * 2U + 0U] = (unsigned char)(rgb565 & 0xffU);
            dst[i * 2U + 1U] = (unsigned char)(rgb565 >> 8);
        }
    }
}

static unsigned long long dirty_rect_area(const V5RemoteDirtyRect *rect)
{
    return (unsigned long long)(rect->x2 - rect->x1 + 1) *
           (unsigned long long)(rect->y2 - rect->y1 + 1);
}

static int dirty_rects_touch(const V5RemoteDirtyRect *left, const V5RemoteDirtyRect *right)
{
    return left->x1 <= right->x2 + 1 && right->x1 <= left->x2 + 1 &&
           left->y1 <= right->y2 + 1 && right->y1 <= left->y2 + 1;
}

static void dirty_rect_union(V5RemoteDirtyRect *target, const V5RemoteDirtyRect *other)
{
    if (other->x1 < target->x1) {
        target->x1 = other->x1;
    }
    if (other->y1 < target->y1) {
        target->y1 = other->y1;
    }
    if (other->x2 > target->x2) {
        target->x2 = other->x2;
    }
    if (other->y2 > target->y2) {
        target->y2 = other->y2;
    }
}

static void remove_pending_dirty_rect(unsigned int index)
{
    unsigned int i;
    for (i = index + 1U; i < g_pending_dirty_count; ++i) {
        g_pending_dirty_rects[i - 1U] = g_pending_dirty_rects[i];
    }
    --g_pending_dirty_count;
}

static void add_pending_dirty_area(int x1, int y1, int x2, int y2)
{
    V5RemoteDirtyRect merged = {x1, y1, x2, y2};
    unsigned int i = 0U;
    while (i < g_pending_dirty_count) {
        if (dirty_rects_touch(&merged, &g_pending_dirty_rects[i])) {
            dirty_rect_union(&merged, &g_pending_dirty_rects[i]);
            remove_pending_dirty_rect(i);
        } else {
            ++i;
        }
    }
    if (g_pending_dirty_count >= V5_REMOTE_DIRTY_RECT_CAPACITY) {
        unsigned int best = 0U;
        unsigned long long best_growth = ~0ULL;
        for (i = 0U; i < g_pending_dirty_count; ++i) {
            V5RemoteDirtyRect candidate = merged;
            unsigned long long growth;
            dirty_rect_union(&candidate, &g_pending_dirty_rects[i]);
            growth = dirty_rect_area(&candidate) - dirty_rect_area(&merged) -
                     dirty_rect_area(&g_pending_dirty_rects[i]);
            if (growth < best_growth) {
                best = i;
                best_growth = growth;
            }
        }
        dirty_rect_union(&merged, &g_pending_dirty_rects[best]);
        remove_pending_dirty_rect(best);
        i = 0U;
        while (i < g_pending_dirty_count) {
            if (dirty_rects_touch(&merged, &g_pending_dirty_rects[i])) {
                dirty_rect_union(&merged, &g_pending_dirty_rects[i]);
                remove_pending_dirty_rect(i);
            } else {
                ++i;
            }
        }
    }
    g_pending_dirty_rects[g_pending_dirty_count++] = merged;
}

static void compose_area(const lv_area_t *area, const lv_color_t *color_p)
{
    int src_w;
    int x1;
    int x2;
    int y1;
    int y2;
    int y;
    if (!area || !color_p || !g_display_ready) {
        return;
    }
    src_w = area->x2 - area->x1 + 1;
    if (src_w <= 0) {
        return;
    }
    x1 = area->x1 < 0 ? 0 : area->x1;
    x2 = area->x2 >= (int)g_width ? (int)g_width - 1 : area->x2;
    y1 = area->y1 < 0 ? 0 : area->y1;
    y2 = area->y2 >= (int)g_height ? (int)g_height - 1 : area->y2;
    if (x1 > x2 || y1 > y2) {
        return;
    }
    for (y = y1; y <= y2; ++y) {
        const lv_color_t *src_row = color_p + (size_t)(y - area->y1) * (size_t)src_w;
        const unsigned char *src_bytes;
        unsigned char *frame_row;
        unsigned int pixels = (unsigned int)(x2 - x1 + 1);
        src_row += x1 - area->x1;
        src_bytes = (const unsigned char *)src_row;
        frame_row = &g_frame[((unsigned int)y * g_width + (unsigned int)x1) * 4U];
        memcpy(frame_row, src_bytes, (size_t)pixels * 4U);
    }
    if (!g_output_suppressed) {
        add_pending_dirty_area(x1, y1, x2, y2);
    }
}

static void commit_composed_frame(void)
{
    unsigned int i;
    unsigned int rect_count;
    unsigned long long base_frame_id;
    if (g_output_suppressed || g_pending_dirty_count == 0U) {
        g_pending_dirty_count = 0U;
        return;
    }
    rect_count = g_pending_dirty_count;
    if (!ensure_remote_framebuffer() || !g_remote_fb || !lock_framebuffer_fd(g_remote_fb_fd, LOCK_EX)) {
        g_pending_dirty_count = 0U;
        return;
    }
    for (i = 0U; i < rect_count; ++i) {
        const V5RemoteDirtyRect *rect = &g_pending_dirty_rects[i];
        unsigned int x = (unsigned int)rect->x1;
        unsigned int width = (unsigned int)(rect->x2 - rect->x1 + 1);
        int y;
        for (y = rect->y1; y <= rect->y2; ++y) {
            const unsigned char *frame_row = &g_frame[((unsigned int)y * g_width + x) * 4U];
            unsigned char *remote_row = &g_remote_fb[((unsigned int)y * g_width + x) * 4U];
            copy_row_to_fb(x, (unsigned int)y, frame_row, width);
            memcpy(remote_row, frame_row, (size_t)width * 4U);
        }
    }
    base_frame_id = g_frame_id;
    ++g_frame_id;
    notify_remote_dirty_rects(base_frame_id, g_frame_id, g_pending_dirty_rects, rect_count);
    (void)lock_framebuffer_fd(g_remote_fb_fd, LOCK_UN);
    g_pending_dirty_count = 0U;
}

static void remote_flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *color_p)
{
    compose_area(area, color_p);
    if (lv_disp_flush_is_last(driver)) {
        commit_composed_frame();
    }
    lv_disp_flush_ready(driver);
}

int v5_lvgl_remote_display_setup(unsigned int width, unsigned int height)
{
    if (g_display_ready) {
        return 1;
    }
    if (width == 0U || height == 0U || width > V5_UI_MAX_WIDTH || height > V5_UI_MAX_HEIGHT) {
        return 0;
    }
    if (!init_framebuffer(width, height)) {
        return 0;
    }
    g_width = width;
    g_height = height;
    memset(g_frame, 0x20, sizeof(g_frame));
    g_frame_id = 1ULL;
    g_output_suppressed = 1;
    if (!ensure_remote_framebuffer()) {
        return 0;
    }
    lv_disp_draw_buf_init(&g_draw, g_draw_buffer, 0, width * V5_UI_DRAW_BUFFER_ROWS);
    lv_disp_drv_init(&g_driver);
    g_driver.hor_res = (lv_coord_t)width;
    g_driver.ver_res = (lv_coord_t)height;
    g_driver.draw_buf = &g_draw;
    g_driver.flush_cb = remote_flush;
    if (!lv_disp_drv_register(&g_driver)) {
        return 0;
    }
    g_display_ready = 1;
    return 1;
}

int v5_lvgl_remote_display_claim_physical_framebuffer(void)
{
    int tty_fd;
    if (!g_fb || g_fb_fd < 0) {
        fprintf(stderr, "v5 physical framebuffer claim failed: framebuffer unavailable\n");
        return 0;
    }
    if (g_physical_framebuffer_claimed) {
        return 1;
    }
    tty_fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (tty_fd < 0) {
        fprintf(stderr, "v5 physical framebuffer claim failed: open /dev/tty0\n");
        return 0;
    }
    if (ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) != 0) {
        fprintf(stderr, "v5 physical framebuffer claim failed: KDSETMODE KD_GRAPHICS\n");
        close(tty_fd);
        return 0;
    }
    close(tty_fd);
    g_physical_framebuffer_claimed = 1;
    fprintf(stderr, "v5 physical framebuffer claimed at formal first-frame boundary\n");
    return 1;
}

void v5_lvgl_remote_display_render_now(void)
{
    lv_timer_handler();
}

int v5_lvgl_remote_display_cache_capture(unsigned int slot)
{
    size_t frame_size;
    if (!g_display_ready || slot >= V5_REMOTE_DISPLAY_CACHE_COUNT) {
        return 0;
    }
    frame_size = remote_frame_size();
    memcpy(g_cached_frame[slot], g_frame, frame_size);
    g_cached_valid[slot] = 1;
    return 1;
}

static void copy_full_frame_to_fb(const unsigned char *frame)
{
    unsigned int y;
    if (!frame || !g_fb) {
        return;
    }
    for (y = 0U; y < g_height; ++y) {
        copy_row_to_fb(0U, y, &frame[(size_t)y * g_width * 4U], g_width);
    }
}

int v5_lvgl_remote_display_cache_blit(unsigned int slot)
{
    size_t frame_size;
    if (!g_display_ready || slot >= V5_REMOTE_DISPLAY_CACHE_COUNT || !g_cached_valid[slot]) {
        return 0;
    }
    frame_size = remote_frame_size();
    memcpy(g_frame, g_cached_frame[slot], frame_size);
    if (g_output_suppressed) {
        return 0;
    }
    return v5_lvgl_remote_display_publish_current_frame();
}

int v5_lvgl_remote_display_publish_current_frame(void)
{
    size_t frame_size;
    unsigned long long base_frame_id;
    if (!g_display_ready || g_output_suppressed ||
        !g_physical_framebuffer_claimed || !g_fb || g_fb_fd < 0) {
        return 0;
    }
    frame_size = remote_frame_size();
    if (!ensure_remote_framebuffer() || !g_remote_fb || !lock_framebuffer_fd(g_remote_fb_fd, LOCK_EX)) {
        return 0;
    }
    copy_full_frame_to_fb(g_frame);
    memcpy(g_remote_fb, g_frame, frame_size);
    g_pending_dirty_count = 0U;
    base_frame_id = g_frame_id;
    ++g_frame_id;
    notify_remote_dirty(base_frame_id, g_frame_id, 0U, 0U, g_width, g_height);
    (void)lock_framebuffer_fd(g_remote_fb_fd, LOCK_UN);
    return 1;
}

int v5_lvgl_remote_display_blackout_for_restart(void)
{
    size_t frame_size;
    unsigned long long base_frame_id;
    int physical_black;
    int remote_black = 0;

    g_output_suppressed = 1;
    g_pending_dirty_count = 0U;
    if (!g_display_ready) {
        return 0;
    }
    frame_size = remote_frame_size();
    memset(g_frame, 0, frame_size);
    copy_full_frame_to_fb(g_frame);
    physical_black = g_fb != 0;

    if (ensure_remote_framebuffer() && g_remote_fb &&
        lock_framebuffer_fd(g_remote_fb_fd, LOCK_EX)) {
        memcpy(g_remote_fb, g_frame, frame_size);
        base_frame_id = g_frame_id;
        ++g_frame_id;
        (void)lock_framebuffer_fd(g_remote_fb_fd, LOCK_UN);
        notify_remote_dirty(base_frame_id, g_frame_id, 0U, 0U, g_width, g_height);
        remote_black = 1;
    }
    return physical_black || remote_black;
}

int v5_lvgl_remote_display_cache_valid(unsigned int slot)
{
    return slot < V5_REMOTE_DISPLAY_CACHE_COUNT && g_cached_valid[slot];
}

void v5_lvgl_remote_display_cache_invalidate(unsigned int slot)
{
    if (slot < V5_REMOTE_DISPLAY_CACHE_COUNT) {
        g_cached_valid[slot] = 0;
    }
}

size_t v5_lvgl_remote_display_cache_budget_bytes(void)
{
    return remote_frame_size() * V5_REMOTE_DISPLAY_CACHE_COUNT;
}

int v5_lvgl_remote_display_set_output_suppressed(int suppressed)
{
    int previous = g_output_suppressed;
    g_output_suppressed = suppressed ? 1 : 0;
    if (g_output_suppressed) {
        g_pending_dirty_count = 0U;
    }
    return previous;
}

int v5_lvgl_remote_display_output_suppressed(void)
{
    return g_output_suppressed;
}

int v5_remote_frame_snapshot(V5RemoteFrameSnapshot *snapshot)
{
    if (!snapshot || !g_display_ready) {
        return 0;
    }
    snapshot->width = g_width;
    snapshot->height = g_height;
    snapshot->stride = g_width * 4U;
    snapshot->pixels = g_frame;
    return 1;
}

int v5_remote_frame_ipc_pump(void)
{
    if (!g_display_ready) {
        return 0;
    }
    return ensure_remote_framebuffer() && ensure_remote_dirty_fifo();
}
