#include "v5_drive_write_window.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static SRWLOCK g_drive_write_window_lock = SRWLOCK_INIT;
#define DRIVE_WINDOW_LOCK() AcquireSRWLockExclusive(&g_drive_write_window_lock)
#define DRIVE_WINDOW_UNLOCK() ReleaseSRWLockExclusive(&g_drive_write_window_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_drive_write_window_lock = PTHREAD_MUTEX_INITIALIZER;
#define DRIVE_WINDOW_LOCK() ((void)pthread_mutex_lock(&g_drive_write_window_lock))
#define DRIVE_WINDOW_UNLOCK() ((void)pthread_mutex_unlock(&g_drive_write_window_lock))
#endif

typedef struct V5DriveWriteWindowState {
    int active;
    int initial_machine_enabled;
    char run_id[V5_DRIVE_WRITE_RUN_ID_CAP];
} V5DriveWriteWindowState;

static V5DriveWriteWindowState g_drive_write_window;

static void set_code(V5DriveWriteWindowResult *result, const char *code)
{
    if (result) {
        snprintf(result->code, sizeof(result->code), "%s", code ? code : "");
    }
}

void v5_drive_write_window_result_init(V5DriveWriteWindowResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    set_code(result, "DRIVE_WRITE_WINDOW_NOT_ATTEMPTED");
}

