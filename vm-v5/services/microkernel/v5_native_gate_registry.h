#ifndef V5_NATIVE_GATE_REGISTRY_H
#define V5_NATIVE_GATE_REGISTRY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5NativeGateKind {
    V5_NATIVE_GATE_RUN_CONTROL = 1,
    V5_NATIVE_GATE_COORDINATE_SYSTEM = 2,
    V5_NATIVE_GATE_GEOMETRY = 3,
    V5_NATIVE_GATE_SAFETY = 4,
    V5_NATIVE_GATE_OVERRIDE = 5,
    V5_NATIVE_GATE_PARAMETER_APPLY = 6
} V5NativeGateKind;

typedef struct V5NativeGateEntry {
    const char *id;
    V5NativeGateKind kind;
    const char *owner;
    const char *command_path;
} V5NativeGateEntry;

const V5NativeGateEntry *v5_native_gate_registry_entries(size_t *count);
size_t v5_native_gate_registry_count(void);
const V5NativeGateEntry *v5_native_gate_registry_find(const char *id);

#ifdef __cplusplus
}
#endif

#endif
