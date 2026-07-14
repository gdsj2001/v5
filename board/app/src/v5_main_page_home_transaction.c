#include "v5_main_page_home_transaction.h"

#include "v5_command_gate_ipc.h"
#include "v5_lvgl_clock.h"
#include "v5_motion_model_registry.h"
#include "v5_native_home.h"
#include "v5_main_page_internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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
    unsigned int timeout_ms;
    int call_ok;
    unsigned long long run_id;
    unsigned int generation;
    V5MainPageActionReport report;
    V5CommandGateResult gate_result;
    V5CommandGateHomeStatus progress;
    unsigned long long terminal_seen_ms;
} V5MainPageHomeTransactionState;

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
    unsigned int timeout_ms)
{
#ifdef V5_MAIN_PAGE_HOME_TRANSACTION_TEST_HOOKS
    if (g_send_hook) {
        return g_send_hook(prepared, request, result, timeout_ms);
    }
#endif
    return v5_command_gate_send_prepared_home(
        prepared, request, result, timeout_ms,
        g_home_transaction.run_id, g_home_transaction.generation);
}

static int probe_progress(V5CommandGateHomeStatus *status, unsigned int timeout_ms)
{
#ifdef V5_MAIN_PAGE_HOME_TRANSACTION_TEST_HOOKS
    if (g_progress_hook) {
        return g_progress_hook(g_home_transaction.run_id, g_home_transaction.generation, status, timeout_ms);
    }
#endif
    return v5_command_gate_probe_home_status(
        g_home_transaction.run_id, g_home_transaction.generation, status, timeout_ms);
}

static void store_progress(const V5CommandGateHomeStatus *status)
{
    if (!status || status->run_id != g_home_transaction.run_id ||
        status->generation != g_home_transaction.generation) return;
    PROGRESS_LOCK();
    g_home_transaction.progress = *status;
    if (status->terminal && g_home_transaction.terminal_seen_ms == 0ULL) {
        g_home_transaction.terminal_seen_ms = monotonic_ms();
    }
    PROGRESS_UNLOCK();
}

static void home_transaction_run(void)
{
    v5_command_gate_result_init(&g_home_transaction.gate_result);
    g_home_transaction.call_ok = send_prepared_background(
        &g_home_transaction.report.command,
        &g_home_transaction.report.request,
        &g_home_transaction.gate_result,
        g_home_transaction.timeout_ms);
    {
        V5CommandGateHomeStatus final_status;
        if (probe_progress(&final_status, 100U)) store_progress(&final_status);
    }
    atomic_store_release(&g_home_transaction.completed, 1);
}

static void home_progress_run(void)
{
    while (atomic_load_acquire(&g_home_transaction.active) &&
           !atomic_load_acquire(&g_home_transaction.completed)) {
        V5CommandGateHomeStatus status;
        if (probe_progress(&status, 80U)) store_progress(&status);
#ifdef _WIN32
        Sleep(100U);
#else
        usleep(100000U);
#endif
    }
}

#ifdef _WIN32
static DWORD WINAPI home_transaction_thread(void *unused)
{
    (void)unused;
    home_transaction_run();
    return 0U;
}
static DWORD WINAPI home_progress_thread(void *unused)
{
    (void)unused;
    home_progress_run();
    return 0U;
}
#else
static void *home_transaction_thread(void *unused)
{
    (void)unused;
    home_transaction_run();
    return 0;
}
static void *home_progress_thread(void *unused)
{
    (void)unused;
    home_progress_run();
    return 0;
}
#endif

static int start_background_thread(void)
{
#ifdef _WIN32
    HANDLE thread = CreateThread(0, 0U, home_transaction_thread, 0, 0U, 0);
    HANDLE progress_thread;
    if (!thread) {
        return 0;
    }
    CloseHandle(thread);
    progress_thread = CreateThread(0, 0U, home_progress_thread, 0, 0U, 0);
    if (!progress_thread) return 0;
    CloseHandle(progress_thread);
    return 1;
#else
    pthread_t thread;
    pthread_t progress_thread;
    if (pthread_create(&thread, 0, home_transaction_thread, 0) != 0) {
        return 0;
    }
    (void)pthread_detach(thread);
    if (pthread_create(&progress_thread, 0, home_progress_thread, 0) != 0) return 0;
    (void)pthread_detach(progress_thread);
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
    g_home_transaction.generation = next_generation();
    g_home_transaction.run_id = make_run_id(g_home_transaction.generation);
    g_home_transaction.timeout_ms = timeout_ms;
    g_home_transaction.call_ok = 0;
    g_home_transaction.report = *report;
    PROGRESS_LOCK();
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
    if (!start_background_thread()) {
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
        if (strcmp(status->direct_reason, "HOME_RTCP_FORCE_OFF_NOT_CONFIRMED") == 0) {
            snprintf(text, text_cap, "%s", "回零前RTCP状态未能切换为关闭");
        } else {
            snprintf(text, text_cap, "%s轴回零失败", axes);
        }
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
    if (!page || !atomic_load_acquire(&g_home_transaction.completed)) {
        return 0;
    }
    report = g_home_transaction.report;
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
    atomic_store_release(&g_home_transaction.completed, 0);
    atomic_store_release(&g_home_transaction.active, 0);
    v5_main_page_internal_set_home_transaction_active(page, 0, 1);
    if (strcmp(report.readback_code, "HOME_PRECONDITION_ESTOP") == 0 ||
        strcmp(report.readback_code, "HOME_PRECONDITION_DISABLED") == 0) {
        v5_main_page_internal_show_home_precondition_popup(page, report.readback_code);
    }
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
