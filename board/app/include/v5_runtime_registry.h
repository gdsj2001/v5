#ifndef V5_RUNTIME_REGISTRY_H
#define V5_RUNTIME_REGISTRY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5RuntimeRegistry {
    size_t resource_count;
    size_t command_count;
    size_t drive_profile_count;
} V5RuntimeRegistry;

void v5_runtime_registry_init(V5RuntimeRegistry *registry);

#ifdef __cplusplus
}
#endif

#endif
