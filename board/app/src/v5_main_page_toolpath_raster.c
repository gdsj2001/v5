#include "v5_main_page.h"

#include "v5_main_page_internal.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define V5_TOOLPATH_RASTER_DIRTY_RECT_BUDGET 12U

static size_t program_raster_bit_index(int x, int y)
{
    return (size_t)y * (size_t)V5_TOOLPATH_VIEWPORT_WIDTH + (size_t)x;
}
static int program_raster_pixel_from(
    const uint64_t raster[V5_TOOLPATH_RASTER_WORD_COUNT],
    int x,
    int y)
{
    size_t bit;
    if (!raster || x < 0 || y < 0 ||
        x >= V5_TOOLPATH_VIEWPORT_WIDTH ||
        y >= V5_TOOLPATH_VIEWPORT_HEIGHT) return 0;
    bit = program_raster_bit_index(x, y);
    return (raster[bit >> 6] & (1ULL << (bit & 63U))) != 0ULL;
}

static void program_raster_set(
    uint64_t raster[V5_TOOLPATH_RASTER_WORD_COUNT],
    int x,
    int y)
{
    size_t bit;
    if (!raster || x < 0 || y < 0 ||
        x >= V5_TOOLPATH_VIEWPORT_WIDTH ||
        y >= V5_TOOLPATH_VIEWPORT_HEIGHT) return;
    bit = program_raster_bit_index(x, y);
    raster[bit >> 6] |= 1ULL << (bit & 63U);
}

enum {
    V5_CLIP_LEFT = 1,
    V5_CLIP_RIGHT = 2,
    V5_CLIP_TOP = 4,
    V5_CLIP_BOTTOM = 8,
};

static unsigned int program_clip_code(double x, double y)
{
    unsigned int code = 0U;
    if (x < 0.0) code |= V5_CLIP_LEFT;
    else if (x > (double)(V5_TOOLPATH_VIEWPORT_WIDTH - 1)) {
        code |= V5_CLIP_RIGHT;
    }
    if (y < 0.0) code |= V5_CLIP_TOP;
    else if (y > (double)(V5_TOOLPATH_VIEWPORT_HEIGHT - 1)) {
        code |= V5_CLIP_BOTTOM;
    }
    return code;
}

static int clip_program_segment(
    int *x0,
    int *y0,
    int *x1,
    int *y1)
{
    double ax;
    double ay;
    double bx;
    double by;
    unsigned int code_a;
    unsigned int code_b;
    if (!x0 || !y0 || !x1 || !y1) return 0;
    ax = (double)*x0;
    ay = (double)*y0;
    bx = (double)*x1;
    by = (double)*y1;
    code_a = program_clip_code(ax, ay);
    code_b = program_clip_code(bx, by);
    while (1) {
        unsigned int outside;
        double x;
        double y;
        if ((code_a | code_b) == 0U) break;
        if ((code_a & code_b) != 0U) return 0;
        outside = code_a ? code_a : code_b;
        if (outside & V5_CLIP_TOP) {
            if (by == ay) return 0;
            x = ax + (bx - ax) * (0.0 - ay) / (by - ay);
            y = 0.0;
        } else if (outside & V5_CLIP_BOTTOM) {
            if (by == ay) return 0;
            y = (double)(V5_TOOLPATH_VIEWPORT_HEIGHT - 1);
            x = ax + (bx - ax) * (y - ay) / (by - ay);
        } else if (outside & V5_CLIP_RIGHT) {
            if (bx == ax) return 0;
            x = (double)(V5_TOOLPATH_VIEWPORT_WIDTH - 1);
            y = ay + (by - ay) * (x - ax) / (bx - ax);
        } else {
            if (bx == ax) return 0;
            x = 0.0;
            y = ay + (by - ay) * (0.0 - ax) / (bx - ax);
        }
        if (outside == code_a) {
            ax = x;
            ay = y;
            code_a = program_clip_code(ax, ay);
        } else {
            bx = x;
            by = y;
            code_b = program_clip_code(bx, by);
        }
    }
    *x0 = (int)lround(ax);
    *y0 = (int)lround(ay);
    *x1 = (int)lround(bx);
    *y1 = (int)lround(by);
    return 1;
}

