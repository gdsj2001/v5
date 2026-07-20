#include "v5_ui_shell_internal.h"

#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_touch_input.h"
#include "v5_ui_page_cache_registry.h"
#include "v5_v3_local_pages.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

typedef lv_obj_t *(*V5ShellPageCreateFn)(lv_obj_t *screen);

typedef struct V5ShellPageCacheDescriptor {
    V5ShellPageKind page;
    unsigned int slot;
    const char *name;
    V5ShellPageCreateFn create;
} V5ShellPageCacheDescriptor;

static lv_obj_t *shell_create_boot_main_page(lv_obj_t *screen)
{
    if (!v5_main_page_create(&g_v5_shell_main_page, screen)) {
        return NULL;
    }
    v5_lvgl_touch_input_set_points_callback(shell_toolpath_touch_points, NULL);
    v5_main_page_bind_program_controller(&g_v5_shell_main_page, &g_v5_shell_program_controller);
    v5_main_page_set_command_execution_enabled(&g_v5_shell_main_page, 0);
    v5_main_page_set_navigation_callback(&g_v5_shell_main_page, shell_navigate, NULL);
    v5_main_page_set_native_readback_refresh_callback(
        &g_v5_shell_main_page,
        shell_refresh_native_readback_for_action,
        NULL);
    (void)shell_refresh_native_readback(1);
    (void)shell_refresh_safety_readback(1);
    return g_v5_shell_main_page.root;
}

static lv_obj_t *shell_create_boot_settings_page(lv_obj_t *screen)
{
    if (!v5_settings_page_create(&g_v5_shell_settings_page, screen)) {
        return NULL;
    }
    v5_settings_page_set_navigation_callback(&g_v5_shell_settings_page, shell_navigate, NULL);
    return g_v5_shell_settings_page.root;
}

static lv_obj_t *shell_create_boot_tool_page(lv_obj_t *screen)
{
    return v5_v3_local_page_create_tool(screen, shell_return_button_cb);
}

static lv_obj_t *shell_create_boot_probe_page(lv_obj_t *screen)
{
    return shell_create_aux_page(screen, "探测");
}

static lv_obj_t *shell_create_boot_offset_page(lv_obj_t *screen)
{
    return v5_v3_local_page_create_offset(screen, shell_return_button_cb);
}

static lv_obj_t *shell_create_boot_io_page(lv_obj_t *screen)
{
    return shell_create_aux_page(screen, "输入输出设置");
}

static lv_obj_t *shell_create_boot_network_page(lv_obj_t *screen)
{
    return shell_create_network_page(screen);
}

static lv_obj_t *shell_create_boot_program_page(lv_obj_t *screen)
{
    return shell_create_program_page(screen);
}

static lv_obj_t *shell_create_boot_mdi_page(lv_obj_t *screen)
{
    return shell_create_mdi_page(screen);
}

static const V5ShellPageCacheDescriptor g_v5_shell_page_cache_registry[] = {
    {V5_SHELL_PAGE_MAIN, V5_REMOTE_DISPLAY_CACHE_MAIN, "main", shell_create_boot_main_page},
    {V5_SHELL_PAGE_SETTINGS, V5_REMOTE_DISPLAY_CACHE_SETTINGS, "settings", shell_create_boot_settings_page},
    {V5_SHELL_PAGE_TOOL, V5_REMOTE_DISPLAY_CACHE_TOOL, "tool", shell_create_boot_tool_page},
    {V5_SHELL_PAGE_PROBE, V5_REMOTE_DISPLAY_CACHE_PROBE, "probe", shell_create_boot_probe_page},
    {V5_SHELL_PAGE_OFFSET, V5_REMOTE_DISPLAY_CACHE_OFFSET, "offset", shell_create_boot_offset_page},
    {V5_SHELL_PAGE_IO, V5_REMOTE_DISPLAY_CACHE_IO, "io", shell_create_boot_io_page},
    {V5_SHELL_PAGE_NETWORK, V5_REMOTE_DISPLAY_CACHE_NETWORK, "network", shell_create_boot_network_page},
    {V5_SHELL_PAGE_PROGRAM, V5_REMOTE_DISPLAY_CACHE_PROGRAM, "program", shell_create_boot_program_page},
    {V5_SHELL_PAGE_MDI, V5_REMOTE_DISPLAY_CACHE_MDI, "mdi", shell_create_boot_mdi_page}
};

