#include "v5_lvgl_remote_display_capture.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define V5_UI_MAX_WIDTH 1024U
#define V5_UI_MAX_HEIGHT 600U
#define V5_FB_MODE_PATH "/sys/class/graphics/fb0/modes"
#define V5_FB_STRIDE_PATH "/sys/class/graphics/fb0/stride"
#define V5_FB_BPP_PATH "/sys/class/graphics/fb0/bits_per_pixel"

static int g_fb_fd = -1;
static unsigned char *g_fb;
static unsigned int g_fb_width;
static unsigned int g_fb_height;
static unsigned int g_fb_stride;
static unsigned int g_fb_bpp;
static size_t g_fb_size;
static int g_physical_framebuffer_claimed;

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

int v5_lvgl_remote_display_capture_setup(unsigned int width, unsigned int height)
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

void v5_lvgl_remote_display_capture_write_row(unsigned int x, unsigned int y, const unsigned char *bgra, unsigned int pixels)
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
        if ((((uintptr_t)dst | (uintptr_t)bgra) & 3U) == 0U) {
            uint32_t *dst_words = (uint32_t *)dst;
            const uint32_t *src_words = (const uint32_t *)bgra;
#if defined(__ARM_NEON)
            for (i = 0U; i + 16U <= pixels; i += 16U) {
                uint8x16x4_t input = vld4q_u8(
                    (const uint8_t *)(src_words + i));
                uint8x16x4_t output;
                output.val[0] = input.val[2];
                output.val[1] = input.val[1];
                output.val[2] = input.val[0];
                output.val[3] = vdupq_n_u8(0xffU);
                vst4q_u8((uint8_t *)(dst_words + i), output);
            }
#else
            i = 0U;
#endif
            for (; i < pixels; ++i) {
                uint32_t pixel = src_words[i];
                dst_words[i] = 0xff000000U |
                    ((pixel & 0x00ff0000U) >> 16) |
                    (pixel & 0x0000ff00U) |
                    ((pixel & 0x000000ffU) << 16);
            }
        } else {
            for (i = 0U; i < pixels; ++i) {
                dst[i * 4U + 0U] = bgra[i * 4U + 2U];
                dst[i * 4U + 1U] = bgra[i * 4U + 1U];
                dst[i * 4U + 2U] = bgra[i * 4U + 0U];
                dst[i * 4U + 3U] = 0xffU;
            }
        }
    } else if (g_fb_bpp == 24U) {
#if defined(__ARM_NEON)
        for (i = 0U; i + 16U <= pixels; i += 16U) {
            uint8x16x4_t input = vld4q_u8(bgra + i * 4U);
            uint8x16x3_t output;
            output.val[0] = input.val[2];
            output.val[1] = input.val[1];
            output.val[2] = input.val[0];
            vst3q_u8(dst + i * 3U, output);
        }
#else
        i = 0U;
#endif
        for (; i < pixels; ++i) {
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

int v5_lvgl_remote_display_capture_claim(void)
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

void v5_lvgl_remote_display_capture_write_full(
    const unsigned char *frame,
    unsigned int width,
    unsigned int height)
{
    unsigned int y;
    if (!frame || !g_fb || width != g_fb_width ||
        height != g_fb_height) {
        return;
    }
    for (y = 0U; y < height; ++y) {
        v5_lvgl_remote_display_capture_write_row(
            0U, y, &frame[(size_t)y * width * 4U], width);
    }
}

int v5_lvgl_remote_display_capture_available(void)
{
    return g_fb != 0 && g_fb_fd >= 0;
}

int v5_lvgl_remote_display_capture_physical_ready(void)
{
    return v5_lvgl_remote_display_capture_available() &&
        g_physical_framebuffer_claimed;
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

static void remove_pending_dirty_rect(
    V5RemoteDirtyRect *rects,
    unsigned int *count,
    unsigned int index)
{
    unsigned int i;
    for (i = index + 1U; i < *count; ++i) {
        rects[i - 1U] = rects[i];
    }
    --*count;
}

void v5_lvgl_remote_display_capture_add_dirty_area(
    V5RemoteDirtyRect *rects,
    unsigned int *count,
    unsigned int capacity,
    int x1,
    int y1,
    int x2,
    int y2)
{
    V5RemoteDirtyRect merged = {x1, y1, x2, y2};
    unsigned int i = 0U;
    if (!rects || !count || capacity == 0U || *count > capacity) return;
    while (i < *count) {
        if (dirty_rects_touch(&merged, &rects[i])) {
            dirty_rect_union(&merged, &rects[i]);
            remove_pending_dirty_rect(rects, count, i);
        } else {
            ++i;
        }
    }
    if (*count >= capacity) {
        unsigned int best = 0U;
        unsigned long long best_growth = ~0ULL;
        for (i = 0U; i < *count; ++i) {
            V5RemoteDirtyRect candidate = merged;
            unsigned long long growth;
            dirty_rect_union(&candidate, &rects[i]);
            growth = dirty_rect_area(&candidate) - dirty_rect_area(&merged) -
                     dirty_rect_area(&rects[i]);
            if (growth < best_growth) {
                best = i;
                best_growth = growth;
            }
        }
        dirty_rect_union(&merged, &rects[best]);
        remove_pending_dirty_rect(rects, count, best);
        i = 0U;
        while (i < *count) {
            if (dirty_rects_touch(&merged, &rects[i])) {
                dirty_rect_union(&merged, &rects[i]);
                remove_pending_dirty_rect(rects, count, i);
            } else {
                ++i;
            }
        }
    }
    rects[(*count)++] = merged;
}
