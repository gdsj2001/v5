#include "v5_lvgl_remote_display.h"

#include "v5_lvgl_remote_display_capture.h"
#include "v5_lvgl_remote_display_delta.h"

#include "lvgl.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define V5_UI_MAX_WIDTH 1024U
#define V5_UI_MAX_HEIGHT 600U
#define V5_REMOTE_RUN_DIR "/run/8ax_v5_product_ui"
#define V5_REMOTE_FRAMEBUFFER_PATH V5_REMOTE_RUN_DIR "/remote_framebuffer.bgra"
#define V5_REMOTE_DIRTY_FIFO_PATH V5_REMOTE_RUN_DIR "/remote_dirty"
#define V5_LOCAL_DIRTY_RECT_CAPACITY 16U
#define V5_REMOTE_DIRTY_RECT_CAPACITY 64U
#define V5_REMOTE_FRAME_INTERVAL_NS 100000000ULL
#define V5_UI_DRAW_BUFFER_ROWS 160U

static lv_color_t g_draw_buffer[V5_UI_MAX_WIDTH * V5_UI_DRAW_BUFFER_ROWS];
static unsigned char g_frame[V5_UI_MAX_WIDTH * V5_UI_MAX_HEIGHT * 4U];
static unsigned char g_cached_frame[V5_REMOTE_DISPLAY_CACHE_COUNT][V5_UI_MAX_WIDTH * V5_UI_MAX_HEIGHT * 4U];
static int g_cached_valid[V5_REMOTE_DISPLAY_CACHE_COUNT];
static lv_disp_draw_buf_t g_draw;
static lv_disp_drv_t g_driver;
static unsigned int g_width;
static unsigned int g_height;
static int g_display_ready;
static int g_remote_fb_fd = -1;
static unsigned char *g_remote_fb;
static size_t g_remote_fb_size;
static int g_remote_dirty_fd = -1;
static unsigned long long g_frame_id;
static int g_output_suppressed = 1;
static V5RemoteDirtyRect g_local_dirty_rects[V5_LOCAL_DIRTY_RECT_CAPACITY];
static unsigned int g_local_dirty_count;
static V5RemoteDirtyRect g_remote_dirty_rects[V5_REMOTE_DIRTY_RECT_CAPACITY];
static unsigned int g_remote_dirty_count;
static unsigned long long g_remote_next_publish_ns;
static unsigned long long g_remote_cpu_sample_generation;

static unsigned long long monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0ULL;
    }
    return (unsigned long long)now.tv_sec * 1000000000ULL +
        (unsigned long long)now.tv_nsec;
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
void v5_lvgl_remote_display_publish_cpu_metrics(double cpu0_percent, double cpu1_percent,
    unsigned long long sample_generation,
    unsigned long long sample_monotonic_ns)
{
    char line[128];
    int len;
    ssize_t written;
    if (!isfinite(cpu0_percent) || !isfinite(cpu1_percent) ||
        cpu0_percent < 0.0 || cpu0_percent > 100.0 ||
        cpu1_percent < 0.0 || cpu1_percent > 100.0 ||
        sample_generation == 0ULL || sample_monotonic_ns == 0ULL ||
        sample_generation == g_remote_cpu_sample_generation) {
        return;
    }
    if (!ensure_remote_dirty_fifo() || g_remote_dirty_fd < 0) {
        return;
    }
    len = snprintf(line, sizeof(line), "M %llu %llu %.3f %.3f\n",
                   sample_generation, sample_monotonic_ns,
                   cpu0_percent, cpu1_percent);
    if (len <= 0 || (size_t)len >= sizeof(line)) {
        return;
    }
    written = write(g_remote_dirty_fd, line, (size_t)len);
    if (written == (ssize_t)len) {
        g_remote_cpu_sample_generation = sample_generation;
    } else {
        close(g_remote_dirty_fd);
        g_remote_dirty_fd = -1;
    }
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
        v5_lvgl_remote_display_capture_add_dirty_area(
            g_local_dirty_rects,
            &g_local_dirty_count,
            V5_LOCAL_DIRTY_RECT_CAPACITY,
            x1, y1, x2, y2);
        v5_lvgl_remote_display_capture_add_dirty_area(
            g_remote_dirty_rects,
            &g_remote_dirty_count,
            V5_REMOTE_DIRTY_RECT_CAPACITY,
            x1, y1, x2, y2);
    }
}