_Static_assert(
    sizeof(g_v5_shell_page_cache_registry) / sizeof(g_v5_shell_page_cache_registry[0]) ==
        V5_SHELL_PAGE_COUNT,
    "page cache registry must contain every page exactly once");
_Static_assert(
    V5_SHELL_PAGE_COUNT == V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT,
    "every shell page requires one non-overlay cache slot");
_Static_assert(
    V5_SHELL_PAGE_COUNT <= sizeof(unsigned int) * 8U,
    "page dirty mask must represent every shell page");

static const V5ShellPageCacheDescriptor *shell_page_cache_descriptor(
    V5ShellPageKind page)
{
    size_t i;
    for (i = 0U; i < sizeof(g_v5_shell_page_cache_registry) /
                            sizeof(g_v5_shell_page_cache_registry[0]); ++i) {
        if (g_v5_shell_page_cache_registry[i].page == page) {
            return &g_v5_shell_page_cache_registry[i];
        }
    }
    return NULL;
}

int shell_create_page_if_needed(V5ShellPageKind page, lv_obj_t *screen)
{
    const V5ShellPageCacheDescriptor *descriptor;
    lv_obj_t *root;
    if (!screen || (unsigned int)page >= (unsigned int)V5_SHELL_PAGE_COUNT) {
        return 0;
    }
    if (g_v5_shell_shell_pages[page]) {
        return 1;
    }
    descriptor = shell_page_cache_descriptor(page);
    if (!descriptor || !descriptor->create) {
        return 0;
    }
    root = descriptor->create(screen);
    if (!root) {
        return 0;
    }
    g_v5_shell_shell_pages[page] = root;
    if (g_v5_shell_remote_display_active) {
        shell_mark_page_cache_dirty(page);
    }
    return 1;
}

const char *shell_page_name(V5ShellPageKind page)
{
    size_t i;
    for (i = 0U; i < sizeof(g_v5_shell_page_cache_registry) /
                            sizeof(g_v5_shell_page_cache_registry[0]); ++i) {
        if (g_v5_shell_page_cache_registry[i].page == page) {
            return g_v5_shell_page_cache_registry[i].name;
        }
    }
    return "invalid";
}

unsigned int shell_page_cache_slot(V5ShellPageKind page)
{
    size_t i;
    for (i = 0U; i < sizeof(g_v5_shell_page_cache_registry) /
                            sizeof(g_v5_shell_page_cache_registry[0]); ++i) {
        if (g_v5_shell_page_cache_registry[i].page == page) {
            return g_v5_shell_page_cache_registry[i].slot;
        }
    }
    return V5_REMOTE_DISPLAY_CACHE_COUNT;
}

int shell_boot_page_cache_registry_validate(void)
{
    V5UiPageCacheRegistryEntry entries[V5_SHELL_PAGE_COUNT];
    size_t bad_index = 0U;
    size_t i;

    for (i = 0U; i < sizeof(g_v5_shell_page_cache_registry) /
                            sizeof(g_v5_shell_page_cache_registry[0]); ++i) {
        V5ShellPageKind page = g_v5_shell_page_cache_registry[i].page;
        if (!g_v5_shell_page_cache_registry[i].create) {
            fprintf(stderr,
                    "V5_UI_CACHE_REGISTRY_FAIL index=%lu reason=missing_create\n",
                    (unsigned long)i);
            return 0;
        }
        entries[i].page = (unsigned int)page;
        entries[i].slot = g_v5_shell_page_cache_registry[i].slot;
        entries[i].name = g_v5_shell_page_cache_registry[i].name;
        entries[i].root = (unsigned int)page < (unsigned int)V5_SHELL_PAGE_COUNT
            ? g_v5_shell_shell_pages[page]
            : NULL;
    }
    if (!v5_ui_page_cache_registry_validate_required_roots(
            entries,
            sizeof(entries) / sizeof(entries[0]),
            (unsigned int)V5_SHELL_PAGE_COUNT,
            V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT,
            1U << (unsigned int)V5_SHELL_PAGE_MAIN,
            &bad_index)) {
        if (bad_index < sizeof(entries) / sizeof(entries[0])) {
            fprintf(stderr,
                    "V5_UI_CACHE_REGISTRY_FAIL index=%lu page=%u slot=%u name=%s root=%p\n",
                    (unsigned long)bad_index,
                    entries[bad_index].page,
                    entries[bad_index].slot,
                    entries[bad_index].name ? entries[bad_index].name : "",
                    entries[bad_index].root);
        } else {
            fprintf(stderr, "V5_UI_CACHE_REGISTRY_FAIL missing_page_or_count_mismatch\n");
        }
        return 0;
    }
    return 1;
}

