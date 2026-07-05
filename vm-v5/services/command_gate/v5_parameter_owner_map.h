#ifndef V5_PARAMETER_OWNER_MAP_H
#define V5_PARAMETER_OWNER_MAP_H

#include "v5_parameter_table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5ParameterOwnerRecord {
    const V5ParameterField *field;
    const char *write_owner;
    const char *readback_owner;
} V5ParameterOwnerRecord;

int v5_parameter_owner_lookup(const char *field_name, V5ParameterOwnerRecord *record);
int v5_parameter_owner_can_write(const char *field_name);
int v5_parameter_owner_requires_restart(const char *field_name);

#ifdef __cplusplus
}
#endif

#endif
