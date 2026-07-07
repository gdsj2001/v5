#include "v5_lvgl_remote_display.h"

#include "v5_lvgl_remote_input.h"
#include "lvgl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

#define V5_UI_MAX_WIDTH 1024U
#define V5_UI_MAX_HEIGHT 600U
#define V5_FB_MODE_PATH "/sys/class/graphics/fb0/modes"
#define V5_FB_STRIDE_PATH "/sys/class/graphics/fb0/stride"
#define V5_FB_BPP_PATH "/sys/class/graphics/fb0/bits_per_pixel"

static lv_color_t g_draw_buffer[V5_UI_MAX_WIDTH * 20U];
static unsigned char g_frame[V5_UI_MAX_WIDTH * V5_UI_MAX_HEIGHT * 4U];
static lv_disp_draw_buf_t g_draw;
static lv_disp_drv_t g_driver;
static unsigned int g_width;
static unsigned int g_height;
static int g_display_ready;
static int g_listen_fd = -1;
static int g_fb_fd = -1;
static unsigned char *g_fb;
static unsigned int g_fb_width;
static unsigned int g_fb_height;
static unsigned int g_fb_stride;
static unsigned int g_fb_bpp;
static size_t g_fb_size;

static int send_all(int fd, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    while (size > 0U) {
        ssize_t n = send(fd, p, size, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (n == 0) {
            return 0;
        }
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

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

static void init_framebuffer(unsigned int *width, unsigned int *height)
{
    const char *path = getenv("V5_UI_FRAMEBUFFER");
    if (!path || !path[0]) {
        path = "/dev/fb0";
    }
    if (strcmp(path, "off") == 0) {
        return;
    }
    if (!read_fb_mode(&g_fb_width, &g_fb_height) ||
        !read_u32_file(V5_FB_STRIDE_PATH, &g_fb_stride) ||
        !read_u32_file(V5_FB_BPP_PATH, &g_fb_bpp)) {
        return;
    }
    if (g_fb_width == 0U || g_fb_height == 0U || g_fb_width > V5_UI_MAX_WIDTH || g_fb_height > V5_UI_MAX_HEIGHT) {
        return;
    }
    if (g_fb_bpp != 16U && g_fb_bpp != 24U && g_fb_bpp != 32U) {
        return;
    }
    g_fb_fd = open(path, O_RDWR);
    if (g_fb_fd < 0) {
        return;
    }
    g_fb_size = (size_t)g_fb_stride * g_fb_height;
    g_fb = (unsigned char *)mmap(0, g_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb == MAP_FAILED) {
        close(g_fb_fd);
        g_fb_fd = -1;
        g_fb = 0;
        return;
    }
    *width = g_fb_width;
    *height = g_fb_height;
}

static void copy_pixel_to_fb(unsigned int x, unsigned int y, lv_color_t c)
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char *dst;
    uint16_t rgb565;
    if (!g_fb || x >= g_fb_width || y >= g_fb_height) {
        return;
    }
    r = LV_COLOR_GET_R(c);
    g = LV_COLOR_GET_G(c);
    b = LV_COLOR_GET_B(c);
    dst = &g_fb[(size_t)y * g_fb_stride + (size_t)x * (g_fb_bpp / 8U)];
    if (g_fb_bpp == 24U) {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
    } else if (g_fb_bpp == 32U) {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = 0xffU;
    } else {
        rgb565 = (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
        dst[0] = (unsigned char)(rgb565 & 0xffU);
        dst[1] = (unsigned char)(rgb565 >> 8);
    }
}

static void copy_area_to_outputs(const lv_area_t *area, const lv_color_t *color_p)
{
    int x;
    int y;
    for (y = area->y1; y <= area->y2; ++y) {
        if (y < 0 || y >= (int)g_height) {
            color_p += (area->x2 - area->x1 + 1);
            continue;
        }
        for (x = area->x1; x <= area->x2; ++x) {
            lv_color_t c = *color_p++;
            if (x >= 0 && x < (int)g_width) {
                unsigned char *dst = &g_frame[((unsigned int)y * g_width + (unsigned int)x) * 4U];
                dst[0] = LV_COLOR_GET_B(c);
                dst[1] = LV_COLOR_GET_G(c);
                dst[2] = LV_COLOR_GET_R(c);
                dst[3] = 0xffU;
                copy_pixel_to_fb((unsigned int)x, (unsigned int)y, c);
            }
        }
    }
}

static void remote_flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *color_p)
{
    copy_area_to_outputs(area, color_p);
    lv_disp_flush_ready(driver);
}

int v5_lvgl_remote_display_setup(unsigned int width, unsigned int height)
{
    if (g_display_ready) {
        return 1;
    }
    init_framebuffer(&width, &height);
    if (width == 0U || height == 0U || width > V5_UI_MAX_WIDTH || height > V5_UI_MAX_HEIGHT) {
        return 0;
    }
    g_width = width;
    g_height = height;
    memset(g_frame, 0x20, sizeof(g_frame));
    lv_disp_draw_buf_init(&g_draw, g_draw_buffer, 0, width * 20U);
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

void v5_lvgl_remote_display_render_now(void)
{
    lv_timer_handler();
    lv_refr_now(0);
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

static int ensure_listener(unsigned short port)
{
    struct sockaddr_in addr;
    int enable = 1;
    if (g_listen_fd >= 0) {
        return 1;
    }
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        return 0;
    }
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(g_listen_fd, 4) != 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return 0;
    }
    return 1;
}

static void send_json_response(int fd, const char *status, const char *json)
{
    char header[160];
    size_t body_len = strlen(json ? json : "");
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
             status ? status : "500 Internal Server Error",
             (unsigned long)body_len);
    (void)send_all(fd, header, strlen(header));
    (void)send_all(fd, json ? json : "", body_len);
}

static void serve_client(int fd)
{
    char request[512];
    char meta[160];
    char header[256];
    uint32_t meta_len;
    size_t payload_len;
    V5RemoteFrameSnapshot frame;
    ssize_t request_len = recv(fd, request, sizeof(request) - 1U, 0);
    if (request_len < 0) {
        request_len = 0;
    }
    request[request_len] = '\0';
    if (strncmp(request, "GET /remote/input/layout-click?", 31) == 0) {
        int x = -1;
        int y = -1;
        if (strstr(request, "mode=layout_only") &&
            sscanf(request, "GET /remote/input/layout-click?x=%d&y=%d", &x, &y) == 2 &&
            v5_lvgl_remote_input_layout_click(x, y)) {
            send_json_response(fd, "200 OK", "{\"ok\":true,\"mode\":\"layout_only\",\"kind\":\"click\"}\n");
        } else {
            send_json_response(fd, "403 Forbidden", "{\"ok\":false,\"required_mode\":\"layout_only\",\"kind\":\"click\"}\n");
        }
        return;
    }
    if (strncmp(request, "GET /remote/input/layout-drag?", 30) == 0) {
        int x1 = -1;
        int y1 = -1;
        int x2 = -1;
        int y2 = -1;
        int steps = 12;
        if (strstr(request, "mode=layout_only") &&
            sscanf(request, "GET /remote/input/layout-drag?x1=%d&y1=%d&x2=%d&y2=%d&steps=%d", &x1, &y1, &x2, &y2, &steps) >= 4 &&
            v5_lvgl_remote_input_layout_drag(x1, y1, x2, y2, steps)) {
            send_json_response(fd, "200 OK", "{\"ok\":true,\"mode\":\"layout_only\",\"kind\":\"drag\"}\n");
        } else {
            send_json_response(fd, "403 Forbidden", "{\"ok\":false,\"required_mode\":\"layout_only\",\"kind\":\"drag\"}\n");
        }
        return;
    }
    if (!v5_remote_frame_snapshot(&frame)) {
        (void)send_all(fd, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n", 55U);
        return;
    }
    snprintf(meta, sizeof(meta), "{\"width\":%u,\"height\":%u,\"stride\":%u,\"format\":\"bgra32\",\"source\":\"v5_lvgl_shell\"}", frame.width, frame.height, frame.stride);
    meta_len = (uint32_t)strlen(meta);
    payload_len = 4U + (size_t)meta_len + (size_t)frame.stride * frame.height;
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", (unsigned long)payload_len);
    (void)send_all(fd, header, strlen(header));
    (void)send_all(fd, &meta_len, sizeof(meta_len));
    (void)send_all(fd, meta, meta_len);
    (void)send_all(fd, frame.pixels, (size_t)frame.stride * frame.height);
}

int v5_remote_frame_poll(unsigned short port, unsigned int timeout_ms)
{
    struct timeval tv;
    fd_set readfds;
    int ready;
    int client;
    if (!ensure_listener(port)) {
        return 0;
    }
    FD_ZERO(&readfds);
    FD_SET(g_listen_fd, &readfds);
    tv.tv_sec = (long)(timeout_ms / 1000U);
    tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
    ready = select(g_listen_fd + 1, &readfds, 0, 0, &tv);
    if (ready < 0) {
        return errno == EINTR ? 1 : 0;
    }
    if (ready == 0) {
        return 1;
    }
    client = accept(g_listen_fd, 0, 0);
    if (client >= 0) {
        serve_client(client);
        close(client);
    }
    return 1;
}
