#include "v5_lvgl_remote_display_delta.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define V5_REMOTE_DISPLAY_DELTA_TILE_COLUMNS \
    ((V5_REMOTE_DISPLAY_DELTA_MAX_WIDTH + V5_REMOTE_DISPLAY_DELTA_TILE_SIZE - 1U) / \
     V5_REMOTE_DISPLAY_DELTA_TILE_SIZE)
#define V5_REMOTE_DISPLAY_DELTA_TILE_ROWS \
    ((V5_REMOTE_DISPLAY_DELTA_MAX_HEIGHT + V5_REMOTE_DISPLAY_DELTA_TILE_SIZE - 1U) / \
     V5_REMOTE_DISPLAY_DELTA_TILE_SIZE)

static unsigned long long rect_area(const V5RemoteDirtyRect *rect)
{
    return (unsigned long long)(rect->x2 - rect->x1 + 1) *
        (unsigned long long)(rect->y2 - rect->y1 + 1);
}

static V5RemoteDirtyRect rect_union(
    const V5RemoteDirtyRect *left,
    const V5RemoteDirtyRect *right)
{
    V5RemoteDirtyRect joined = *left;
    if (right->x1 < joined.x1) joined.x1 = right->x1;
    if (right->y1 < joined.y1) joined.y1 = right->y1;
    if (right->x2 > joined.x2) joined.x2 = right->x2;
    if (right->y2 > joined.y2) joined.y2 = right->y2;
    return joined;
}

static unsigned long long rect_overlap_area(
    const V5RemoteDirtyRect *left,
    const V5RemoteDirtyRect *right)
{
    int x1 = left->x1 > right->x1 ? left->x1 : right->x1;
    int y1 = left->y1 > right->y1 ? left->y1 : right->y1;
    int x2 = left->x2 < right->x2 ? left->x2 : right->x2;
    int y2 = left->y2 < right->y2 ? left->y2 : right->y2;
    if (x1 > x2 || y1 > y2) return 0ULL;
    return (unsigned long long)(x2 - x1 + 1) *
        (unsigned long long)(y2 - y1 + 1);
}

static int rects_form_exact_rectangle(
    const V5RemoteDirtyRect *left,
    const V5RemoteDirtyRect *right,
    V5RemoteDirtyRect *joined)
{
    unsigned long long union_pixels;
    *joined = rect_union(left, right);
    union_pixels = rect_area(left) + rect_area(right) -
        rect_overlap_area(left, right);
    return rect_area(joined) == union_pixels;
}

static void remove_rect(
    V5RemoteDirtyRect *rects,
    unsigned int *count,
    unsigned int index)
{
    if (index + 1U < *count) {
        memmove(
            &rects[index], &rects[index + 1U],
            (*count - index - 1U) * sizeof(rects[0]));
    }
    --*count;
}

static void add_changed_rect(
    V5RemoteDirtyRect *rects,
    unsigned int *count,
    unsigned int capacity,
    const V5RemoteDirtyRect *input)
{
    V5RemoteDirtyRect merged = *input;
    unsigned int index = 0U;
    while (index < *count) {
        V5RemoteDirtyRect joined;
        if (rects_form_exact_rectangle(&merged, &rects[index], &joined)) {
            merged = joined;
            remove_rect(rects, count, index);
        } else {
            ++index;
        }
    }
    if (*count >= capacity) {
        unsigned int best = 0U;
        unsigned long long best_growth = ~0ULL;
        for (index = 0U; index < *count; ++index) {
            V5RemoteDirtyRect joined = rect_union(&merged, &rects[index]);
            unsigned long long growth = rect_area(&joined) -
                rect_area(&merged) - rect_area(&rects[index]) +
                rect_overlap_area(&merged, &rects[index]);
            if (growth < best_growth) {
                best = index;
                best_growth = growth;
            }
        }
        merged = rect_union(&merged, &rects[best]);
        remove_rect(rects, count, best);
    }
    rects[(*count)++] = merged;
}

static unsigned int trailing_zeroes_u64(uint64_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned int)__builtin_ctzll(value);
#else
    unsigned int count = 0U;
    while ((value & 1ULL) == 0ULL) {
        value >>= 1;
        ++count;
    }
    return count;
