#include "v5_lvgl_remote_display.h"

static unsigned char g_test_cache_valid[V5_REMOTE_DISPLAY_CACHE_COUNT];
static int g_test_output_suppressed;

int v5_lvgl_remote_display_cache_capture(unsigned int slot)
{
    if (slot >= V5_REMOTE_DISPLAY_CACHE_COUNT) {
        return 0;
    }
    g_test_cache_valid[slot] = 1U;
    return 1;
}

int v5_lvgl_remote_display_cache_blit(unsigned int slot)
{
    return !g_test_output_suppressed &&
        slot < V5_REMOTE_DISPLAY_CACHE_COUNT &&
        g_test_cache_valid[slot];
}

int v5_lvgl_remote_display_publish_current_frame(void)
{
    return !g_test_output_suppressed;
}

int v5_lvgl_remote_display_set_output_suppressed(int suppressed)
{
    int previous = g_test_output_suppressed;
    g_test_output_suppressed = suppressed ? 1 : 0;
    return previous;
}
