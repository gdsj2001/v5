#include "v5_ui_page_cache_registry.h"

#include <stdio.h>

static int registry_valid(V5UiPageCacheRegistryEntry entries[3])
{
    size_t bad_index = 99U;
    return v5_ui_page_cache_registry_validate(entries, 3U, 3U, 3U, &bad_index);
}

int main(void)
{
    int root_a;
    int root_b;
    int root_c;
    size_t bad_index = 99U;
    V5UiPageCacheQueueEvidence evidence[3] = {
        {0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U, 1U},
        {1U, 1U, 1U, 0U, 1U, 1U, 1U, 1U, 1U, 1U, 1U},
        {2U, 2U, 2U, 0U, 1U, 1U, 1U, 1U, 1U, 1U, 1U}
    };
    V5UiPageCacheRegistryEntry entries[3] = {
        {0U, 0U, "main", &root_a},
        {1U, 1U, "settings", &root_b},
        {2U, 2U, "tool", &root_c}
    };

    if (!registry_valid(entries)) {
        return 1;
    }
    entries[2].page = 1U;
    if (registry_valid(entries)) {
        return 2;
    }
    entries[2].page = 2U;
    entries[2].slot = 1U;
    if (registry_valid(entries)) {
        return 3;
    }
    entries[2].slot = 3U;
    if (registry_valid(entries)) {
        return 4;
    }
    entries[2].slot = 2U;
    entries[2].root = NULL;
    if (registry_valid(entries)) {
        return 5;
    }
    entries[2].root = &root_c;
    entries[2].name = "";
    if (registry_valid(entries)) {
        return 6;
    }
    if (v5_ui_page_cache_registry_validate(entries, 3U, 3U, 4U, NULL)) {
        return 7;
    }
    if (v5_ui_page_cache_affected_mask(2U, 0U, 1U, 0, 1, 1) != 0x3U) {
        return 8;
    }
    if (v5_ui_page_cache_affected_mask(0U, 0U, 1U, 0, 1, 0) != 0U) {
        return 9;
    }
    if (v5_ui_page_cache_affected_mask(1U, 0U, 1U, 0, 1, 1) != 0x1U) {
        return 10;
    }
    if (v5_ui_page_cache_affected_mask(0U, 0U, 1U, 1, 0, 0) != 0U) {
        return 11;
    }
    if (v5_ui_page_cache_affected_mask(0U, 0U, 1U, 1, 1, 0) != 0x1U) {
        return 16;
    }
    if (v5_ui_page_cache_affected_mask(1U, 0U, 1U, 1, 0, 1) != 0x2U) {
        return 17;
    }
    if (v5_ui_page_cache_affected_mask(2U, 0U, 1U, 1, 0, 0) != 0U) {
        return 18;
    }
    if (v5_ui_page_cache_projection_required(1, 1, 1)) {
        return 19;
    }
    if (!v5_ui_page_cache_projection_required(0, 1, 1)) {
        return 20;
    }
    if (!v5_ui_page_cache_projection_required(1, 0, 1)) {
        return 21;
    }
    if (!v5_ui_page_cache_projection_required(1, 1, 0)) {
        return 22;
    }
    if (!v5_ui_page_cache_queue_evidence_validate(evidence, 3U, 3U, 0U, &bad_index)) {
        return 12;
    }
    evidence[1].worker_id = 1U;
    if (v5_ui_page_cache_queue_evidence_validate(evidence, 3U, 3U, 0U, &bad_index) || bad_index != 1U) {
        return 13;
    }
    evidence[1].worker_id = 0U;
    evidence[1].yielded = 0U;
    if (v5_ui_page_cache_queue_evidence_validate(evidence, 3U, 3U, 0U, &bad_index) || bad_index != 1U) {
        return 14;
    }
    evidence[1].yielded = 1U;
    evidence[1].invalidation_clean = 0U;
    if (v5_ui_page_cache_queue_evidence_validate(evidence, 3U, 3U, 0U, &bad_index) || bad_index != 1U) {
        return 15;
    }

    puts("v5_ui_page_cache_registry_smoke PASS");
    return 0;
}