static void rasterize_clipped_program_segment(
    uint64_t raster[V5_TOOLPATH_RASTER_WORD_COUNT],
    V5StatusScreenPoint start_point,
    V5StatusScreenPoint end_point)
{
    int x = (int)lroundf(start_point.x);
    int y = (int)lroundf(start_point.y);
    int end_x = (int)lroundf(end_point.x);
    int end_y = (int)lroundf(end_point.y);
    int dx;
    int dy_abs;
    int step_x;
    int step_y;
    int dy;
    int error;
    if (!clip_program_segment(&x, &y, &end_x, &end_y)) return;
    dx = end_x >= x ? end_x - x : x - end_x;
    dy_abs = end_y >= y ? end_y - y : y - end_y;
    step_x = x < end_x ? 1 : -1;
    step_y = y < end_y ? 1 : -1;
    dy = -dy_abs;
    error = dx + dy;
    while (1) {
        program_raster_set(raster, x, y);
        if (x == end_x && y == end_y) break;
        {
            const int twice_error = error * 2;
            if (twice_error >= dy) {
                error += dy;
                x += step_x;
            }
            if (twice_error <= dx) {
                error += dx;
                y += step_y;
            }
        }
    }
}

static void build_program_raster(
    uint64_t raster[V5_TOOLPATH_RASTER_WORD_COUNT],
    const V5StatusDisplayScene *scene)
{
    unsigned int i;
    memset(raster, 0, sizeof(uint64_t) * V5_TOOLPATH_RASTER_WORD_COUNT);
    if (!scene) return;
    for (i = 1U; i < scene->point_count; ++i) {
        if (scene->break_before[i]) continue;
        rasterize_clipped_program_segment(
            raster, scene->points[i - 1U], scene->points[i]);
    }
}

