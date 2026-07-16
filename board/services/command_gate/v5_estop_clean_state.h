#ifndef V5_ESTOP_CLEAN_STATE_H
#define V5_ESTOP_CLEAN_STATE_H

#include "v5_linuxcncrsh_client.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*V5EstopCleanLockFn)(void *context);

typedef struct V5EstopCleanStatus {
    unsigned int generation;
    int active;
    int terminal;
    int ok;
    char code[64];
} V5EstopCleanStatus;

int v5_estop_clean_state_start(
    const V5LinuxcncrshConfig *config,
    V5EstopCleanLockFn lock_fn,
    V5EstopCleanLockFn unlock_fn,
    void *lock_context,
    unsigned int *generation_out);
int v5_estop_clean_state_snapshot(
    unsigned int generation,
    V5EstopCleanStatus *status);
int v5_estop_clean_state_wait_latest_terminal(
    unsigned int timeout_ms,
    V5EstopCleanStatus *status);

#ifdef __cplusplus
}
#endif

#endif
