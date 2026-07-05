#ifndef V5_NATIVE_STATUS_API_H
#define V5_NATIVE_STATUS_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5NativeStatusOwner {
    V5_NATIVE_STATUS_LINUXCNC = 1,
    V5_NATIVE_STATUS_HAL = 2,
    V5_NATIVE_STATUS_KINEMATICS = 3,
    V5_NATIVE_STATUS_ROTARY_WINDOW = 4,
    V5_NATIVE_STATUS_SAFETY = 5,
    V5_NATIVE_STATUS_MOTION_OWNER = 6
} V5NativeStatusOwner;

typedef struct V5NativeStatusEntry {
    const char *id;
    V5NativeStatusOwner owner;
    const char *read_path;
    int export_to_shm;
} V5NativeStatusEntry;

const V5NativeStatusEntry *v5_native_status_api_entries(size_t *count);
size_t v5_native_status_api_count(void);
const V5NativeStatusEntry *v5_native_status_api_find(const char *id);
int v5_native_status_api_has_shm_exports(void);

#ifdef __cplusplus
}
#endif

#endif
