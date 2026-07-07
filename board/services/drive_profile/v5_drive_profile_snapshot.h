#ifndef V5_DRIVE_PROFILE_SNAPSHOT_H
#define V5_DRIVE_PROFILE_SNAPSHOT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5DriveProfileSnapshot {
    const char *source;
    int loaded;
    size_t profile_count;
    size_t map_file_count;
} V5DriveProfileSnapshot;

void v5_drive_profile_snapshot_init(V5DriveProfileSnapshot *snapshot);
void v5_drive_profile_snapshot_load(V5DriveProfileSnapshot *snapshot, const char *project_root);
size_t v5_drive_profile_snapshot_count(void);
size_t v5_drive_profile_snapshot_map_file_count(void);

#ifdef __cplusplus
}
#endif

#endif
