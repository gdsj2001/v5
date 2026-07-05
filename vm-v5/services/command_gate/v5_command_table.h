#ifndef V5_COMMAND_TABLE_H
#define V5_COMMAND_TABLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5CommandEntry {
    const char *name;
    const char *owner;
} V5CommandEntry;

const V5CommandEntry *v5_command_table_entries(size_t *count);
size_t v5_command_table_count(void);

#ifdef __cplusplus
}
#endif

#endif
