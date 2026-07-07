#ifndef V5_BOOT_CLOSURE_H
#define V5_BOOT_CLOSURE_H

#include "v5_parameter_table.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_BOOT_CLOSURE_PROJECT_ROOT_CAP 256U
#define V5_BOOT_CLOSURE_SOURCE_PATH_CAP 160U
#define V5_BOOT_CLOSURE_TEXT_CAP 65536U
#define V5_BOOT_CLOSURE_DEVICE_REGISTER_STATUS_CAP 4096U

typedef struct V5BootClosureResidentText {
    unsigned int loaded;
    unsigned int truncated;
    size_t size;
    char source_path[V5_BOOT_CLOSURE_SOURCE_PATH_CAP];
    char text[V5_BOOT_CLOSURE_TEXT_CAP];
} V5BootClosureResidentText;

typedef struct V5BootClosure {
    unsigned int abi_version;
    char project_root[V5_BOOT_CLOSURE_PROJECT_ROOT_CAP];
    size_t command_count;
    size_t drive_profile_count;
    size_t drive_profile_map_count;
    const V5ParameterField *parameter_owner_fields;
    size_t parameter_owner_count;
    size_t microkernel_manifest_count;
    size_t microkernel_manifest_file_count;
    size_t microkernel_manifest_file_loaded_count;
    size_t microkernel_runtime_owner_count;
    size_t native_gate_count;
    size_t native_readback_count;
    size_t resource_count;
    V5BootClosureResidentText runtime_ini;
    V5BootClosureResidentText runtime_hal;
    V5BootClosureResidentText ethercat_hal;
    V5BootClosureResidentText linuxcnc_parameter_file;
    V5BootClosureResidentText tool_table;
    V5BootClosureResidentText step_ip_contract;
    V5BootClosureResidentText self_parameter_table;
    V5BootClosureResidentText drive_parameter_table;
    V5BootClosureResidentText device_register_status;
} V5BootClosure;

void v5_boot_closure_load(V5BootClosure *closure, const char *project_root);

#ifdef __cplusplus
}
#endif

#endif