static int shell_page_layout_complete(lv_obj_t *root)
{
    lv_area_t area;
    if (!root || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }
    lv_obj_get_coords(root, &area);
    return area.x1 == 0 && area.y1 == 0 &&
        lv_area_get_width(&area) == 1024 &&
        lv_area_get_height(&area) == 600;
}

static int shell_prepare_page_cache_with_evidence(
    V5ShellPageKind page,
    V5UiPageCacheEvidence *evidence)
{
    int previous_suppressed;
    int ok;
    unsigned int slot = shell_page_cache_slot(page);
    lv_obj_t *root;
    lv_disp_t *disp;
    if (!g_v5_shell_remote_display_active ||
        (unsigned int)page >= (unsigned int)V5_SHELL_PAGE_COUNT ||
        slot >= V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT ||
        !g_v5_shell_shell_pages[page]) {
        return 0;
    }
    root = g_v5_shell_shell_pages[page];
    previous_suppressed = v5_lvgl_remote_display_set_output_suppressed(1);
    shell_show_page_objects(page);
    if (!shell_apply_page_resident_model(page)) {
        (void)v5_lvgl_remote_display_set_output_suppressed(previous_suppressed);
        return 0;
    }
    if (evidence) {
        evidence->apply_ok = 1U;
    }
    lv_obj_update_layout(root);
    lv_obj_invalidate(root);
    if (g_v5_shell_top_status_layer &&
        !lv_obj_has_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_invalidate(g_v5_shell_top_status_layer);
    }
    lv_refr_now(NULL);
    disp = lv_obj_get_disp(root);
    if (evidence) {
        evidence->render_ok = shell_page_layout_complete(root) &&
            disp && !disp->rendering_in_progress ? 1U : 0U;
        evidence->invalidation_clean = disp && disp->inv_p == 0U ? 1U : 0U;
    }
    if (!shell_page_layout_complete(root) || !disp ||
        disp->rendering_in_progress || disp->inv_p != 0U) {
        (void)v5_lvgl_remote_display_set_output_suppressed(previous_suppressed);
        return 0;
    }
    ok = v5_lvgl_remote_display_cache_capture(slot);
    if (evidence) {
        evidence->capture_ok = ok ? 1U : 0U;
        evidence->cache_valid = v5_lvgl_remote_display_cache_valid(slot) ? 1U : 0U;
        evidence->invalidation_clean = disp && !disp->rendering_in_progress &&
            disp->inv_p == 0U ? 1U : 0U;
    }
    (void)v5_lvgl_remote_display_set_output_suppressed(previous_suppressed);
    if (ok && v5_lvgl_remote_display_cache_valid(slot) &&
        disp && !disp->rendering_in_progress && disp->inv_p == 0U) {
        g_v5_shell_page_cache_dirty[page] = 0;
        return 1;
    }
    return 0;
}

int shell_prepare_page_cache(V5ShellPageKind page)
{
    return shell_prepare_page_cache_with_evidence(page, NULL);
}

