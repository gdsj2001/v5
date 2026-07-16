#include "v5_main_page_home_transaction.h"

#include "v5_command_gate_ipc.h"
#include "v5_lvgl_clock.h"
#include "v5_motion_model_registry.h"
#include "v5_native_home.h"
#include "v5_main_page_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef V5_HOME_TERMINAL_PROBE_ATTEMPTS
#define V5_HOME_TERMINAL_PROBE_ATTEMPTS 20U
#endif
#ifndef V5_HOME_TERMINAL_PROBE_TIMEOUT_MS
#define V5_HOME_TERMINAL_PROBE_TIMEOUT_MS 100U
#endif
#ifndef V5_HOME_TERMINAL_PROBE_DELAY_MS
#define V5_HOME_TERMINAL_PROBE_DELAY_MS 100U
#endif

#ifdef _WIN32
#include <windows.h>
typedef volatile LONG V5HomeAtomic;
static SRWLOCK g_progress_lock = SRWLOCK_INIT;
#define PROGRESS_LOCK() AcquireSRWLockExclusive(&g_progress_lock)
#define PROGRESS_UNLOCK() ReleaseSRWLockExclusive(&g_progress_lock)
#else
#include <pthread.h>
#include <unistd.h>
typedef volatile int V5HomeAtomic;
static pthread_mutex_t g_progress_lock = PTHREAD_MUTEX_INITIALIZER;
#define PROGRESS_LOCK() ((void)pthread_mutex_lock(&g_progress_lock))
#define PROGRESS_UNLOCK() ((void)pthread_mutex_unlock(&g_progress_lock))
#endif

typedef struct V5MainPageHomeTransactionState {
    V5HomeAtomic active;
    V5HomeAtomic completed;
    V5HomeAtomic terminal_ready;
    unsigned int timeout_ms;
    int call_ok;
    unsigned long long run_id;
    unsigned int generation;
    V5MainPageActionReport report;
    V5CommandGateResult gate_result;
    V5CommandGateHomeStatus progress;
    unsigned long long terminal_seen_ms;
} V5MainPageHomeTransactionState;

typedef struct V5MainPageHomeWorkerBinding {
    unsigned long long run_id;
    unsigned int generation;
} V5MainPageHomeWorkerBinding;

static V5MainPageHomeTransactionState g_home_transaction;

#ifdef V5_MAIN_PAGE_HOME_TRANSACTION_TEST_HOOKS
static V5MainPageHomeTransactionSendHook g_send_hook;
static V5MainPageHomeTransactionProgressHook g_progress_hook;
#endif

static unsigned int next_generation(void)
{
#ifdef _WIN32
    static volatile LONG value;
    LONG next = InterlockedIncrement(&value);
    return next > 0 ? (unsigned int)next : 1U;
#else
    static volatile unsigned int value;
    unsigned int next = __atomic_add_fetch(&value, 1U, __ATOMIC_RELAXED);
    return next ? next : __atomic_add_fetch(&value, 1U, __ATOMIC_RELAXED);
#endif
}

static unsigned long long make_run_id(unsigned int generation)
{
#ifdef _WIN32
    return ((unsigned long long)GetTickCount64() << 16) ^ (unsigned long long)generation;
#else
    struct timespec now;
    unsigned long long value = (unsigned long long)generation;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        value ^= ((unsigned long long)now.tv_sec * 1000000000ULL) + (unsigned long long)now.tv_nsec;
    }
    return value ? value : 1ULL;
#endif
}

static unsigned long long monotonic_ms(void)
{
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0ULL;
    return ((unsigned long long)now.tv_sec * 1000ULL) + (unsigned long long)(now.tv_nsec / 1000000ULL);
#endif
}

