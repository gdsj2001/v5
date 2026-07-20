#ifndef V5_UI_PAGE_CACHE_REGISTRY_H
#define V5_UI_PAGE_CACHE_REGISTRY_H

#include <stddef.h>

typedef struct V5UiPageCacheRegistryEntry {
    unsigned int page;
    unsigned int slot;
    const char *name;
    const void *root;
} V5UiPageCacheRegistryEntry;

typedef struct V5UiPageCacheEvidence {
    unsigned int page;
    unsigned int slot;
    unsigned int create_ok;
    unsigned int apply_ok;
    unsigned int render_ok;
    unsigned int capture_ok;
    unsigned int cache_valid;
    unsigned int invalidation_clean;
} V5UiPageCacheEvidence;

static inline int v5_ui_page_cache_projection_required(
    int projection_valid,
    int owner_same,
    int visible_payload_equal)
{
    return !projection_valid || !owner_same || !visible_payload_equal;
}

static inline int v5_ui_page_cache_registry_validate_required_roots(
    const V5UiPageCacheRegistryEntry *entries,
    size_t entry_count,
    unsigned int page_count,
    unsigned int cache_page_count,
    unsigned int required_root_mask,
    size_t *bad_index)
{
    const unsigned int bit_count = (unsigned int)(sizeof(required_root_mask) * 8U);
    const unsigned int valid_page_mask = page_count >= bit_count
        ? ~0U
        : ((1U << page_count) - 1U);
    unsigned int page;
    size_t i;
    size_t j;

    if (bad_index) {
        *bad_index = 0U;
    }
    if (!entries || entry_count != (size_t)page_count || page_count != cache_page_count ||
        (required_root_mask & ~valid_page_mask) != 0U) {
        return 0;
    }
    for (i = 0U; i < entry_count; ++i) {
        if (entries[i].page >= page_count || entries[i].slot >= cache_page_count ||
            !entries[i].name || !entries[i].name[0] ||
            ((required_root_mask & (1U << entries[i].page)) != 0U && !entries[i].root)) {
            if (bad_index) {
                *bad_index = i;
            }
            return 0;
        }
        for (j = 0U; j < i; ++j) {
            if (entries[j].page == entries[i].page || entries[j].slot == entries[i].slot) {
                if (bad_index) {
                    *bad_index = i;
                }
                return 0;
            }
        }
    }
    for (page = 0U; page < page_count; ++page) {
        int found = 0;
        for (i = 0U; i < entry_count; ++i) {
            if (entries[i].page == page) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (bad_index) {
                *bad_index = entry_count;
            }
            return 0;
        }
    }
    return 1;
}

static inline int v5_ui_page_cache_registry_validate(
    const V5UiPageCacheRegistryEntry *entries,
    size_t entry_count,
    unsigned int page_count,
    unsigned int cache_page_count,
    size_t *bad_index)
{
    const unsigned int bit_count = (unsigned int)(sizeof(unsigned int) * 8U);
    const unsigned int required_root_mask = page_count >= bit_count
        ? ~0U
        : ((1U << page_count) - 1U);
    return v5_ui_page_cache_registry_validate_required_roots(
        entries,
        entry_count,
        page_count,
        cache_page_count,
        required_root_mask,
        bad_index);
}

static inline unsigned int v5_ui_page_cache_affected_mask(
    unsigned int current_page,
    unsigned int main_page,
    unsigned int settings_page,
    int overlay_active,
    int main_changed,
    int settings_changed)
{
    unsigned int mask = 0U;
    if (main_changed && (overlay_active || current_page != main_page) &&
        main_page < sizeof(mask) * 8U) {
        mask |= 1U << main_page;
    }
    if (settings_changed && (overlay_active || current_page != settings_page) &&
        settings_page < sizeof(mask) * 8U) {
        mask |= 1U << settings_page;
    }
    return mask;
}

static inline int v5_ui_page_cache_invalidate_now(
    unsigned int current_page,
    unsigned int changed_page,
    int changed_page_visible)
{
    return current_page == changed_page && changed_page_visible;
}

static inline int v5_ui_structure_event_pending(
    int signature_valid,
    unsigned int consumed_view_generation,
    unsigned int current_view_generation,
    int consumed_program_present,
    int current_program_present,
    unsigned int consumed_program_epoch,
    unsigned int current_program_epoch,
    int current_model_ready)
{
    if (current_program_present && !current_model_ready) {
        return 0;
    }
    return !signature_valid ||
        consumed_view_generation != current_view_generation ||
        consumed_program_present != current_program_present ||
        consumed_program_epoch != current_program_epoch;
}

#endif
