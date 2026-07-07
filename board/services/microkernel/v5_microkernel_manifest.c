#include "v5_microkernel_manifest.h"

static const V5MicrokernelManifestEntry k_manifest[] = {
    {"linuxcnc_ini", "linuxcnc/ini/v5_bus.ini", V5_MICROKERNEL_MANIFEST_FILE, 1, "microkernel"},
    {"linuxcnc_hal", "linuxcnc/hal/v5_bus_2ms.hal", V5_MICROKERNEL_MANIFEST_FILE, 1, "microkernel"},
    {"ethercat_hal_config", "linuxcnc/hal/ethercat-conf-2ms.xml", V5_MICROKERNEL_MANIFEST_FILE, 1, "microkernel"},
    {"step_ip_contract", "linuxcnc/components/step_ip_v1_5.contract.json", V5_MICROKERNEL_MANIFEST_FILE, 1, "microkernel"},
    {"linuxcnc_executable", "linuxcnc/runtime/linuxcnc", V5_MICROKERNEL_MANIFEST_RUNTIME_OWNER, 1, "microkernel"},
    {"rtapi_modules", "linuxcnc/runtime/rtapi_modules", V5_MICROKERNEL_MANIFEST_RUNTIME_OWNER, 1, "microkernel"},
    {"kinematics_component", "linuxcnc/runtime/kinematics", V5_MICROKERNEL_MANIFEST_RUNTIME_OWNER, 1, "microkernel"},
    {"parameter_file", "linuxcnc/runtime/var/linuxcnc.var", V5_MICROKERNEL_MANIFEST_FILE, 1, "microkernel"},
    {"tool_table", "linuxcnc/runtime/var/tool.tbl", V5_MICROKERNEL_MANIFEST_FILE, 1, "microkernel"},
    {"native_gate_registry", "services/microkernel/v5_native_gate_registry.c", V5_MICROKERNEL_MANIFEST_NATIVE_API, 1, "native_gate"},
    {"native_status_api", "services/microkernel/v5_native_status_api.c", V5_MICROKERNEL_MANIFEST_NATIVE_API, 1, "native_status"},
};

const V5MicrokernelManifestEntry *v5_microkernel_manifest_entries(size_t *count)
{
    if (count) {
        *count = sizeof(k_manifest) / sizeof(k_manifest[0]);
    }
    return k_manifest;
}

size_t v5_microkernel_manifest_count(void)
{
    return sizeof(k_manifest) / sizeof(k_manifest[0]);
}

int v5_microkernel_manifest_all_ram_resident(void)
{
    size_t i;
    for (i = 0; i < v5_microkernel_manifest_count(); ++i) {
        if (!k_manifest[i].preload_to_ram) {
            return 0;
        }
    }
    return 1;
}
