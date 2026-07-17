#ifndef V5_UI_REFRESH_SCHEDULE_H
#define V5_UI_REFRESH_SCHEDULE_H

#include <stdint.h>

static inline int v5_ui_refresh_deadline_due(
    uint64_t now_ns,
    uint64_t *anchor_ns,
    uint64_t period_ns)
{
    uint64_t elapsed_ns;
    uint64_t elapsed_periods;

    if (!anchor_ns || period_ns == 0ULL) {
        return 0;
    }
    if (*anchor_ns == 0ULL || now_ns < *anchor_ns) {
        *anchor_ns = now_ns;
        return 1;
    }
    elapsed_ns = now_ns - *anchor_ns;
    if (elapsed_ns < period_ns) {
        return 0;
    }
    elapsed_periods = elapsed_ns / period_ns;
    *anchor_ns += elapsed_periods * period_ns;
    return 1;
}

static inline uint64_t v5_ui_refresh_wait_ns(
    uint64_t now_ns,
    uint64_t anchor_ns,
    uint64_t period_ns,
    uint64_t max_wait_ns)
{
    uint64_t elapsed_ns;
    uint64_t remaining_ns;

    if (anchor_ns == 0ULL || period_ns == 0ULL || now_ns < anchor_ns) {
        return 0ULL;
    }
    elapsed_ns = now_ns - anchor_ns;
    if (elapsed_ns >= period_ns) {
        return 0ULL;
    }
    remaining_ns = period_ns - elapsed_ns;
    return remaining_ns < max_wait_ns ? remaining_ns : max_wait_ns;
}

#endif