static uint64_t raster_dirty_pixels(const lv_area_t *area)
{
    if (!area || area->x1 > area->x2 || area->y1 > area->y2) return 0ULL;
    return (uint64_t)((int)area->x2 - (int)area->x1 + 1) *
        (uint64_t)((int)area->y2 - (int)area->y1 + 1);
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

static void raster_changed_tile_rows(
    const uint64_t old_raster[V5_TOOLPATH_RASTER_WORD_COUNT],
    const uint64_t new_raster[V5_TOOLPATH_RASTER_WORD_COUNT],
    uint64_t changed_rows[V5_TOOLPATH_RASTER_TILE_ROWS])
{
    size_t word;
    memset(changed_rows, 0,
        sizeof(uint64_t) * V5_TOOLPATH_RASTER_TILE_ROWS);
    for (word = 0U; word < V5_TOOLPATH_RASTER_WORD_COUNT; ++word) {
        uint64_t changed = old_raster[word] ^ new_raster[word];
        while (changed != 0ULL) {
            const unsigned int offset = trailing_zeroes_u64(changed);
            const size_t pixel = word * 64U + offset;
            size_t y;
            size_t x;
            unsigned int tile_row;
            unsigned int tile_column;
            changed &= changed - 1ULL;
            if (pixel >= (size_t)V5_TOOLPATH_RASTER_PIXEL_COUNT) continue;
            y = pixel / (size_t)V5_TOOLPATH_VIEWPORT_WIDTH;
            x = pixel - y * (size_t)V5_TOOLPATH_VIEWPORT_WIDTH;
            tile_row = (unsigned int)(y / V5_TOOLPATH_RASTER_TILE_SIZE);
            tile_column = (unsigned int)(x / V5_TOOLPATH_RASTER_TILE_SIZE);
            if (tile_row < V5_TOOLPATH_RASTER_TILE_ROWS &&
                tile_column < V5_TOOLPATH_RASTER_TILE_COLUMNS) {
                changed_rows[tile_row] |= 1ULL << tile_column;
            }
        }
    }
}

static void merge_raster_dirty_areas(lv_area_t *areas, unsigned int *count)
{
    if (!areas || !count) return;
    while (*count > V5_TOOLPATH_RASTER_DIRTY_RECT_BUDGET) {
        unsigned int first;
        unsigned int second;
        unsigned int best_first = 0U;
        unsigned int best_second = 1U;
        int64_t best_extra = INT64_MAX;
        lv_area_t best_union = areas[0];
        for (first = 0U; first + 1U < *count; ++first) {
            for (second = first + 1U; second < *count; ++second) {
                lv_area_t joined = areas[first];
                int64_t extra;
                if (areas[second].x1 < joined.x1) joined.x1 = areas[second].x1;
                if (areas[second].y1 < joined.y1) joined.y1 = areas[second].y1;
                if (areas[second].x2 > joined.x2) joined.x2 = areas[second].x2;
                if (areas[second].y2 > joined.y2) joined.y2 = areas[second].y2;
                extra = (int64_t)raster_dirty_pixels(&joined) -
                    (int64_t)raster_dirty_pixels(&areas[first]) -
                    (int64_t)raster_dirty_pixels(&areas[second]);
                if (extra < best_extra) {
                    best_extra = extra;
                    best_first = first;
                    best_second = second;
                    best_union = joined;
                }
            }
        }
        areas[best_first] = best_union;
        memmove(
            &areas[best_second], &areas[best_second + 1U],
            (*count - best_second - 1U) * sizeof(areas[0]));
        --*count;
    }
}

static void invalidate_program_raster_delta(
    V5MainPage *page,
    const uint64_t old_raster[V5_TOOLPATH_RASTER_WORD_COUNT],
    const uint64_t new_raster[V5_TOOLPATH_RASTER_WORD_COUNT])
{
    lv_area_t areas[
        V5_TOOLPATH_RASTER_TILE_ROWS * V5_TOOLPATH_RASTER_TILE_COLUMNS];
    uint64_t changed_rows[V5_TOOLPATH_RASTER_TILE_ROWS];
    lv_area_t coords;
    unsigned int area_count = 0U;
    unsigned int row;
    if (!page || !page->trajectory_line || !old_raster || !new_raster) return;
    raster_changed_tile_rows(old_raster, new_raster, changed_rows);
    for (row = 0U; row < V5_TOOLPATH_RASTER_TILE_ROWS; ++row) {
        unsigned int column;
        for (column = 0U; column < V5_TOOLPATH_RASTER_TILE_COLUMNS;) {
            unsigned int first_column;
            unsigned int last_column;
            lv_area_t *area;
            if ((changed_rows[row] & (1ULL << column)) == 0ULL) {
                ++column;
                continue;
            }
            first_column = column;
            while (column + 1U < V5_TOOLPATH_RASTER_TILE_COLUMNS &&
                   (changed_rows[row] & (1ULL << (column + 1U))) != 0ULL) {
                ++column;
            }
            last_column = column;
            area = &areas[area_count++];
            area->x1 = (lv_coord_t)(
                (int)first_column * (int)V5_TOOLPATH_RASTER_TILE_SIZE);
            area->x2 = (lv_coord_t)(
                ((int)last_column + 1) *
                    (int)V5_TOOLPATH_RASTER_TILE_SIZE - 1);
            area->y1 = (lv_coord_t)(
                row * V5_TOOLPATH_RASTER_TILE_SIZE);
            area->y2 = (lv_coord_t)(
                (row + 1U) * V5_TOOLPATH_RASTER_TILE_SIZE - 1U);
            if (area->x2 >= V5_TOOLPATH_VIEWPORT_WIDTH) {
                area->x2 = V5_TOOLPATH_VIEWPORT_WIDTH - 1;
            }
            if (area->y2 >= V5_TOOLPATH_VIEWPORT_HEIGHT) {
                area->y2 = V5_TOOLPATH_VIEWPORT_HEIGHT - 1;
            }
            ++column;
        }
    }
    merge_raster_dirty_areas(areas, &area_count);
    lv_obj_get_coords(page->trajectory_line, &coords);
    for (row = 0U; row < area_count; ++row) {
        lv_area_t dirty = areas[row];
        uint64_t pixels;
        dirty.x1 += coords.x1;
        dirty.x2 += coords.x1;
        dirty.y1 += coords.y1;
        dirty.y2 += coords.y1;
        lv_obj_invalidate_area(page->trajectory_line, &dirty);
        pixels = raster_dirty_pixels(&dirty);
        page->toolpath_line_last_dirty_rect_count += 1U;
        page->toolpath_line_last_dirty_pixels += pixels;
        if (pixels > page->toolpath_line_last_dirty_max_pixels) {
            page->toolpath_line_last_dirty_max_pixels = pixels;
        }
    }
}

void v5_main_page_internal_update_program_raster(
    V5MainPage *page,
    const V5StatusDisplayScene *scene,
    uint64_t scene_generation)
{
    unsigned int next;
    uint64_t *old_raster;
    uint64_t *new_raster;
    if (!page || (page->toolpath_program_raster_valid &&
                  page->toolpath_program_raster_generation ==
                      scene_generation)) return;
    next = page->toolpath_program_raster_active ^ 1U;
    old_raster = page->toolpath_program_raster[
        page->toolpath_program_raster_active];
    new_raster = page->toolpath_program_raster[next];
    build_program_raster(new_raster, scene);
    invalidate_program_raster_delta(page, old_raster, new_raster);
    page->toolpath_program_raster_active = next;
    page->toolpath_program_raster_generation = scene_generation;
    page->toolpath_program_raster_build_count += 1ULL;
    page->toolpath_program_raster_valid = 1;
}

void v5_main_page_internal_clear_program_raster(V5MainPage *page)
{
    unsigned int next;
    uint64_t *old_raster;
    uint64_t *new_raster;
    if (!page || !page->toolpath_program_raster_valid) return;
    next = page->toolpath_program_raster_active ^ 1U;
    old_raster = page->toolpath_program_raster[
        page->toolpath_program_raster_active];
    new_raster = page->toolpath_program_raster[next];
    memset(new_raster, 0, sizeof(page->toolpath_program_raster[next]));
    invalidate_program_raster_delta(page, old_raster, new_raster);
    page->toolpath_program_raster_active = next;
    page->toolpath_program_raster_generation = 0ULL;
    page->toolpath_program_raster_valid = 0;
}

int v5_main_page_internal_program_raster_pixel(
    const V5MainPage *page,
    int x,
    int y)
{
    if (!page || !page->toolpath_program_raster_valid) return 0;
    return program_raster_pixel_from(
        page->toolpath_program_raster[page->toolpath_program_raster_active],
        x, y);
}

void v5_main_page_internal_draw_program_raster(
    const V5MainPage *page,
    lv_draw_ctx_t *draw_ctx,
    lv_coord_t x_offset,
    lv_coord_t y_offset)
{
    const uint64_t *raster;
    const lv_area_t *buffer_area;
    const lv_area_t *clip;
    lv_color_t *buffer;
    const lv_color_t color = lv_color_make(255, 214, 64);
    int x1;
    int y1;
    int x2;
    int y2;
    int stride;
    int y;
    if (!page || !page->toolpath_program_raster_valid || !draw_ctx ||
        !draw_ctx->buf || !draw_ctx->buf_area || !draw_ctx->clip_area) return;
    raster = page->toolpath_program_raster[
        page->toolpath_program_raster_active];
    buffer_area = draw_ctx->buf_area;
    clip = draw_ctx->clip_area;
    buffer = (lv_color_t *)draw_ctx->buf;
    x1 = clip->x1 > buffer_area->x1 ? clip->x1 : buffer_area->x1;
    y1 = clip->y1 > buffer_area->y1 ? clip->y1 : buffer_area->y1;
    x2 = clip->x2 < buffer_area->x2 ? clip->x2 : buffer_area->x2;
    y2 = clip->y2 < buffer_area->y2 ? clip->y2 : buffer_area->y2;
    if (x1 < x_offset) x1 = x_offset;
    if (y1 < y_offset) y1 = y_offset;
    if (x2 >= x_offset + V5_TOOLPATH_VIEWPORT_WIDTH) {
        x2 = x_offset + V5_TOOLPATH_VIEWPORT_WIDTH - 1;
    }
    if (y2 >= y_offset + V5_TOOLPATH_VIEWPORT_HEIGHT) {
        y2 = y_offset + V5_TOOLPATH_VIEWPORT_HEIGHT - 1;
    }
    if (x1 > x2 || y1 > y2) return;
    stride = lv_area_get_width(buffer_area);
    for (y = y1; y <= y2; ++y) {
        const size_t local_y = (size_t)(y - y_offset);
        const size_t row_start =
            local_y * (size_t)V5_TOOLPATH_VIEWPORT_WIDTH;
        const size_t first_bit = row_start + (size_t)(x1 - x_offset);
        const size_t last_bit = row_start + (size_t)(x2 - x_offset);
        const size_t first_word = first_bit >> 6;
        const size_t last_word = last_bit >> 6;
        size_t word;
        for (word = first_word; word <= last_word; ++word) {
            uint64_t bits = raster[word];
            if (word == first_word) {
                bits &= ~0ULL << (first_bit & 63U);
            }
            if (word == last_word && (last_bit & 63U) != 63U) {
                bits &= (1ULL << ((last_bit & 63U) + 1U)) - 1ULL;
            }
            while (bits != 0ULL) {
                const unsigned int offset = trailing_zeroes_u64(bits);
                const size_t bit = word * 64U + offset;
                const int x = x_offset + (int)(bit - row_start);
                buffer[(size_t)(y - buffer_area->y1) * (size_t)stride +
                    (size_t)(x - buffer_area->x1)] = color;
                bits &= bits - 1ULL;
            }
        }
    }
}
