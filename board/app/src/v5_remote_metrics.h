#ifndef V5_REMOTE_METRICS_H
#define V5_REMOTE_METRICS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void v5_remote_metrics_json(char *buffer, size_t size);
void v5_remote_metrics_display_text(char *cpu0, size_t cpu0_size, char *cpu1, size_t cpu1_size);

#ifdef __cplusplus
}
#endif

#endif