static int run_id_ok(const char *run_id)
{
    const unsigned char *p = (const unsigned char *)run_id;
    size_t length = 0U;
    if (!run_id || !run_id[0]) {
        return 0;
    }
    while (*p) {
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.' || *p == ':')) {
            return 0;
        }
        if (++length >= V5_DRIVE_WRITE_RUN_ID_CAP) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int ops_ok(const V5DriveWriteWindowOps *ops)
{
    return ops && ops->read_safety && ops->set_machine_off && ops->set_machine_on;
}

static int read_actual(
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteSafetyActual *actual,
    V5DriveWriteWindowResult *result)
{
    memset(actual, 0, sizeof(*actual));
    if (!ops->read_safety(ops->context, actual)) {
        set_code(result, "DRIVE_WRITE_SAFETY_ACTUAL_UNAVAILABLE");
        return 0;
    }
    result->final_machine_enable_known = actual->machine_enable_known ? 1 : 0;
    result->final_machine_enabled = actual->machine_enabled ? 1 : 0;
    return 1;
}

static int ensure_machine_off(
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteWindowResult *result)
{
    V5DriveWriteSafetyActual actual;
    int have_actual = read_actual(ops, &actual, result) && actual.machine_enable_known;
    if (!have_actual || actual.machine_enabled) {
        (void)ops->set_machine_off(ops->context);
        if (!read_actual(ops, &actual, result) || !actual.machine_enable_known) {
            set_code(result, "DRIVE_WRITE_MACHINE_OFF_ACTUAL_UNAVAILABLE");
            return 0;
        }
    }
    if (actual.machine_enabled) {
        set_code(result, "DRIVE_WRITE_MACHINE_OFF_NOT_CONFIRMED");
        return 0;
    }
    return 1;
}

static int restore_machine_on(
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteWindowResult *result)
{
    V5DriveWriteSafetyActual actual;
    if (!read_actual(ops, &actual, result) ||
        !actual.safety_estop_known || !actual.machine_enable_known) {
        set_code(result, "DRIVE_WRITE_RESTORE_SAFETY_ACTUAL_UNAVAILABLE");
        return 0;
    }
    if (actual.safety_estop_active) {
        return ensure_machine_off(ops, result);
    }
    if (!actual.machine_enabled) {
        (void)ops->set_machine_on(ops->context);
        if (!read_actual(ops, &actual, result) || !actual.machine_enable_known) {
            set_code(result, "DRIVE_WRITE_MACHINE_ON_ACTUAL_UNAVAILABLE");
            return 0;
        }
    }
    if (!actual.machine_enabled) {
        set_code(result, "DRIVE_WRITE_MACHINE_ON_NOT_CONFIRMED");
        return 0;
    }
    return 1;
}

static void clear_state(void)
{
    memset(&g_drive_write_window, 0, sizeof(g_drive_write_window));
}

int v5_drive_write_window_begin(
    const char *run_id,
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteWindowResult *result)
{
    V5DriveWriteSafetyActual initial;
    int ok = 0;
    v5_drive_write_window_result_init(result);
    if (!result || !run_id_ok(run_id) || !ops_ok(ops)) {
        set_code(result, "DRIVE_WRITE_WINDOW_BAD_REQUEST");
        return 0;
    }
    DRIVE_WINDOW_LOCK();
    if (g_drive_write_window.active) {
        result->initial_machine_enabled = g_drive_write_window.initial_machine_enabled;
        if (strcmp(g_drive_write_window.run_id, run_id) != 0) {
            set_code(result, "DRIVE_WRITE_WINDOW_BUSY");
        } else if (ensure_machine_off(ops, result)) {
            result->ok = 1;
            set_code(result, "DRIVE_WRITE_WINDOW_ALREADY_ACTIVE");
            ok = 1;
        }
        DRIVE_WINDOW_UNLOCK();
        return ok;
    }
    if (!read_actual(ops, &initial, result) ||
        !initial.safety_estop_known || !initial.machine_enable_known) {
        (void)ensure_machine_off(ops, result);
        set_code(result, "DRIVE_WRITE_INITIAL_SAFETY_ACTUAL_UNAVAILABLE");
        DRIVE_WINDOW_UNLOCK();
        return 0;
    }
    result->initial_machine_enabled = initial.machine_enabled ? 1 : 0;
    if (!ensure_machine_off(ops, result)) {
        DRIVE_WINDOW_UNLOCK();
        return 0;
    }
    g_drive_write_window.active = 1;
    g_drive_write_window.initial_machine_enabled = result->initial_machine_enabled;
    snprintf(g_drive_write_window.run_id, sizeof(g_drive_write_window.run_id), "%s", run_id);
    result->ok = 1;
    set_code(result, "DRIVE_WRITE_WINDOW_BEGIN_OK");
    DRIVE_WINDOW_UNLOCK();
    return 1;
}

int v5_drive_write_window_finish(
    const char *run_id,
    int allow_restore,
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteWindowResult *result)
{
    V5DriveWriteSafetyActual before;
    int restore_requested = 0;
    int estop_known = 0;
    int estop_active = 0;
    int ok;
    v5_drive_write_window_result_init(result);
    if (!result || !run_id_ok(run_id) || !ops_ok(ops) ||
        (allow_restore != 0 && allow_restore != 1)) {
        set_code(result, "DRIVE_WRITE_WINDOW_BAD_REQUEST");
        return 0;
    }
    DRIVE_WINDOW_LOCK();
    if (!g_drive_write_window.active) {
        set_code(result, "DRIVE_WRITE_WINDOW_NOT_ACTIVE");
        DRIVE_WINDOW_UNLOCK();
        return 0;
    }
    result->initial_machine_enabled = g_drive_write_window.initial_machine_enabled;
    if (strcmp(g_drive_write_window.run_id, run_id) != 0) {
        set_code(result, "DRIVE_WRITE_WINDOW_RUN_MISMATCH");
        DRIVE_WINDOW_UNLOCK();
        return 0;
    }
    if (!read_actual(ops, &before, result)) {
        ok = 0;
    } else {
        estop_known = before.safety_estop_known ? 1 : 0;
        estop_active = before.safety_estop_known && before.safety_estop_active;
        restore_requested = allow_restore && result->initial_machine_enabled &&
                            before.safety_estop_known && !before.safety_estop_active;
        ok = restore_requested ? restore_machine_on(ops, result)
                               : ensure_machine_off(ops, result);
    }
    if (!ok) {
        char failure_code[64];
        int safe_off;
        snprintf(failure_code, sizeof(failure_code), "%s", result->code);
        safe_off = ensure_machine_off(ops, result);
        set_code(result, failure_code);
        if (safe_off) {
            clear_state();
        }
        DRIVE_WINDOW_UNLOCK();
        return 0;
    }
    clear_state();
    result->ok = 1;
    if (restore_requested && result->final_machine_enabled) {
        set_code(result, "DRIVE_WRITE_WINDOW_FINISH_RESTORED");
    } else if (allow_restore && result->initial_machine_enabled && !estop_known) {
        set_code(result, "DRIVE_WRITE_WINDOW_FINISH_SAFETY_UNKNOWN_KEEP_OFF");
    } else if (allow_restore && result->initial_machine_enabled && estop_active) {
        set_code(result, "DRIVE_WRITE_WINDOW_FINISH_ESTOP_KEEP_OFF");
    } else if (allow_restore && result->initial_machine_enabled &&
               !result->final_machine_enabled) {
        set_code(result, "DRIVE_WRITE_WINDOW_FINISH_SAFETY_CHANGED_KEEP_OFF");
    } else {
        set_code(result, "DRIVE_WRITE_WINDOW_FINISH_KEEP_OFF");
    }
    DRIVE_WINDOW_UNLOCK();
    return 1;
}

int v5_drive_write_window_abort(
    const char *run_id,
    const V5DriveWriteWindowOps *ops,
    V5DriveWriteWindowResult *result)
{
    V5DriveWriteSafetyActual initial;
    int was_active;
    int ok;
    v5_drive_write_window_result_init(result);
    if (!result || !run_id_ok(run_id) || !ops_ok(ops)) {
        set_code(result, "DRIVE_WRITE_WINDOW_BAD_REQUEST");
        return 0;
    }
    DRIVE_WINDOW_LOCK();
    was_active = g_drive_write_window.active;
    if (was_active && strcmp(g_drive_write_window.run_id, run_id) != 0) {
        result->initial_machine_enabled = g_drive_write_window.initial_machine_enabled;
        set_code(result, "DRIVE_WRITE_WINDOW_RUN_MISMATCH");
        DRIVE_WINDOW_UNLOCK();
        return 0;
    }
    if (was_active) {
        result->initial_machine_enabled = g_drive_write_window.initial_machine_enabled;
    } else if (read_actual(ops, &initial, result) && initial.machine_enable_known) {
        result->initial_machine_enabled = initial.machine_enabled ? 1 : 0;
    }
    ok = ensure_machine_off(ops, result);
    if (was_active && ok) {
        clear_state();
    }
    if (!ok) {
        DRIVE_WINDOW_UNLOCK();
        return 0;
    }
    result->ok = 1;
    set_code(result, was_active
        ? "DRIVE_WRITE_WINDOW_ABORT_OK"
        : "DRIVE_WRITE_WINDOW_ABORT_NOT_ACTIVE");
    DRIVE_WINDOW_UNLOCK();
    return 1;
}

int v5_drive_write_window_blocks_kind(V5CommandKind kind)
{
    int active;
    int blocked = 0;
    DRIVE_WINDOW_LOCK();
    active = g_drive_write_window.active;
    if (active) {
        switch (kind) {
        case V5_COMMAND_START:
        case V5_COMMAND_MDI_RUN:
        case V5_COMMAND_RESUME:
        case V5_COMMAND_HOME:
        case V5_COMMAND_JOG_INCREMENT:
        case V5_COMMAND_JOG_CONTINUOUS:
        case V5_COMMAND_ESTOP_RESET:
        case V5_COMMAND_WORK_ZERO:
        case V5_COMMAND_FIRST_POINT:
        case V5_COMMAND_AXIS_ZERO_POSITION:
            blocked = 1;
            break;
        default:
            break;
        }
    }
    DRIVE_WINDOW_UNLOCK();
    return blocked;
}

void v5_drive_write_window_reset_for_test(void)
{
    DRIVE_WINDOW_LOCK();
    clear_state();
    DRIVE_WINDOW_UNLOCK();
}
