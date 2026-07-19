#ifndef V5_JOG_WATCHDOG_H
#define V5_JOG_WATCHDOG_H

#include "v5_linuxcncrsh_client.h"
#include "v5_native_motion_parameters.h"

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define V5_JOG_WATCHDOG_TIMEOUT_MS 750ULL
#define V5_JOG_WATCHDOG_POLL_US 50000U

typedef struct V5JogWatchdog {
    pthread_mutex_t state_lock;
    pthread_mutex_t *command_lock;
    pthread_t thread;
    V5LinuxcncrshConfig *config;
    volatile sig_atomic_t *stop_requested;
    uint64_t deadline_ms;
    char axis;
    int active;
    int started;
} V5JogWatchdog;

static uint64_t v5_jog_watchdog_monotonic_ms(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0ULL;
    }
    return (uint64_t)value.tv_sec * 1000ULL + (uint64_t)value.tv_nsec / 1000000ULL;
}

static void *v5_jog_watchdog_thread_main(void *arg)
{
    V5JogWatchdog *watchdog = (V5JogWatchdog *)arg;
    while (watchdog && watchdog->stop_requested && !*watchdog->stop_requested) {
        char axis = '\0';
        uint64_t now = v5_jog_watchdog_monotonic_ms();
        pthread_mutex_lock(&watchdog->state_lock);
        if (watchdog->active && now && now >= watchdog->deadline_ms) {
            axis = watchdog->axis;
            watchdog->active = 0;
        }
        pthread_mutex_unlock(&watchdog->state_lock);
        if (axis) {
            char line[64];
            snprintf(line, sizeof(line), "Set Jog_Stop %c", axis);
            pthread_mutex_lock(watchdog->command_lock);
            (void)v5_linuxcncrsh_send_line(watchdog->config, line);
            (void)v5_linuxcncrsh_send_line(watchdog->config, "Set Teleop_Enable On");
            pthread_mutex_unlock(watchdog->command_lock);
        }
        usleep(V5_JOG_WATCHDOG_POLL_US);
    }
    return 0;
}

static int v5_jog_watchdog_start(
    V5JogWatchdog *watchdog,
    V5LinuxcncrshConfig *config,
    pthread_mutex_t *command_lock,
    volatile sig_atomic_t *stop_requested)
{
    if (!watchdog || !config || !command_lock || !stop_requested) {
        return 0;
    }
    watchdog->config = config;
    watchdog->command_lock = command_lock;
    watchdog->stop_requested = stop_requested;
    watchdog->deadline_ms = 0ULL;
    watchdog->axis = '\0';
    watchdog->active = 0;
    if (pthread_mutex_init(&watchdog->state_lock, 0) != 0 ||
        pthread_create(&watchdog->thread, 0, v5_jog_watchdog_thread_main, watchdog) != 0) {
        return 0;
    }
    watchdog->started = 1;
    return 1;
}

static int v5_jog_watchdog_refresh(
    V5JogWatchdog *watchdog,
    char axis,
    int *new_transaction)
{
    uint64_t now = v5_jog_watchdog_monotonic_ms();
    int accepted = 0;
    if (new_transaction) {
        *new_transaction = 0;
    }
    if (!watchdog || !watchdog->started || !axis || !now) {
        return 0;
    }
    pthread_mutex_lock(&watchdog->state_lock);
    if (!watchdog->active || watchdog->axis == axis) {
        if (new_transaction && !watchdog->active) {
            *new_transaction = 1;
        }
        watchdog->axis = axis;
        watchdog->deadline_ms = now + V5_JOG_WATCHDOG_TIMEOUT_MS;
        watchdog->active = 1;
        accepted = 1;
    }
    pthread_mutex_unlock(&watchdog->state_lock);
    return accepted;
}

static void v5_jog_watchdog_clear(V5JogWatchdog *watchdog, char axis)
{
    if (!watchdog || !watchdog->started) {
        return;
    }
    pthread_mutex_lock(&watchdog->state_lock);
    if (!axis || watchdog->axis == axis) {
        watchdog->active = 0;
        watchdog->axis = '\0';
        watchdog->deadline_ms = 0ULL;
    }
    pthread_mutex_unlock(&watchdog->state_lock);
}

static int v5_jog_watchdog_prepare_request(
    V5JogWatchdog *watchdog,
    const V5NativeMotionParameters *parameters,
    V5CommandRequest *request,
    char *code,
    size_t code_cap)
{
    int is_jog = request &&
        (request->kind == V5_COMMAND_JOG_INCREMENT ||
         request->kind == V5_COMMAND_JOG_CONTINUOUS ||
         request->kind == V5_COMMAND_JOG_STOP);
    int new_continuous_transaction = 0;
    if (!is_jog) {
        return 1;
    }
    if (!v5_native_motion_parameters_resolve_jog(parameters, request, code, code_cap)) {
        return 0;
    }
    if (request->kind == V5_COMMAND_JOG_CONTINUOUS) {
        if (!v5_jog_watchdog_refresh(
                watchdog,
                request->text_value[0],
                &new_continuous_transaction)) {
            snprintf(code, code_cap, "%s", "JOG_WATCHDOG_AXIS_CONFLICT");
            return 0;
        }
        if (new_continuous_transaction &&
            v5_linuxcncrsh_send_line(
                watchdog->config,
                "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT) {
            v5_jog_watchdog_clear(watchdog, request->text_value[0]);
            snprintf(code, code_cap, "%s", "JOG_MANUAL_MODE_FAILED");
            return 0;
        }
    } else if (request->kind == V5_COMMAND_JOG_INCREMENT &&
               v5_linuxcncrsh_send_line(
                   watchdog->config,
                   "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT) {
        snprintf(code, code_cap, "%s", "JOG_MANUAL_MODE_FAILED");
        return 0;
    }
    return 1;
}

static void v5_jog_watchdog_complete_request(
    V5JogWatchdog *watchdog,
    const V5CommandRequest *request,
    int status)
{
    if (!watchdog || !request || !request->text_value) {
        return;
    }
    if (request->kind == V5_COMMAND_JOG_CONTINUOUS &&
        status != V5_LINUXCNCRSH_SEND_SENT) {
        v5_jog_watchdog_clear(watchdog, request->text_value[0]);
    } else if (request->kind == V5_COMMAND_JOG_STOP &&
               status == V5_LINUXCNCRSH_SEND_SENT) {
        v5_jog_watchdog_clear(watchdog, request->text_value[0]);
        (void)v5_linuxcncrsh_send_line(watchdog->config, "Set Teleop_Enable On");
    }
}

static void v5_jog_watchdog_join(V5JogWatchdog *watchdog)
{
    if (!watchdog || !watchdog->started) {
        return;
    }
    (void)pthread_join(watchdog->thread, 0);
    pthread_mutex_destroy(&watchdog->state_lock);
    watchdog->started = 0;
}

#endif
