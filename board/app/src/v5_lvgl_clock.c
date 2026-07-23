#include "v5_lvgl_clock.h"

#include "lvgl.h"

#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#define V5_LVGL_CLOCK_FALLBACK_MS 10U

static uint64_t g_last_tick_ns;

static uint64_t monotonic_nanoseconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    uint64_t seconds;
    uint64_t remainder;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        !QueryPerformanceCounter(&counter) || counter.QuadPart < 0) {
        return 0U;
    }
    seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    remainder = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    return seconds * 1000000000ULL +
           (remainder * 1000000000ULL) / (uint64_t)frequency.QuadPart;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0U;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#endif
}

void v5_lvgl_clock_init(void)
{
    g_last_tick_ns = monotonic_nanoseconds();
}

void v5_lvgl_clock_advance(void)
{
    uint64_t now_ns = monotonic_nanoseconds();
    uint64_t elapsed_ms;
    if (now_ns == 0U) {
        lv_tick_inc(V5_LVGL_CLOCK_FALLBACK_MS);
        g_last_tick_ns = 0U;
        return;
    }
    if (g_last_tick_ns == 0U) {
        g_last_tick_ns = now_ns;
        return;
    }
    if (now_ns <= g_last_tick_ns) {
        return;
    }
    elapsed_ms = (now_ns - g_last_tick_ns) / 1000000ULL;
    if (elapsed_ms == 0U) {
        return;
    }
    if (elapsed_ms > UINT32_MAX) {
        elapsed_ms = UINT32_MAX;
    }
    lv_tick_inc((uint32_t)elapsed_ms);
    g_last_tick_ns += elapsed_ms * 1000000ULL;
}
