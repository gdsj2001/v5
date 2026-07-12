#ifndef V5_UI_PAGE_CACHE_REGISTRY_H
#define V5_UI_PAGE_CACHE_REGISTRY_H

#include <stddef.h>

typedef struct V5UiPageCacheRegistryEntry {
    unsigned int page;
    unsigned int slot;
    const char *name;
    const void *root;
} V5UiPageCacheRegistryEntry;

typedef struct V5UiPageCacheQueueEvidence {
    unsigned int sequence;
    unsigned int page;
    unsigned int slot;
    unsigned int worker_id;
    unsigned int create_ok;
    unsigned int apply_ok;
    unsigned int render_ok;
    unsigned int capture_ok;
    unsigned int cache_valid;
    unsigned int invalidation_clean;
    unsigned int yielded;
} V5UiPageCacheQueueEvidence;

static inline int v5_ui_page_cache_projection_required(
    int projection_valid,
    int owner_same,
    int visible_payload_equal)
{
    return !projection_valid || !owner_same || !visible_payload_equal;
}

static inline int v5_ui_page_cache_registry_validate(
    const V5UiPageCacheRegistryEntry *entries,
    size_t entry_count,
    unsigned int page_count,
    unsigned int cache_page_count,
    size_t *bad_index)
{
    unsigned int page;
    size_t i;
    size_t j;

    if (bad_index) {
        *bad_index = 0U;
    }
    if (!entries || entry_count != (size_t)page_count || page_count != cache_page_count) {
        return 0;
    }
    for (i = 0U; i < entry_count; ++i) {
        if (entries[i].page >= page_count || entries[i].slot >= cache_page_count ||
            !entries[i].name || !entries[i].name[0] || !entries[i].root) {
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

static inline int v5_ui_page_cache_queue_evidence_validate(
    const V5UiPageCacheQueueEvidence *entries,
    size_t entry_count,
    unsigned int page_count,
    unsigned int worker_id,
    size_t *bad_index)
{
    size_t i;
    if (bad_index) {
        *bad_index = 0U;
    }
    if (!entries || entry_count != (size_t)page_count) {
        return 0;
    }
    for (i = 0U; i < entry_count; ++i) {
        if (entries[i].sequence != (unsigned int)i ||
            entries[i].page != (unsigned int)i ||
            entries[i].worker_id != worker_id ||
            !entries[i].create_ok ||
            !entries[i].apply_ok ||
            !entries[i].render_ok ||
            !entries[i].capture_ok ||
            !entries[i].cache_valid ||
            !entries[i].invalidation_clean ||
            !entries[i].yielded) {
            if (bad_index) {
                *bad_index = i;
            }
            return 0;
        }
    }
    return 1;
}

#endif