int shell_prepare_boot_main_cache(
    lv_obj_t *screen,
    int remote_display,
    V5UiPageCacheEvidence *evidence,
    unsigned long long *peak_cpu_pct_x100)
{
    const V5ShellPageCacheDescriptor *descriptor =
        shell_page_cache_descriptor(V5_SHELL_PAGE_MAIN);
    unsigned long long page_started;
    unsigned long long create_finished;
    unsigned long long prepare_finished;
    unsigned long long elapsed_us;
    unsigned long long create_us;
    unsigned long long prepare_us;
    unsigned long long cpu_us = 0ULL;
    unsigned long long cpu_pct_x100 = 0ULL;
    clock_t cpu_started;
    lv_obj_t *root;

    if (!screen || !descriptor || !evidence || !peak_cpu_pct_x100) {
        return 0;
    }
    memset(evidence, 0, sizeof(*evidence));
    *peak_cpu_pct_x100 = 0ULL;
    evidence->page = (unsigned int)descriptor->page;
    evidence->slot = descriptor->slot;

    if (remote_display) {
        if (!v5_lvgl_remote_display_output_suppressed()) {
            fprintf(stderr,
                    "V5_UI_MAIN_CACHE event=fail stage=start reason=output_not_suppressed\n");
            return 0;
        }
        printf(
            "V5_UI_MAIN_CACHE event=start page=%s slot=%u output_suppressed=1\n",
            descriptor->name,
            descriptor->slot);
        fflush(stdout);
    }

    page_started = shell_monotonic_ns();
    cpu_started = clock();
    if (!shell_create_page_if_needed(descriptor->page, screen)) {
        fprintf(stderr,
                "V5_UI_MAIN_CACHE event=fail stage=create page=%s slot=%u\n",
                descriptor->name,
                descriptor->slot);
        return 0;
    }
    root = g_v5_shell_shell_pages[descriptor->page];
    create_finished = shell_monotonic_ns();
    evidence->create_ok = root ? 1U : 0U;

    if (remote_display) {
        if (!shell_prepare_page_cache_with_evidence(descriptor->page, evidence)) {
            fprintf(stderr,
                    "V5_UI_MAIN_CACHE event=fail stage=prepare page=%s slot=%u "
                    "apply_ok=%u render_ok=%u capture_ok=%u cache_valid=%u "
                    "invalidation_clean=%u\n",
                    descriptor->name,
                    descriptor->slot,
                    evidence->apply_ok,
                    evidence->render_ok,
                    evidence->capture_ok,
                    evidence->cache_valid,
                    evidence->invalidation_clean);
            return 0;
        }
    }
    prepare_finished = shell_monotonic_ns();
    elapsed_us = (prepare_finished - page_started) / 1000ULL;
    create_us = (create_finished - page_started) / 1000ULL;
    prepare_us = (prepare_finished - create_finished) / 1000ULL;
    if (cpu_started != (clock_t)-1) {
        clock_t cpu_finished = clock();
        if (cpu_finished != (clock_t)-1 && cpu_finished >= cpu_started) {
            cpu_us = (unsigned long long)(cpu_finished - cpu_started) * 1000000ULL /
                (unsigned long long)CLOCKS_PER_SEC;
        }
    }
    if (elapsed_us > 0ULL) {
        cpu_pct_x100 = cpu_us * 10000ULL / elapsed_us;
    }
    *peak_cpu_pct_x100 = cpu_pct_x100;

    if (remote_display) {
        printf(
            "V5_UI_MAIN_CACHE event=end page=%s slot=%u create_ok=%u apply_ok=%u "
            "render_ok=%u capture_ok=%u cache_valid=%u invalidation_clean=%u "
            "elapsed_us=%llu create_us=%llu prepare_us=%llu cpu_pct_x100=%llu "
            "budget_bytes=%lu\n",
            descriptor->name,
            descriptor->slot,
            evidence->create_ok,
            evidence->apply_ok,
            evidence->render_ok,
            evidence->capture_ok,
            evidence->cache_valid,
            evidence->invalidation_clean,
            elapsed_us,
            create_us,
            prepare_us,
            cpu_pct_x100,
            (unsigned long)v5_lvgl_remote_display_cache_budget_bytes());
        fflush(stdout);
        return evidence->create_ok && evidence->apply_ok &&
            evidence->render_ok && evidence->capture_ok &&
            evidence->cache_valid && evidence->invalidation_clean;
    }
    return evidence->create_ok;
}
