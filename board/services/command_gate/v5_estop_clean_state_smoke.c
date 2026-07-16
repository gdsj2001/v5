#include "v5_estop_clean_state.h"

#include "v5_native_rtcp_control.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static volatile int g_fail_clean;
static volatile int g_cancel_calls;
static volatile int g_lock_calls;
static volatile int g_unlock_calls;

int v5_native_home_runtime_cancel_active_by_estop(void)
{
    ++g_cancel_calls;
    return 1;
}

int v5_native_rtcp_control_force_off(V5NativeRtcpControlResult *result)
{
    if (result) {
        memset(result, 0, sizeof(*result));
        result->actual_known = 1;
    }
    return V5_NATIVE_RTCP_CONTROL_SEND_SENT;
}

int v5_linuxcncrsh_clean_execution_after_estop(
    const V5LinuxcncrshConfig *config,
    V5LinuxcncrshTaskState *state,
    char *code,
    size_t code_cap)
{
    (void)config;
#ifdef _WIN32
    Sleep(40U);
#else
    usleep(40000U);
#endif
    if (state) memset(state, 0, sizeof(*state));
    snprintf(code, code_cap, "%s", g_fail_clean ? "ESTOP_CLEAN_TASK_NOT_IDLE" : "ESTOP_CLEAN_OK");
    return g_fail_clean ? 0 : 1;
}

static void lock_cb(void *context)
{
    (void)context;
    ++g_lock_calls;
}

static void unlock_cb(void *context)
{
    (void)context;
    ++g_unlock_calls;
}

int main(void)
{
    V5LinuxcncrshConfig config = {"127.0.0.1", 5007U, "EMC", "smoke", 100U};
    V5EstopCleanStatus status;
    unsigned int first = 0U;
    unsigned int duplicate = 0U;
    unsigned int second = 0U;

    if (!v5_estop_clean_state_start(&config, lock_cb, unlock_cb, 0, &first) || first == 0U ||
        !v5_estop_clean_state_start(&config, lock_cb, unlock_cb, 0, &duplicate) || duplicate != first) {
        fprintf(stderr, "clean generation start/dedup failed first=%u duplicate=%u\n", first, duplicate);
        return 1;
    }
    if (!v5_estop_clean_state_wait_latest_terminal(500U, &status) ||
        status.generation != first || !status.terminal || !status.ok ||
        strcmp(status.code, "ESTOP_CLEAN_OK") != 0 ||
        g_cancel_calls != 1 || g_lock_calls != 1 || g_unlock_calls != 1) {
        fprintf(stderr, "clean success terminal invalid gen=%u active=%d terminal=%d ok=%d code=%s\n",
                status.generation, status.active, status.terminal, status.ok, status.code);
        return 2;
    }

    g_fail_clean = 1;
    if (!v5_estop_clean_state_start(&config, lock_cb, unlock_cb, 0, &second) || second == first ||
        !v5_estop_clean_state_wait_latest_terminal(500U, &status) ||
        status.generation != second || !status.terminal || status.ok ||
        strcmp(status.code, "ESTOP_CLEAN_TASK_NOT_IDLE") != 0) {
        fprintf(stderr, "clean failure terminal invalid gen=%u terminal=%d ok=%d code=%s\n",
                status.generation, status.terminal, status.ok, status.code);
        return 3;
    }
    puts("v5_estop_clean_state_smoke: PASS");
    return 0;
}
