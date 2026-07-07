#ifndef V5_MICROKERNEL_MANIFEST_H
#define V5_MICROKERNEL_MANIFEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5MicrokernelManifestKind {
    V5_MICROKERNEL_MANIFEST_FILE = 1,
    V5_MICROKERNEL_MANIFEST_RUNTIME_OWNER = 2,
    V5_MICROKERNEL_MANIFEST_NATIVE_API = 3
} V5MicrokernelManifestKind;

typedef struct V5MicrokernelManifestEntry {
    const char *id;
    const char *relative_path;
    V5MicrokernelManifestKind kind;
    int preload_to_ram;
    const char *owner;
} V5MicrokernelManifestEntry;

const V5MicrokernelManifestEntry *v5_microkernel_manifest_entries(size_t *count);
size_t v5_microkernel_manifest_count(void);
int v5_microkernel_manifest_all_ram_resident(void);

#ifdef __cplusplus
}
#endif

#endif