#endif
}

int v5_lvgl_remote_display_delta_commit(
    const unsigned char *frame,
    unsigned char *published_frame,
    unsigned int width,
    unsigned int height,
    const V5RemoteDirtyRect *candidate_rects,
    unsigned int candidate_count,
    V5RemoteDirtyRect *changed_rects,
    unsigned int changed_capacity,
    unsigned int *changed_count)
{
    uint64_t candidate_tiles[V5_REMOTE_DISPLAY_DELTA_TILE_ROWS] = {0ULL};
    unsigned int index;
    unsigned int tile_row;
    size_t stride;
    if (!frame || !published_frame || !changed_rects || !changed_count ||
        width == 0U || height == 0U ||
        width > V5_REMOTE_DISPLAY_DELTA_MAX_WIDTH ||
        height > V5_REMOTE_DISPLAY_DELTA_MAX_HEIGHT ||
        changed_capacity == 0U ||
        (candidate_count > 0U && !candidate_rects)) return 0;
    *changed_count = 0U;
    stride = (size_t)width * 4U;
    for (index = 0U; index < candidate_count; ++index) {
        int x1 = candidate_rects[index].x1;
        int y1 = candidate_rects[index].y1;
        int x2 = candidate_rects[index].x2;
        int y2 = candidate_rects[index].y2;
        unsigned int first_column;
        unsigned int last_column;
        unsigned int first_row;
        unsigned int last_row;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= (int)width) x2 = (int)width - 1;
        if (y2 >= (int)height) y2 = (int)height - 1;
        if (x1 > x2 || y1 > y2) continue;
        first_column = (unsigned int)x1 / V5_REMOTE_DISPLAY_DELTA_TILE_SIZE;
        last_column = (unsigned int)x2 / V5_REMOTE_DISPLAY_DELTA_TILE_SIZE;
        first_row = (unsigned int)y1 / V5_REMOTE_DISPLAY_DELTA_TILE_SIZE;
        last_row = (unsigned int)y2 / V5_REMOTE_DISPLAY_DELTA_TILE_SIZE;
        for (tile_row = first_row; tile_row <= last_row; ++tile_row) {
            unsigned int column;
            for (column = first_column; column <= last_column; ++column) {
                candidate_tiles[tile_row] |= 1ULL << column;
            }
        }
    }
    for (tile_row = 0U; tile_row < V5_REMOTE_DISPLAY_DELTA_TILE_ROWS;
         ++tile_row) {
        uint64_t columns = candidate_tiles[tile_row];
        while (columns != 0ULL) {
            unsigned int column = trailing_zeroes_u64(columns);
            unsigned int x = column * V5_REMOTE_DISPLAY_DELTA_TILE_SIZE;
            unsigned int y = tile_row * V5_REMOTE_DISPLAY_DELTA_TILE_SIZE;
            unsigned int tile_width = V5_REMOTE_DISPLAY_DELTA_TILE_SIZE;
            unsigned int tile_height = V5_REMOTE_DISPLAY_DELTA_TILE_SIZE;
            unsigned int row;
            int changed = 0;
            V5RemoteDirtyRect rect;
            columns &= columns - 1ULL;
            if (x >= width || y >= height) continue;
            if (x + tile_width > width) tile_width = width - x;
            if (y + tile_height > height) tile_height = height - y;
            for (row = 0U; row < tile_height; ++row) {
                size_t offset = (size_t)(y + row) * stride + (size_t)x * 4U;
                if (memcmp(&frame[offset], &published_frame[offset],
                           (size_t)tile_width * 4U) != 0) {
                    changed = 1;
                    break;
                }
            }
            if (!changed) continue;
            for (row = 0U; row < tile_height; ++row) {
                size_t offset = (size_t)(y + row) * stride + (size_t)x * 4U;
                memcpy(
                    &published_frame[offset], &frame[offset],
                    (size_t)tile_width * 4U);
            }
            rect.x1 = (int)x;
            rect.y1 = (int)y;
            rect.x2 = (int)(x + tile_width - 1U);
            rect.y2 = (int)(y + tile_height - 1U);
            add_changed_rect(
                changed_rects, changed_count, changed_capacity, &rect);
        }
    }
    return 1;
}
