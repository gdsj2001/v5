#ifndef V5_BOOT_CLOSURE_H
#define V5_BOOT_CLOSURE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5BootClosure {
    unsigned int abi_version;
    size_t command_count;
    size_t drive_profile_count;
    size_t drive_profile_map_count;
    size_t parameter_owner_count;
    size_t microkernel_manifest_count;
    size_t native_gate_count;
    size_t native_readback_count;
    size_t resource_count;
} V5BootClosure;

void v5_boot_closure_load(V5BootClosure *closure, const char *project_root);

#ifdef __cplusplus
}
#endif

#endif
