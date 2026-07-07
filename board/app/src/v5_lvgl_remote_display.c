#include "v5_lvgl_remote_display.h"

#include "v5_remote_input_socket.h"
#include "v5_remote_metrics.h"
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
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define V5_UI_MAX_WIDTH 1024U
#define V5_UI_MAX_HEIGHT 600U
#define V5_FB_MODE_PATH "/sys/class/graphics/fb0/modes"
#define V5_FB_STRIDE_PATH "/sys/class/graphics/fb0/stride"
#define V5_FB_BPP_PATH "/sys/class/graphics/fb0/bits_per_pixel"
#define V5_REMOTE_DEFAULT_BIND "0.0.0.0"
#define V5_REMOTE_DEFAULT_ALLOW_CIDRS "127.0.0.1/32,192.168.1.0/24"

static lv_color_t g_draw_buffer[V5_UI_MAX_WIDTH * 20U];
static unsigned char g_frame[V5_UI_MAX_WIDTH * V5_UI_MAX_HEIGHT * 4U];
static unsigned char g_cached_frame[V5_REMOTE_DISPLAY_CACHE_COUNT][V5_UI_MAX_WIDTH * V5_UI_MAX_HEIGHT * 4U];
static unsigned char g_cached_fb[V5_REMOTE_DISPLAY_CACHE_COUNT][V5_UI_MAX_WIDTH * V5_UI_MAX_HEIGHT * 4U];
static size_t g_cached_fb_size[V5_REMOTE_DISPLAY_CACHE_COUNT];
static int g_cached_valid[V5_REMOTE_DISPLAY_CACHE_COUNT];
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
static unsigned long long g_frame_id;

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

static unsigned long long monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((unsigned long long)ts.tv_sec * 1000000000ULL) + (unsigned long long)ts.tv_nsec;
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

