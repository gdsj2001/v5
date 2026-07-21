#include "v5_lvgl_remote_display_delta.h"

#include <stdio.h>
#include <string.h>

#define WIDTH 64U
#define HEIGHT 64U
#define FRAME_BYTES (WIDTH * HEIGHT * 4U)

static void set_pixel(unsigned char *frame, unsigned int x, unsigned int y,
                      unsigned char value)
{
    size_t offset = ((size_t)y * WIDTH + x) * 4U;
    frame[offset] = value;
    frame[offset + 1U] = (unsigned char)(value + 1U);
    frame[offset + 2U] = (unsigned char)(value + 2U);
    frame[offset + 3U] = 255U;
}

int main(void)
{
    unsigned char frame[FRAME_BYTES] = {0};
    unsigned char published[FRAME_BYTES] = {0};
    V5RemoteDirtyRect candidates[2] = {{0, 0, 63, 63}, {0, 0, 15, 15}};
    V5RemoteDirtyRect changed[16];
    unsigned int changed_count = 99U;
    if (!v5_lvgl_remote_display_delta_commit(
            frame, published, WIDTH, HEIGHT,
            candidates, 1U, changed, 16U, &changed_count) ||
        changed_count != 0U) return 1;

    set_pixel(frame, 1U, 1U, 10U);
    if (!v5_lvgl_remote_display_delta_commit(
            frame, published, WIDTH, HEIGHT,
            candidates, 1U, changed, 16U, &changed_count) ||
        changed_count != 1U || changed[0].x1 != 0 || changed[0].y1 != 0 ||
        changed[0].x2 != 15 || changed[0].y2 != 15 ||
        memcmp(frame, published, FRAME_BYTES) != 0) return 2;

    memset(published, 0, sizeof(published));
    memset(frame, 0, sizeof(frame));
    set_pixel(frame, 1U, 1U, 20U);
    set_pixel(frame, 31U, 31U, 30U);
    if (!v5_lvgl_remote_display_delta_commit(
            frame, published, WIDTH, HEIGHT,
        candidates, 1U, changed, 16U, &changed_count) ||
        changed_count != 2U ||
        changed[0].x1 != 0 || changed[0].y1 != 0 ||
        changed[0].x2 != 15 || changed[0].y2 != 15 ||
        memcmp(frame, published, FRAME_BYTES) != 0) return 3;

    memset(published, 0, sizeof(published));
    memset(frame, 0, sizeof(frame));
    set_pixel(frame, 1U, 1U, 40U);
    set_pixel(frame, 17U, 1U, 50U);
    set_pixel(frame, 33U, 33U, 60U);
    if (!v5_lvgl_remote_display_delta_commit(
            frame, published, WIDTH, HEIGHT,
            candidates, 1U, changed, 2U, &changed_count) ||
        changed_count > 2U ||
        memcmp(frame, published, FRAME_BYTES) != 0) return 4;

    memset(published, 0, sizeof(published));
    memset(frame, 0, sizeof(frame));
    set_pixel(frame, 48U, 48U, 70U);
    if (!v5_lvgl_remote_display_delta_commit(
            frame, published, WIDTH, HEIGHT,
            &candidates[1], 1U, changed, 16U, &changed_count) ||
        changed_count != 0U ||
        memcmp(frame, published, FRAME_BYTES) == 0) return 5;

    puts("v5 LVGL remote display delta smoke: ok");
    return 0;
}