static void commit_composed_frame(void)
{
    unsigned int i;
    unsigned int remote_rect_count;
    unsigned int changed_count = 0U;
    unsigned long long base_frame_id;
    unsigned long long now_ns;
    V5RemoteDirtyRect changed_rects[V5_REMOTE_DIRTY_RECT_CAPACITY];
    if (g_output_suppressed) {
        g_local_dirty_count = 0U;
        g_remote_dirty_count = 0U;
        return;
    }
    for (i = 0U; i < g_local_dirty_count; ++i) {
        const V5RemoteDirtyRect *rect = &g_local_dirty_rects[i];
        unsigned int x = (unsigned int)rect->x1;
        unsigned int width = (unsigned int)(rect->x2 - rect->x1 + 1);
        int y;
        for (y = rect->y1; y <= rect->y2; ++y) {
            const unsigned char *frame_row = &g_frame[((unsigned int)y * g_width + x) * 4U];
            v5_lvgl_remote_display_capture_write_row(
                x, (unsigned int)y, frame_row, width);
        }
    }
    g_local_dirty_count = 0U;
    if (g_remote_dirty_count == 0U) {
        return;
    }
    now_ns = monotonic_ns();
    if (g_remote_next_publish_ns != 0ULL && now_ns != 0ULL &&
        now_ns < g_remote_next_publish_ns) {
        return;
    }
    remote_rect_count = g_remote_dirty_count;
    if (!ensure_remote_framebuffer() || !g_remote_fb ||
        !lock_framebuffer_fd(g_remote_fb_fd, LOCK_EX)) {
        return;
    }
    if (!v5_lvgl_remote_display_delta_commit(
            g_frame, g_remote_fb, g_width, g_height,
            g_remote_dirty_rects, remote_rect_count,
            changed_rects, V5_REMOTE_DIRTY_RECT_CAPACITY,
            &changed_count)) {
        (void)lock_framebuffer_fd(g_remote_fb_fd, LOCK_UN);
        return;
    }
    g_remote_dirty_count = 0U;
    g_remote_next_publish_ns = now_ns == 0ULL ? 0ULL :
        now_ns + V5_REMOTE_FRAME_INTERVAL_NS;
    if (changed_count == 0U) {
        (void)lock_framebuffer_fd(g_remote_fb_fd, LOCK_UN);
        return;
    }
    base_frame_id = g_frame_id;
    ++g_frame_id;
    (void)lock_framebuffer_fd(g_remote_fb_fd, LOCK_UN);
    notify_remote_dirty_rects(
        base_frame_id, g_frame_id, changed_rects, changed_count);
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
    if (!v5_lvgl_remote_display_capture_setup(width, height)) {
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
    return v5_lvgl_remote_display_capture_claim();
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
        !v5_lvgl_remote_display_capture_physical_ready()) {
        return 0;
    }
    frame_size = remote_frame_size();
    if (!ensure_remote_framebuffer() || !g_remote_fb || !lock_framebuffer_fd(g_remote_fb_fd, LOCK_EX)) {
        return 0;
    }
    v5_lvgl_remote_display_capture_write_full(
        g_frame, g_width, g_height);
    memcpy(g_remote_fb, g_frame, frame_size);
    g_local_dirty_count = 0U;
    g_remote_dirty_count = 0U;
    {
        unsigned long long now_ns = monotonic_ns();
        g_remote_next_publish_ns = now_ns == 0ULL ? 0ULL :
            now_ns + V5_REMOTE_FRAME_INTERVAL_NS;
    }
    base_frame_id = g_frame_id;
    ++g_frame_id;
    (void)lock_framebuffer_fd(g_remote_fb_fd, LOCK_UN);
    notify_remote_dirty(base_frame_id, g_frame_id, 0U, 0U, g_width, g_height);
    return 1;
}

int v5_lvgl_remote_display_blackout_for_restart(void)
{
    size_t frame_size;
    unsigned long long base_frame_id;
    int physical_black;
    int remote_black = 0;

    g_output_suppressed = 1;
    g_local_dirty_count = 0U;
    g_remote_dirty_count = 0U;
    g_remote_next_publish_ns = 0ULL;
    if (!g_display_ready) {
        return 0;
    }
    frame_size = remote_frame_size();
    memset(g_frame, 0, frame_size);
    v5_lvgl_remote_display_capture_write_full(
        g_frame, g_width, g_height);
    physical_black = v5_lvgl_remote_display_capture_available();

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
        g_local_dirty_count = 0U;
        g_remote_dirty_count = 0U;
        g_remote_next_publish_ns = 0ULL;
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