static int atomic_claim(V5HomeAtomic *value)
{
#ifdef _WIN32
    return InterlockedCompareExchange(value, 1L, 0L) == 0L;
#else
    int expected = 0;
    return __atomic_compare_exchange_n(
        value, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
}

static int atomic_load_acquire(const V5HomeAtomic *value)
{
#ifdef _WIN32
    return InterlockedCompareExchange((V5HomeAtomic *)value, 0L, 0L) != 0L;
#else
    return __atomic_load_n(value, __ATOMIC_ACQUIRE) != 0;
#endif
}

static void atomic_store_release(V5HomeAtomic *value, int state)
{
#ifdef _WIN32
    InterlockedExchange(value, state ? 1L : 0L);
#else
    __atomic_store_n(value, state ? 1 : 0, __ATOMIC_RELEASE);
#endif
}

static int send_prepared_background(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms,
    const V5MainPageHomeWorkerBinding *binding)
{
    if (!binding) return 0;
#ifdef V5_MAIN_PAGE_HOME_TRANSACTION_TEST_HOOKS
    if (g_send_hook) {
        return g_send_hook(prepared, request, result, timeout_ms);
    }
#endif
    return v5_command_gate_send_prepared_home(
        prepared, request, result, timeout_ms,
        binding->run_id, binding->generation);
}

static int probe_progress(
    const V5MainPageHomeWorkerBinding *binding,
    V5CommandGateHomeStatus *status,
    unsigned int timeout_ms)
{
    if (!binding) return 0;
#ifdef V5_MAIN_PAGE_HOME_TRANSACTION_TEST_HOOKS
    if (g_progress_hook) {
        return g_progress_hook(binding->run_id, binding->generation, status, timeout_ms);
    }
#endif
    return v5_command_gate_probe_home_status(
        binding->run_id, binding->generation, status, timeout_ms);
}

static int binding_current(const V5MainPageHomeWorkerBinding *binding)
{
    int current;
    if (!binding || !atomic_load_acquire(&g_home_transaction.active)) return 0;
    PROGRESS_LOCK();
    current = g_home_transaction.run_id == binding->run_id &&
              g_home_transaction.generation == binding->generation;
    PROGRESS_UNLOCK();
    return current;
}

static void store_progress(
    const V5MainPageHomeWorkerBinding *binding,
    const V5CommandGateHomeStatus *status)
{
    if (!binding || !status || status->run_id != binding->run_id ||
        status->generation != binding->generation) return;
    PROGRESS_LOCK();
    if (g_home_transaction.run_id == binding->run_id &&
        g_home_transaction.generation == binding->generation) {
        g_home_transaction.progress = *status;
        if (status->terminal && g_home_transaction.terminal_seen_ms == 0ULL) {
            g_home_transaction.terminal_seen_ms = monotonic_ms();
            atomic_store_release(&g_home_transaction.terminal_ready, 1);
        }
    }
    PROGRESS_UNLOCK();
}

static int terminal_progress_seen(const V5MainPageHomeWorkerBinding *binding)
{
    int seen = 0;
    if (!binding) return 0;
    PROGRESS_LOCK();
    if (g_home_transaction.run_id == binding->run_id &&
        g_home_transaction.generation == binding->generation &&
        g_home_transaction.progress.run_id == binding->run_id &&
        g_home_transaction.progress.generation == binding->generation &&
        g_home_transaction.progress.terminal) {
        seen = 1;
    }
    PROGRESS_UNLOCK();
    return seen;
}

static void home_worker_delay(unsigned int delay_ms)
{
#ifdef _WIN32
    Sleep(delay_ms);
#else
    usleep(delay_ms * 1000U);
#endif
}

static void home_transaction_run(const V5MainPageHomeWorkerBinding *binding)
{
    V5CommandGateResult gate_result;
    V5CommandGateHomeStatus final_status;
    int call_ok;
    unsigned int attempt;
    if (!binding_current(binding)) return;
    v5_command_gate_result_init(&gate_result);
    call_ok = send_prepared_background(
        &g_home_transaction.report.command,
        &g_home_transaction.report.request,
        &gate_result,
        g_home_transaction.timeout_ms,
        binding);
    for (attempt = 0U;
         attempt < V5_HOME_TERMINAL_PROBE_ATTEMPTS && binding_current(binding);
         ++attempt) {
        if (terminal_progress_seen(binding)) break;
        if (probe_progress(binding, &final_status, V5_HOME_TERMINAL_PROBE_TIMEOUT_MS)) {
            store_progress(binding, &final_status);
            if (final_status.terminal) break;
        }
        if (attempt + 1U < V5_HOME_TERMINAL_PROBE_ATTEMPTS) {
            home_worker_delay(V5_HOME_TERMINAL_PROBE_DELAY_MS);
        }
    }
    if (!binding_current(binding)) return;
    if (!terminal_progress_seen(binding)) {
        memset(&final_status, 0, sizeof(final_status));
        final_status.run_id = binding->run_id;
        final_status.generation = binding->generation;
        final_status.phase = V5_NATIVE_HOME_PHASE_FAILED;
        final_status.failure_phase = V5_NATIVE_HOME_PHASE_PREPARING;
        final_status.terminal = 1;
        snprintf(final_status.direct_reason, sizeof(final_status.direct_reason), "%s",
                 "HOME_TERMINAL_STATUS_MISSING");
        store_progress(binding, &final_status);
    }
    g_home_transaction.gate_result = gate_result;
    g_home_transaction.call_ok = call_ok;
    atomic_store_release(&g_home_transaction.completed, 1);
}

static void home_progress_run(const V5MainPageHomeWorkerBinding *binding)
{
    while (binding_current(binding) &&
           !atomic_load_acquire(&g_home_transaction.completed)) {
        V5CommandGateHomeStatus status;
        if (probe_progress(binding, &status, 80U)) store_progress(binding, &status);
        home_worker_delay(100U);
    }
}

#ifdef _WIN32
static DWORD WINAPI home_transaction_thread(void *context)
{
    V5MainPageHomeWorkerBinding binding = *(V5MainPageHomeWorkerBinding *)context;
    free(context);
    home_transaction_run(&binding);
    return 0U;
}
static DWORD WINAPI home_progress_thread(void *context)
{
    V5MainPageHomeWorkerBinding binding = *(V5MainPageHomeWorkerBinding *)context;
    free(context);
    home_progress_run(&binding);
    return 0U;
}
#else
static void *home_transaction_thread(void *context)
{
    V5MainPageHomeWorkerBinding binding = *(V5MainPageHomeWorkerBinding *)context;
    free(context);
    home_transaction_run(&binding);
    return 0;
}
static void *home_progress_thread(void *context)
{
    V5MainPageHomeWorkerBinding binding = *(V5MainPageHomeWorkerBinding *)context;
    free(context);
    home_progress_run(&binding);
    return 0;
}
#endif

static V5MainPageHomeWorkerBinding *new_worker_binding(
    unsigned long long run_id,
    unsigned int generation)
{
    V5MainPageHomeWorkerBinding *binding =
        (V5MainPageHomeWorkerBinding *)malloc(sizeof(*binding));
    if (binding) {
        binding->run_id = run_id;
        binding->generation = generation;
    }
    return binding;
}

static int start_background_thread(unsigned long long run_id, unsigned int generation)
{
    V5MainPageHomeWorkerBinding *execute_binding = new_worker_binding(run_id, generation);
    V5MainPageHomeWorkerBinding *progress_binding = new_worker_binding(run_id, generation);
    if (!execute_binding || !progress_binding) {
        free(execute_binding);
        free(progress_binding);
        return 0;
    }
#ifdef _WIN32
    HANDLE thread;
    HANDLE progress_thread = CreateThread(0, 0U, home_progress_thread, progress_binding, 0U, 0);
    if (!progress_thread) {
        free(execute_binding);
        free(progress_binding);
        return 0;
    }
    CloseHandle(progress_thread);
    thread = CreateThread(0, 0U, home_transaction_thread, execute_binding, 0U, 0);
    if (!thread) {
        free(execute_binding);
        return 0;
    }
    CloseHandle(thread);
    return 1;
#else
    pthread_t thread;
    pthread_t progress_thread;
    if (pthread_create(&progress_thread, 0, home_progress_thread, progress_binding) != 0) {
        free(execute_binding);
        free(progress_binding);
        return 0;
    }
    (void)pthread_detach(progress_thread);
    if (pthread_create(&thread, 0, home_transaction_thread, execute_binding) != 0) {
        free(execute_binding);
        return 0;
    }
    (void)pthread_detach(thread);
    return 1;
#endif
}

static int home_request(const V5MainPageActionReport *report)
{
    return report && report->action == V5_MAIN_PAGE_ACTION_HOME &&
           (report->request.kind == V5_COMMAND_HOME ||
            report->request.kind == V5_COMMAND_AXIS_ZERO_POSITION);
}

int v5_main_page_home_transaction_start(
    V5MainPage *page,
    V5MainPageActionReport *report,
    unsigned int timeout_ms)
{
    unsigned int generation;
    unsigned long long run_id;
    if (!page || !home_request(report) || timeout_ms == 0U) {
        return 0;
    }
    if (!atomic_claim(&g_home_transaction.active)) {
        report->send_status = V5_COMMAND_GATE_SEND_INVALID;
        report->executed = 0;
        report->pending_readback = 1;
        snprintf(report->readback_code, sizeof(report->readback_code), "%s", "HOME_TRANSACTION_ALREADY_ACTIVE");
        return 0;
    }
    atomic_store_release(&g_home_transaction.completed, 0);
    atomic_store_release(&g_home_transaction.terminal_ready, 0);
    generation = next_generation();
    run_id = make_run_id(generation);
    g_home_transaction.timeout_ms = timeout_ms;
    g_home_transaction.call_ok = 0;
    g_home_transaction.report = *report;
    PROGRESS_LOCK();
    g_home_transaction.generation = generation;
    g_home_transaction.run_id = run_id;
    memset(&g_home_transaction.progress, 0, sizeof(g_home_transaction.progress));
    g_home_transaction.terminal_seen_ms = 0ULL;
    PROGRESS_UNLOCK();
    g_home_transaction.report.send_status = V5_COMMAND_GATE_SEND_SENT;
    g_home_transaction.report.executed = 0;
    g_home_transaction.report.pending_readback = 1;
    snprintf(
        g_home_transaction.report.readback_code,
        sizeof(g_home_transaction.report.readback_code),
        "%s",
        "HOME_TRANSACTION_STARTED");
    if (!start_background_thread(run_id, generation)) {
        atomic_store_release(&g_home_transaction.active, 0);
        report->send_status = V5_COMMAND_GATE_SEND_UNAVAILABLE;
        report->executed = 0;
        report->pending_readback = 0;
        snprintf(report->readback_code, sizeof(report->readback_code), "%s", "HOME_TRANSACTION_START_FAILED");
        return 0;
    }
    *report = g_home_transaction.report;
    v5_main_page_internal_set_home_transaction_active(page, 1, 1);
    return 1;
}

int v5_main_page_home_transaction_active(void)
{
    return atomic_load_acquire(&g_home_transaction.active);
}

void v5_main_page_home_transaction_reset_after_estop(V5MainPage *page)
{
    /* E-stop must release the pressed/transaction visual immediately, but the
     * matching native Home worker remains authoritative for the terminal
     * CANCELLED/FAILED result.  Keeping the binding alive lets poll() consume
     * that fresh terminal instead of leaving an old PREPARING status behind. */
    if (page) {
        v5_main_page_internal_set_home_transaction_active(page, 0, 1);
    }
}

int v5_main_page_home_transaction_status(V5CommandGateHomeStatus *status)
{
    if (!status) return 0;
    PROGRESS_LOCK();
    *status = g_home_transaction.progress;
    if (status->terminal && g_home_transaction.terminal_seen_ms != 0ULL &&
        monotonic_ms() > g_home_transaction.terminal_seen_ms + 2000ULL) {
        memset(status, 0, sizeof(*status));
    }
    PROGRESS_UNLOCK();
    return status->run_id == g_home_transaction.run_id &&
           status->generation == g_home_transaction.generation &&
           status->phase != V5_NATIVE_HOME_PHASE_IDLE;
}

int v5_main_page_home_transaction_format_status_cn(
    const V5CommandGateHomeStatus *status,
    char *text,
    size_t text_cap)
{
    char axes[32] = "";
    unsigned int bit;
    size_t used = 0U;
    if (!status || !text || text_cap == 0U) return 0;
    text[0] = '\0';
    for (bit = 0U; bit < 6U; ++bit) {
        static const char names[6] = {'X','Y','Z','A','B','C'};
        if ((status->current_axis_mask & (1U << bit)) != 0U && used + 3U < sizeof(axes)) {
            if (used) axes[used++] = '/';
            axes[used++] = names[bit];
            axes[used] = '\0';
        }
    }
    if (status->phase == V5_NATIVE_HOME_PHASE_RTCP_FORCE_OFF) {
        snprintf(text, text_cap, "%s", "正在关闭RTCP");
    } else if (status->phase == V5_NATIVE_HOME_PHASE_COMPLETE) {
        snprintf(text, text_cap, "%s", "回零完成");
    } else if (status->phase == V5_NATIVE_HOME_PHASE_CANCELLED || status->cancelled) {
        snprintf(text, text_cap, "%s", "回零已取消");
    } else if (status->phase == V5_NATIVE_HOME_PHASE_FAILED) {
        return 0;
    } else if (status->phase == V5_NATIVE_HOME_PHASE_NATIVE_HOME) {
        snprintf(text, text_cap, "%s", "机械全轴回零中");
    } else if (status->phase == V5_NATIVE_HOME_PHASE_PREPARING || !axes[0]) {
        snprintf(text, text_cap, "%s", "准备回零");
    } else if (strcmp(status->mode, "mcs") == 0) {
        snprintf(text, text_cap, "%s轴回机械零中", axes);
    } else if (strcmp(status->mode, "wcs") == 0) {
        snprintf(text, text_cap, "%s轴回加工零中", axes);
    } else {
        snprintf(text, text_cap, "%s轴回零中", axes);
    }
    return text[0] != '\0';
}

int v5_main_page_home_transaction_poll(V5MainPage *page)
{
    V5MainPageActionReport report;
    V5CommandGateResult result;
    V5CommandGateHomeStatus terminal_status;
    int terminal_ready;
    if (!page) {
        return 0;
    }
    terminal_ready = atomic_load_acquire(&g_home_transaction.terminal_ready);
    if (!terminal_ready && !atomic_load_acquire(&g_home_transaction.completed)) return 0;
    report = g_home_transaction.report;
    if (terminal_ready) {
        PROGRESS_LOCK();
        terminal_status = g_home_transaction.progress;
        PROGRESS_UNLOCK();
        report.executed = terminal_status.phase == V5_NATIVE_HOME_PHASE_COMPLETE;
        report.pending_readback = 0;
        snprintf(report.readback_code, sizeof(report.readback_code), "%.*s",
                 (int)sizeof(report.readback_code) - 1,
                 terminal_status.direct_reason[0] ? terminal_status.direct_reason :
                 "HOME_TERMINAL_STATUS_MISSING");
    } else {
        result = g_home_transaction.gate_result;
        report.send_status = result.send_status;
        report.executed = g_home_transaction.call_ok && result.executed &&
                          result.send_status == V5_COMMAND_GATE_SEND_SENT;
        report.machine_on_status = result.machine_on_status;
        report.machine_on_requested = result.machine_on_requested;
        report.pending_readback = 0;
        snprintf(report.command_line, sizeof(report.command_line), "%.*s",
                 (int)sizeof(report.command_line) - 1, result.command_line);
        snprintf(report.readback_code, sizeof(report.readback_code), "%.*s",
                 (int)sizeof(report.readback_code) - 1, result.readback_code);
    }
    atomic_store_release(&g_home_transaction.completed, 0);
    atomic_store_release(&g_home_transaction.terminal_ready, 0);
    atomic_store_release(&g_home_transaction.active, 0);
    v5_main_page_internal_set_home_transaction_active(page, 0, 1);
    if (page->native_readback_refresh_cb) {
        page->native_readback_refresh_cb(page->native_readback_refresh_user_data, V5_MAIN_PAGE_ACTION_HOME);
    }
    page->last_action = report;
    v5_lvgl_clock_advance();
    v5_main_page_internal_log_button_event(V5_MAIN_PAGE_ACTION_HOME, 1, &report);
    return 1;
}

#ifdef V5_MAIN_PAGE_HOME_TRANSACTION_TEST_HOOKS
void v5_main_page_home_transaction_set_send_hook(V5MainPageHomeTransactionSendHook hook)
{
    if (!v5_main_page_home_transaction_active()) {
        g_send_hook = hook;
    }
}

void v5_main_page_home_transaction_set_progress_hook(V5MainPageHomeTransactionProgressHook hook)
{
    if (!v5_main_page_home_transaction_active()) g_progress_hook = hook;
}
#endif
