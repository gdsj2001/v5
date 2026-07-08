#include "v5_microkernel_manifest.h"
#include "v5_native_gate_registry.h"
#include "v5_native_status_api.h"

#include <stdio.h>
#include <string.h>

static int require_gate(const char *id)
{
    if (!v5_native_gate_registry_find(id)) {
        fprintf(stderr, "missing native gate: %s\n", id);
        return 1;
    }
    return 0;
}

static int require_manifest(const char *id)
{
    const V5MicrokernelManifestEntry *entries;
    size_t count;
    size_t i;
    entries = v5_microkernel_manifest_entries(&count);
    for (i = 0; i < count; ++i) {
        if (entries[i].id && strcmp(entries[i].id, id) == 0) {
            return 0;
        }
    }
    fprintf(stderr, "missing microkernel manifest entry: %s\n", id);
    return 1;
}

static int require_status(const char *id)
{
    if (!v5_native_status_api_find(id)) {
        fprintf(stderr, "missing native status: %s\n", id);
        return 1;
    }
    return 0;
}

int main(void)
{
    int rc = 0;
    size_t manifest_count = v5_microkernel_manifest_count();
    size_t gate_count = v5_native_gate_registry_count();
    size_t status_count = v5_native_status_api_count();

    if (manifest_count == 0 || gate_count == 0 || status_count == 0) {
        fprintf(stderr, "microkernel registry is empty\n");
        return 1;
    }
    if (!v5_microkernel_manifest_all_ram_resident()) {
        fprintf(stderr, "microkernel manifest contains non-RAM item\n");
        return 1;
    }
    if (v5_native_status_api_has_shm_exports()) {
        fprintf(stderr, "native actual readback leaked into SHM export list\n");
        return 1;
    }

    rc |= require_gate("program_start");
    rc |= require_gate("wcs_select");
    rc |= require_gate("work_zero");
    rc |= require_gate("g92_clear");
    rc |= require_gate("rtcp_set");
    rc |= require_gate("estop_force");
    rc |= require_gate("feed_override_set");
    rc |= require_gate("settings_apply");
    rc |= require_manifest("g53_geometry_parameter_memory");
    rc |= require_manifest("native_safety_latch_owner");
    rc |= require_status("active_wcs");
    rc |= require_status("wcs_offsets");
    rc |= require_status("g92_offset");
    rc |= require_status("rtcp_actual");
    rc |= require_status("g53_geometry");
    rc |= require_status("rotary_logical_abs_counts64");
    rc |= require_status("rotary_runtime_window_counts");
    rc |= require_status("safety_estop");
    rc |= require_status("motion_owner_state");
    if (rc) {
        return 1;
    }

    printf(
        "v5 microkernel manifest: files=%zu gates=%zu readbacks=%zu shm_exports=0\n",
        manifest_count,
        gate_count,
        status_count);
    return 0;
}
