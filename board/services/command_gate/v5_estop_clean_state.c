#include "v5_estop_clean_state.h"

#include "v5_native_home_runtime_owner.h"
#include "v5_native_rtcp_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static SRWLOCK g_clean_lock = SRWLOCK_INIT;
#define CLEAN_LOCK() AcquireSRWLockExclusive(&g_clean_lock)
#define CLEAN_UNLOCK() ReleaseSRWLockExclusive(&g_clean_lock)
#else
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t g_clean_lock = PTHREAD_MUTEX_INITIALIZER;
#define CLEAN_LOCK() ((void)pthread_mutex_lock(&g_clean_lock))
#define CLEAN_UNLOCK() ((void)pthread_mutex_unlock(&g_clean_lock))
#endif

typedef struct V5EstopCleanBinding {
    unsigned int generation;
    V5LinuxcncrshConfig config;
    V5EstopCleanLockFn lock_fn;
    V5EstopCleanLockFn unlock_fn;
    void *lock_context;
} V5EstopCleanBinding;

static V5EstopCleanStatus g_clean_status;
static unsigned int g_clean_generation;

static void copy_code(char *dst, size_t cap, const char *code)
{
    if (!dst || cap == 0U) return;
    snprintf(dst, cap, "%s", code && code[0] ? code : "ESTOP_CLEAN_FAILED");
}

static void complete_generation(unsigned int generation, int ok, const char *code)
{
    CLEAN_LOCK();
    if (g_clean_status.generation == generation) {
        g_clean_status.active = 0;
        g_clean_status.terminal = 1;
        g_clean_status.ok = ok ? 1 : 0;
        copy_code(g_clean_status.code, sizeof(g_clean_status.code), code);
    }
    CLEAN_UNLOCK();
}

static void clean_generation_run(const V5EstopCleanBinding *binding)
{
    V5NativeRtcpControlResult rtcp;
    V5LinuxcncrshTaskState task_state;
    char code[64] = "ESTOP_CLEAN_FAILED";
    int ok;

    if (!binding) return;
    (void)v5_native_home_runtime_cancel_active_by_estop();
    if (v5_native_rtcp_control_force_off(&rtcp) != V5_NATIVE_RTCP_CONTROL_SEND_SENT) {
        complete_generation(binding->generation, 0, "ESTOP_CLEAN_RTCP_OFF_FAILED");
        return;
    }

    memset(&task_state, 0, sizeof(task_state));
    if (binding->lock_fn) binding->lock_fn(binding->lock_context);
    ok = v5_linuxcncrsh_clean_execution_after_estop(
        &binding->config, &task_state, code, sizeof(code));
    if (binding->unlock_fn) binding->unlock_fn(binding->lock_context);
    complete_generation(binding->generation, ok, ok ? "ESTOP_CLEAN_OK" : code);
}

#ifdef _WIN32
static DWORD WINAPI clean_generation_thread(void *context)
{
    V5EstopCleanBinding binding = *(V5EstopCleanBinding *)context;
    free(context);
    clean_generation_run(&binding);
    return 0U;
}
#else
static void *clean_generation_thread(void *context)
{
    V5EstopCleanBinding binding = *(V5EstopCleanBinding *)context;
    free(context);
    clean_generation_run(&binding);
    return 0;
}
#endif

static int start_thread(V5EstopCleanBinding *binding)
{
#ifdef _WIN32
    HANDLE thread = CreateThread(0, 0U, clean_generation_thread, binding, 0U, 0);
    if (!thread) return 0;
    CloseHandle(thread);
    return 1;
#else
    pthread_t thread;
    if (pthread_create(&thread, 0, clean_generation_thread, binding) != 0) return 0;
    (void)pthread_detach(thread);
    return 1;
#endif
}

static void clean_delay(unsigned int delay_ms)
{
#ifdef _WIN32
    Sleep(delay_ms);
#else
    usleep(delay_ms * 1000U);
#endif
}

int v5_estop_clean_state_start(
    const V5LinuxcncrshConfig *config,
    V5EstopCleanLockFn lock_fn,
    V5EstopCleanLockFn unlock_fn,
    void *lock_context,
    unsigned int *generation_out)
{
    V5EstopCleanBinding *binding;
    unsigned int generation;

    if (!config || !lock_fn || !unlock_fn) return 0;
    binding = (V5EstopCleanBinding *)calloc(1U, sizeof(*binding));
    if (!binding) return 0;

    CLEAN_LOCK();
    if (g_clean_status.active) {
        generation = g_clean_status.generation;
        CLEAN_UNLOCK();
        free(binding);
        if (generation_out) *generation_out = generation;
        return 1;
    }
    generation = ++g_clean_generation;
    if (generation == 0U) generation = ++g_clean_generation;
    memset(&g_clean_status, 0, sizeof(g_clean_status));
    g_clean_status.generation = generation;
    g_clean_status.active = 1;
    copy_code(g_clean_status.code, sizeof(g_clean_status.code), "ESTOP_CLEAN_RUNNING");
    CLEAN_UNLOCK();

    binding->generation = generation;
    binding->config = *config;
    binding->lock_fn = lock_fn;
    binding->unlock_fn = unlock_fn;
    binding->lock_context = lock_context;
    if (!start_thread(binding)) {
        free(binding);
        complete_generation(generation, 0, "ESTOP_CLEAN_THREAD_START_FAILED");
        if (generation_out) *generation_out = generation;
        return 0;
    }
    if (generation_out) *generation_out = generation;
    return 1;
}

int v5_estop_clean_state_snapshot(
    unsigned int generation,
    V5EstopCleanStatus *status)
{
    int matched = 0;
    if (!status) return 0;
    memset(status, 0, sizeof(*status));
    CLEAN_LOCK();
    if (g_clean_status.generation != 0U &&
        (generation == 0U || generation == g_clean_status.generation)) {
        *status = g_clean_status;
        matched = 1;
    }
    CLEAN_UNLOCK();
    return matched;
}

int v5_estop_clean_state_wait_latest_terminal(
    unsigned int timeout_ms,
    V5EstopCleanStatus *status)
{
    unsigned int waited = 0U;
    V5EstopCleanStatus current;
    if (!status) return 0;
    for (;;) {
        if (!v5_estop_clean_state_snapshot(0U, &current)) {
            memset(status, 0, sizeof(*status));
            status->terminal = 1;
            status->ok = 1;
            copy_code(status->code, sizeof(status->code), "ESTOP_CLEAN_NOT_REQUIRED");
            return 1;
        }
        *status = current;
        if (current.terminal) return 1;
        if (waited >= timeout_ms) return 0;
        clean_delay(timeout_ms - waited < 10U ? timeout_ms - waited : 10U);
        waited += timeout_ms - waited < 10U ? timeout_ms - waited : 10U;
    }
}