static void copy_area_to_outputs(const lv_area_t *area, const lv_color_t *color_p)
{
    int src_w;
    int x1;
    int x2;
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
    if (x1 > x2) {
        return;
    }
    for (y = area->y1; y <= area->y2; ++y) {
        const lv_color_t *src_row = color_p + (size_t)(y - area->y1) * (size_t)src_w;
        const unsigned char *src_bytes;
        unsigned char *frame_row;
        unsigned int visible_pixels;
        if (y < 0 || y >= (int)g_height) {
            continue;
        }
        src_row += x1 - area->x1;
        src_bytes = (const unsigned char *)src_row;
        frame_row = &g_frame[((unsigned int)y * g_width + (unsigned int)x1) * 4U];
        visible_pixels = (unsigned int)(x2 - x1 + 1);
        memcpy(frame_row, src_bytes, (size_t)visible_pixels * 4U);
        copy_row_to_fb((unsigned int)x1, (unsigned int)y, src_bytes, visible_pixels);
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

int v5_lvgl_remote_display_cache_capture(unsigned int slot)
{
    size_t frame_size;
    if (!g_display_ready || slot >= V5_REMOTE_DISPLAY_CACHE_COUNT) {
        return 0;
    }
    frame_size = (size_t)g_width * (size_t)g_height * 4U;
    memcpy(g_cached_frame[slot], g_frame, frame_size);
    g_cached_fb_size[slot] = 0U;
    if (g_fb && g_fb_size > 0U && g_fb_size <= sizeof(g_cached_fb[slot])) {
        memcpy(g_cached_fb[slot], g_fb, g_fb_size);
        g_cached_fb_size[slot] = g_fb_size;
    }
    g_cached_valid[slot] = 1;
    return 1;
}

int v5_lvgl_remote_display_cache_blit(unsigned int slot)
{
    size_t frame_size;
    if (!g_display_ready || slot >= V5_REMOTE_DISPLAY_CACHE_COUNT || !g_cached_valid[slot]) {
        return 0;
    }
    frame_size = (size_t)g_width * (size_t)g_height * 4U;
    memcpy(g_frame, g_cached_frame[slot], frame_size);
    if (g_fb && g_cached_fb_size[slot] > 0U && g_cached_fb_size[slot] <= g_fb_size) {
        memcpy(g_fb, g_cached_fb[slot], g_cached_fb_size[slot]);
    }
    ++g_frame_id;
    return 1;
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

static unsigned int cidr_mask(unsigned int prefix)
{
    if (prefix == 0U) {
        return 0U;
    }
    if (prefix >= 32U) {
        return 0xffffffffU;
    }
    return 0xffffffffU << (32U - prefix);
}

static const char *skip_spaces(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        ++p;
    }
    return p;
}

static int parse_ipv4_cidr_token(const char *begin, const char *end, unsigned int *network, unsigned int *mask)
{
    char token[64];
    char *slash;
    char *tail;
    struct in_addr addr;
    unsigned long prefix = 32UL;
    size_t len;
    if (!begin || !end || end <= begin || !network || !mask) {
        return 0;
    }
    while (begin < end && (*begin == ' ' || *begin == '\t')) {
        ++begin;
    }
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        --end;
    }
    len = (size_t)(end - begin);
    if (len == 0U || len >= sizeof(token)) {
        return 0;
    }
    memcpy(token, begin, len);
    token[len] = '\0';
    slash = strchr(token, '/');
    if (slash) {
        *slash = '\0';
        if (!slash[1]) {
            return 0;
        }
        prefix = strtoul(slash + 1, &tail, 10);
        if (*tail || prefix > 32UL) {
            return 0;
        }
    }
    if (strcmp(token, "*") == 0) {
        *network = 0U;
        *mask = 0U;
        return 1;
    }
    if (inet_aton(token, &addr) == 0) {
        return 0;
    }
    *mask = cidr_mask((unsigned int)prefix);
    *network = ntohl(addr.s_addr) & *mask;
    return 1;
}

static int remote_peer_allowed(const struct sockaddr_in *peer)
{
    const char *allow = getenv("V5_UI_REMOTE_ALLOW_CIDRS");
    const char *cursor;
    unsigned int peer_addr;
    if (!peer) {
        return 0;
    }
    if (!allow || !allow[0]) {
        allow = V5_REMOTE_DEFAULT_ALLOW_CIDRS;
    }
    cursor = allow;
    peer_addr = ntohl(peer->sin_addr.s_addr);
    while (cursor && *cursor) {
        const char *end;
        unsigned int network;
        unsigned int mask;
        cursor = skip_spaces(cursor);
        end = strchr(cursor, ',');
        if (!end) {
            end = cursor + strlen(cursor);
        }
        if (parse_ipv4_cidr_token(cursor, end, &network, &mask) && ((peer_addr & mask) == network)) {
            return 1;
        }
        cursor = *end ? end + 1 : end;
    }
    return 0;
}

static int parse_bind_address(struct in_addr *addr)
{
    const char *bind_addr = getenv("V5_UI_REMOTE_BIND");
    if (!addr) {
        return 0;
    }
    if (!bind_addr || !bind_addr[0]) {
        bind_addr = V5_REMOTE_DEFAULT_BIND;
    }
    if (inet_aton(bind_addr, addr) == 0) {
        return 0;
    }
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
    if (!parse_bind_address(&addr.sin_addr)) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return 0;
    }
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

static int request_path_is(const char *request, const char *path)
{
    size_t path_len = strlen(path);
    if (strncmp(request, "GET ", 4) != 0) {
        return 0;
    }
    if (strncmp(request + 4, path, path_len) != 0) {
        return 0;
    }
    return request[4 + path_len] == ' ' || request[4 + path_len] == '?' || request[4 + path_len] == '\r';
}

static int serve_client(int fd)
{
    char request[2048];
    char meta[320];
    char header[256];
    uint32_t meta_len;
    size_t payload_len;
    V5RemoteFrameSnapshot frame;
    ssize_t request_len = recv(fd, request, sizeof(request) - 1U, 0);
    if (request_len < 0) {
        request_len = 0;
    }
    request[request_len] = '\0';
    if (request_path_is(request, "/remote/input")) {
        return v5_remote_input_socket_begin(fd, request);
    }
    if (!v5_remote_frame_snapshot(&frame)) {
        (void)send_all(fd, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n", 55U);
        return 0;
    }
    if (request_path_is(request, "/remote/info")) {
        char info[720];
        char metrics[480];
        v5_remote_metrics_json(metrics, sizeof(metrics));
        int input_enabled = v5_remote_input_socket_enabled();
        snprintf(info, sizeof(info),
                 "{\"protocol_version\":\"8ax-remote-ui/1\",\"width\":%u,\"height\":%u,\"pixel_format\":\"bgra32\",\"stride\":%u,\"view_only\":%s,\"input_enabled\":%s,\"system_metrics\":%s}\n",
                 frame.width,
                 frame.height,
                 frame.stride,
                 input_enabled ? "false" : "true",
                 input_enabled ? "true" : "false",
                 metrics);
        send_json_response(fd, "200 OK", info);
        return 0;
    }
    if (!request_path_is(request, "/remote/frame/full")) {
        send_json_response(fd, "404 Not Found", "{\"ok\":false,\"error\":\"unsupported_remote_path\"}\n");
        return 0;
    }
    if (g_frame_id == 0ULL) {
        g_frame_id = 1ULL;
    } else {
        ++g_frame_id;
    }
    snprintf(meta, sizeof(meta),
             "{\"type\":\"full_frame\",\"frame_id\":%llu,\"base_frame_id\":0,\"monotonic_ns\":%llu,\"width\":%u,\"height\":%u,\"stride\":%u,\"format\":\"bgra32\",\"dirty_count\":0,\"rects\":[]}",
             g_frame_id, monotonic_ns(), frame.width, frame.height, frame.stride);
    meta_len = (uint32_t)strlen(meta);
    payload_len = 4U + (size_t)meta_len + (size_t)frame.stride * frame.height;
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", (unsigned long)payload_len);
    (void)send_all(fd, header, strlen(header));
    (void)send_all(fd, &meta_len, sizeof(meta_len));
    (void)send_all(fd, meta, meta_len);
    (void)send_all(fd, frame.pixels, (size_t)frame.stride * frame.height);
    return 0;
}

int v5_remote_frame_poll(unsigned short port, unsigned int timeout_ms)
{
    struct timeval tv;
    fd_set readfds;
    int ready;
    int client;
    int input_fd;
    int max_fd;
    if (!ensure_listener(port)) {
        return 0;
    }
    FD_ZERO(&readfds);
    FD_SET(g_listen_fd, &readfds);
    max_fd = g_listen_fd;
    input_fd = v5_remote_input_socket_fd();
    if (input_fd >= 0) {
        FD_SET(input_fd, &readfds);
        if (input_fd > max_fd) {
            max_fd = input_fd;
        }
    }
    tv.tv_sec = (long)(timeout_ms / 1000U);
    tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
    ready = select(max_fd + 1, &readfds, 0, 0, &tv);
    if (ready < 0) {
        return errno == EINTR ? 1 : 0;
    }
    if (ready == 0) {
        return 1;
    }
    if (input_fd >= 0 && FD_ISSET(input_fd, &readfds)) {
        v5_remote_input_socket_process();
    }
    if (FD_ISSET(g_listen_fd, &readfds)) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        memset(&peer, 0, sizeof(peer));
        client = accept(g_listen_fd, (struct sockaddr *)&peer, &peer_len);
        if (client >= 0) {
            if (peer.sin_family != AF_INET || !remote_peer_allowed(&peer)) {
                send_json_response(client, "403 Forbidden", "{\"ok\":false,\"error\":\"remote_peer_not_allowed\"}\n");
                close(client);
            } else if (!serve_client(client)) {
                close(client);
            }
        }
    }
    return 1;
}
