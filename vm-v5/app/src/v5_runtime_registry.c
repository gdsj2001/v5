#include "v5_runtime_registry.h"

#include "v5_command_table.h"
#include "v5_drive_profile_snapshot.h"

void v5_runtime_registry_init(V5RuntimeRegistry *registry)
{
    if (!registry) {
        return;
    }

    registry->resource_count = 3U;
    registry->command_count = v5_command_table_count();
    registry->drive_profile_count = v5_drive_profile_snapshot_count();
}
