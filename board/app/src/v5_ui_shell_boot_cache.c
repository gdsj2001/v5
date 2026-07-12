#include "v5_ui_shell_internal.h"

#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_touch_input.h"
#include "v5_ui_page_cache_registry.h"
#include "v5_v3_local_pages.h"

#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define V5_UI_CACHE_PREP_YIELD_US 5000U

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
    if (!v5_ui_page_cache_registry_validate(
            entries,
            sizeof(entries) / sizeof(entries[0]),
            (unsigned int)V5_SHELL_PAGE_COUNT,
            V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT,
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
    V5UiPageCacheQueueEvidence *evidence)
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

int shell_run_boot_page_cache_queue(
    lv_obj_t *screen,
    int remote_display,
    V5UiPageCacheQueueEvidence evidence[V5_SHELL_PAGE_COUNT],
    unsigned long long *peak_cpu_pct_x100)
{
    size_t bad_index = 0U;
    unsigned int page_index;
    const unsigned int total = (unsigned int)(
        sizeof(g_v5_shell_page_cache_registry) /
        sizeof(g_v5_shell_page_cache_registry[0]));

    if (!screen || !evidence || !peak_cpu_pct_x100) {
        return 0;
    }
    memset(evidence, 0, sizeof(*evidence) * V5_SHELL_PAGE_COUNT);
    *peak_cpu_pct_x100 = 0ULL;
    if (remote_display) {
        if (!v5_lvgl_remote_display_output_suppressed()) {
            fprintf(stderr,
                    "V5_UI_CACHE_QUEUE event=fail worker_id=%u stage=queue_start reason=output_not_suppressed\n",
                    V5_UI_CACHE_BOOT_WORKER_ID);
            return 0;
        }
        printf(
            "V5_UI_CACHE_QUEUE event=start worker_id=%u worker_count=1 mode=caller_thread total=%u output_suppressed=1\n",
            V5_UI_CACHE_BOOT_WORKER_ID,
            total);
        fflush(stdout);
    }

    for (page_index = 0U; page_index < total; ++page_index) {
        const V5ShellPageCacheDescriptor *descriptor =
            &g_v5_shell_page_cache_registry[page_index];
        V5UiPageCacheQueueEvidence *item = &evidence[page_index];
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

        item->sequence = page_index;
        item->page = (unsigned int)descriptor->page;
        item->slot = descriptor->slot;
        item->worker_id = V5_UI_CACHE_BOOT_WORKER_ID;
        if ((unsigned int)descriptor->page >= (unsigned int)V5_SHELL_PAGE_COUNT ||
            descriptor->slot >= V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT ||
            !descriptor->name || !descriptor->name[0] || !descriptor->create) {
            fprintf(stderr,
                    "V5_UI_CACHE_QUEUE event=fail worker_id=%u sequence=%u stage=descriptor page=%u slot=%u\n",
                    V5_UI_CACHE_BOOT_WORKER_ID,
                    page_index,
                    (unsigned int)descriptor->page,
                    descriptor->slot);
            return 0;
        }
        if (remote_display) {
            printf(
                "V5_UI_CACHE_QUEUE event=page_start worker_id=%u sequence=%u page=%s slot=%u\n",
                V5_UI_CACHE_BOOT_WORKER_ID,
                page_index,
                descriptor->name,
                descriptor->slot);
            fflush(stdout);
        }
        page_started = shell_monotonic_ns();
        cpu_started = clock();
        root = descriptor->create(screen);
        create_finished = shell_monotonic_ns();
        item->create_ok = root ? 1U : 0U;
        if (!root) {
            fprintf(stderr,
                    "V5_UI_CACHE_QUEUE event=fail worker_id=%u sequence=%u page=%s slot=%u stage=create\n",
                    V5_UI_CACHE_BOOT_WORKER_ID,
                    page_index,
                    descriptor->name,
                    descriptor->slot);
            return 0;
        }
        g_v5_shell_shell_pages[descriptor->page] = root;
        if (remote_display) {
            shell_mark_page_cache_dirty(descriptor->page);
            if (!shell_prepare_page_cache_with_evidence(descriptor->page, item)) {
                fprintf(stderr,
                        "V5_UI_CACHE_QUEUE event=fail worker_id=%u sequence=%u page=%s slot=%u stage=prepare apply_ok=%u render_ok=%u capture_ok=%u cache_valid=%u invalidation_clean=%u\n",
                        V5_UI_CACHE_BOOT_WORKER_ID,
                        page_index,
                        descriptor->name,
                        descriptor->slot,
                        item->apply_ok,
                        item->render_ok,
                        item->capture_ok,
                        item->cache_valid,
                        item->invalidation_clean);
                return 0;
            }
        }
        prepare_finished = shell_monotonic_ns();
        if (remote_display) {
            sched_yield();
            usleep(V5_UI_CACHE_PREP_YIELD_US);
            item->yielded = 1U;
        }
        elapsed_us = (shell_monotonic_ns() - page_started) / 1000ULL;
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
        if (cpu_pct_x100 > *peak_cpu_pct_x100) {
            *peak_cpu_pct_x100 = cpu_pct_x100;
        }
        if (remote_display) {
            printf(
                "V5_UI_CACHE_PREP page=%s completed=%u total=%u elapsed_us=%llu create_us=%llu prepare_us=%llu yield_us=%u cpu_pct_x100=%llu peak_cpu_pct_x100=%llu worker_id=%u cache_valid=%u invalidation_clean=%u budget_bytes=%lu\n",
                descriptor->name,
                page_index + 1U,
                total,
                elapsed_us,
                create_us,
                prepare_us,
                V5_UI_CACHE_PREP_YIELD_US,
                cpu_pct_x100,
                *peak_cpu_pct_x100,
                V5_UI_CACHE_BOOT_WORKER_ID,
                item->cache_valid,
                item->invalidation_clean,
                (unsigned long)v5_lvgl_remote_display_cache_budget_bytes());
            printf(
                "V5_UI_CACHE_QUEUE event=page_end worker_id=%u sequence=%u page=%s slot=%u create_ok=%u apply_ok=%u render_ok=%u capture_ok=%u cache_valid=%u invalidation_clean=%u yielded=%u\n",
                V5_UI_CACHE_BOOT_WORKER_ID,
                page_index,
                descriptor->name,
                descriptor->slot,
                item->create_ok,
                item->apply_ok,
                item->render_ok,
                item->capture_ok,
                item->cache_valid,
                item->invalidation_clean,
                item->yielded);
            fflush(stdout);
        }
    }

    if (remote_display) {
        if (!v5_ui_page_cache_queue_evidence_validate(
                evidence,
                V5_SHELL_PAGE_COUNT,
                V5_SHELL_PAGE_COUNT,
                V5_UI_CACHE_BOOT_WORKER_ID,
                &bad_index)) {
            fprintf(stderr,
                    "V5_UI_CACHE_QUEUE event=fail worker_id=%u stage=evidence bad_index=%lu\n",
                    V5_UI_CACHE_BOOT_WORKER_ID,
                    (unsigned long)bad_index);
            return 0;
        }
        printf(
            "V5_UI_CACHE_QUEUE event=end worker_id=%u worker_count=1 completed=%u total=%u evidence_valid=1 peak_cpu_pct_x100=%llu\n",
            V5_UI_CACHE_BOOT_WORKER_ID,
            total,
            total,
            *peak_cpu_pct_x100);
        fflush(stdout);
    }
    return 1;
}
