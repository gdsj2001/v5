#include "v5_boot_closure.h"

#include "v5_command_table.h"
#include "v5_drive_profile_snapshot.h"
#include "v5_microkernel_manifest.h"
#include "v5_native_gate_registry.h"
#include "v5_native_status_api.h"
#include "v5_runtime_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int boot_closure_path(char *out, size_t cap, const char *project_root, const char *rel)
{
    int n;
    const char *root = (project_root && project_root[0]) ? project_root : ".";
    if (!out || cap == 0U || !rel || !rel[0]) {
        return 0;
    }
    n = snprintf(out, cap, "%s/%s", root, rel);
    return n > 0 && (size_t)n < cap;
}

static void boot_closure_load_text(V5BootClosureResidentText *blob, const char *project_root, const char *rel)
{
    FILE *fp;
    char path[512];
    size_t n;

    if (!blob) {
        return;
    }
    memset(blob, 0, sizeof(*blob));
    if (rel) {
        snprintf(blob->source_path, sizeof(blob->source_path), "%s", rel);
    }
    if (!boot_closure_path(path, sizeof(path), project_root, rel)) {
        return;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return;
    }
    n = fread(blob->text, 1U, V5_BOOT_CLOSURE_TEXT_CAP - 1U, fp);
    blob->text[n] = '\0';
    blob->size = n;
    blob->loaded = 1U;
    if (!feof(fp)) {
        blob->truncated = 1U;
    }
    fclose(fp);
}

static void boot_closure_load_external_text(V5BootClosureResidentText *blob, const char *path)
{
    FILE *fp;
    size_t n;

    if (!blob) {
        return;
    }
    memset(blob, 0, sizeof(*blob));
    if (path) {
        snprintf(blob->source_path, sizeof(blob->source_path), "%s", path);
    }
    if (!path || !path[0]) {
        return;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return;
    }
    n = fread(blob->text, 1U, V5_BOOT_CLOSURE_DEVICE_REGISTER_STATUS_CAP - 1U, fp);
    blob->text[n] = '\0';
    blob->size = n;
    blob->loaded = 1U;
    if (!feof(fp)) {
        blob->truncated = 1U;
    }
    fclose(fp);
}

void v5_boot_closure_load(V5BootClosure *closure, const char *project_root)
{
    V5DriveProfileSnapshot drive_profiles;
    V5RuntimeRegistry registry;
    const V5MicrokernelManifestEntry *manifest;
    size_t manifest_count = 0U;
    size_t i;
    const char *device_status_path;
    const char *root = (project_root && project_root[0]) ? project_root : ".";

    if (!closure) {
        return;
    }
    memset(closure, 0, sizeof(*closure));

    v5_drive_profile_snapshot_load(&drive_profiles, project_root);
    v5_runtime_registry_init(&registry);

    closure->abi_version = 2U;
    snprintf(closure->project_root, sizeof(closure->project_root), "%s", root);
    closure->command_count = v5_command_table_count();
    closure->drive_profile_count = drive_profiles.profile_count;
    closure->drive_profile_map_count = drive_profiles.map_file_count;
    closure->parameter_owner_fields = v5_parameter_table_entries(&closure->parameter_owner_count);
    closure->microkernel_manifest_count = v5_microkernel_manifest_count();
    manifest = v5_microkernel_manifest_entries(&manifest_count);
    for (i = 0U; i < manifest_count; ++i) {
        if (manifest[i].kind == V5_MICROKERNEL_MANIFEST_FILE) {
            ++closure->microkernel_manifest_file_count;
        } else if (manifest[i].kind == V5_MICROKERNEL_MANIFEST_RUNTIME_OWNER) {
            ++closure->microkernel_runtime_owner_count;
        }
    }
    closure->native_gate_count = v5_native_gate_registry_count();
    closure->native_readback_count = v5_native_status_api_count();
    closure->resource_count = registry.resource_count;

    boot_closure_load_text(&closure->runtime_ini, root, "linuxcnc/ini/v5_bus.ini");
    boot_closure_load_text(&closure->runtime_hal, root, "linuxcnc/hal/v5_bus_2ms.hal");
    boot_closure_load_text(&closure->ethercat_hal, root, "linuxcnc/hal/ethercat-conf-2ms.xml");
    boot_closure_load_text(&closure->linuxcnc_parameter_file, root, "linuxcnc/runtime/var/linuxcnc.var");
    boot_closure_load_text(&closure->tool_table, root, "linuxcnc/runtime/var/tool.tbl");
    boot_closure_load_text(&closure->step_ip_contract, root, "linuxcnc/components/step_ip_v1_5.contract.json");
    boot_closure_load_text(&closure->self_parameter_table, root, "config/settings/self_parameter_table.tsv");
    boot_closure_load_text(&closure->drive_parameter_table, root, "config/settings/drive_parameter_table.tsv");
    if (closure->runtime_ini.loaded) ++closure->microkernel_manifest_file_loaded_count;
    if (closure->runtime_hal.loaded) ++closure->microkernel_manifest_file_loaded_count;
    if (closure->ethercat_hal.loaded) ++closure->microkernel_manifest_file_loaded_count;
    if (closure->linuxcnc_parameter_file.loaded) ++closure->microkernel_manifest_file_loaded_count;
    if (closure->tool_table.loaded) ++closure->microkernel_manifest_file_loaded_count;
    if (closure->step_ip_contract.loaded) ++closure->microkernel_manifest_file_loaded_count;
    device_status_path = getenv("V5_DEVICE_REGISTER_STATUS_JSON");
    if (!device_status_path || !device_status_path[0]) {
        device_status_path = "/opt/8ax/drive-profiles/device_register_status.json";
    }
    boot_closure_load_external_text(&closure->device_register_status, device_status_path);
}
