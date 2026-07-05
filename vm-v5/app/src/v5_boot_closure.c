#include "v5_boot_closure.h"

#include "v5_command_table.h"
#include "v5_drive_profile_snapshot.h"
#include "v5_microkernel_manifest.h"
#include "v5_native_gate_registry.h"
#include "v5_native_status_api.h"
#include "v5_parameter_table.h"
#include "v5_runtime_registry.h"

void v5_boot_closure_load(V5BootClosure *closure, const char *project_root)
{
    V5DriveProfileSnapshot drive_profiles;
    V5RuntimeRegistry registry;

    if (!closure) {
        return;
    }

    v5_drive_profile_snapshot_load(&drive_profiles, project_root);
    v5_runtime_registry_init(&registry);

    closure->abi_version = 1U;
    closure->command_count = v5_command_table_count();
    closure->drive_profile_count = drive_profiles.profile_count;
    closure->drive_profile_map_count = drive_profiles.map_file_count;
    closure->parameter_owner_count = v5_parameter_table_count();
    closure->microkernel_manifest_count = v5_microkernel_manifest_count();
    closure->native_gate_count = v5_native_gate_registry_count();
    closure->native_readback_count = v5_native_status_api_count();
    closure->resource_count = registry.resource_count;
}
