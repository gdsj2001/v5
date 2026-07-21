#include "v5_command_gate_ipc.h"
#include "v5_command_gate_server_io.h"
#include "v5_command_gate_server_response.h"
#include "v5_command_gate_server_transport.h"
#include "v5_command_gate_server_internal.h"
#include "v5_command_gate_validator.h"
#include "v5_estop_clean_state.h"
#include "v5_drive_write_window.h"
#include "v5_linuxcncrsh_client.h"
#include "v5_jog_watchdog.h"
#include "v5_native_axis_zero_position.h"
#include "v5_native_axis_zero_live.h"
#include "v5_native_first_point.h"
#include "v5_native_home.h"
#include "v5_native_home_mapping.h"
#include "v5_native_home_runtime_owner.h"
#include "v5_native_hal_owner_client.h"
#include "v5_native_motion_parameters.h"
#include "v5_native_rtcp_control.h"
#include "v5_native_safety.h"
#include "v5_native_work_zero.h"
#include "v5_process_residency.h"
#include "v5_settings_apply.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/un.h>
#include <unistd.h>

volatile sig_atomic_t g_stop_requested;
pthread_mutex_t g_linuxcncrsh_lock = PTHREAD_MUTEX_INITIALIZER;
V5LinuxcncrshConfig g_linuxcncrsh_config = {"127.0.0.1", 5007U, "EMC", "v5_command_gate", 1000U};
static char g_host[64] = "127.0.0.1";
static char g_password[64] = "EMC";
char g_ini_path[256];
char g_settings_project_root[256] = "/opt/8ax/v5";
char g_settings_runtime_path[256] = "/opt/8ax/phase0_bus5/settings_runtime.json";
char g_pulse_contract_path[256] = "/opt/8ax/v5/linuxcnc/components/step_ip_v1_5.contract.json";
static char g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] = V5_COMMAND_GATE_SOCKET_PATH;
V5NativeMotionParameters g_motion_parameters;
V5JogWatchdog g_jog_watchdog;
int g_axis_slave_mapping_status_available;
int g_axis_slave_mapping_applicable;
int g_axis_slave_mapping_valid;
unsigned int g_axis_slave_mapping_generation;
char g_axis_slave_mapping_code[64];

static void handle_transport_frame(
    const V5CommandGateIpcRequestFrame *request,
    V5CommandGateIpcResponseFrame *response,
    void *context)
{
    (void)context;
    handle_frame(request, response);
}
static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(g_socket_path, sizeof(g_socket_path), argv[++i]);
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(g_host, sizeof(g_host), argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_linuxcncrsh_config.port = (unsigned short)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(g_password, sizeof(g_password), argv[++i]);
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            g_linuxcncrsh_config.timeout_ms = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ini") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(g_ini_path, sizeof(g_ini_path), argv[++i]);
        } else if (strcmp(argv[i], "--settings-project-root") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(
                g_settings_project_root, sizeof(g_settings_project_root), argv[++i]);
        } else if (strcmp(argv[i], "--settings-runtime") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(
                g_settings_runtime_path, sizeof(g_settings_runtime_path), argv[++i]);
        } else if (strcmp(argv[i], "--pulse-contract") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(
                g_pulse_contract_path, sizeof(g_pulse_contract_path), argv[++i]);
        }
    }
    g_linuxcncrsh_config.host = g_host;
    g_linuxcncrsh_config.connect_password = g_password;
    g_linuxcncrsh_config.client_name = "v5_command_gate";
    if (g_linuxcncrsh_config.timeout_ms == 0U) {
        g_linuxcncrsh_config.timeout_ms = 1000U;
    }
}

int main(int argc, char **argv)
{
    V5CommandGateServerTransport transport;
    int home_runtime_owner_loaded;
    char motion_code[64];
    parse_args(argc, argv);
    if (!v5_process_residency_lock("v5_command_gate_server")) {
        return 3;
    }
    if (!v5_native_motion_parameters_load(
            g_ini_path, &g_motion_parameters, motion_code, sizeof(motion_code))) {
        fprintf(stderr, "v5_command_gate_server motion parameter preload failed: %s ini=%s\n",
                motion_code, g_ini_path);
        return 5;
    }
    load_axis_slave_mapping_status();
    home_runtime_owner_loaded = v5_native_motion_parameters_load_runtime_owner(
            g_settings_project_root,
            g_settings_runtime_path,
            g_pulse_contract_path,
            &g_motion_parameters,
            motion_code,
            sizeof(motion_code));
    if (!home_runtime_owner_loaded) {
        fprintf(stderr,
                "v5_command_gate_server Home runtime owner unavailable: %s; "
                "dependent Home actions remain fail-closed\n",
                motion_code);
    }
    if (g_axis_slave_mapping_applicable && g_axis_slave_mapping_valid &&
        !v5_native_home_mapping_project(
            &g_motion_parameters, 0, motion_code, sizeof(motion_code))) {
        g_axis_slave_mapping_status_available = 1;
        g_axis_slave_mapping_valid = 0;
        g_axis_slave_mapping_generation = 0U;
        snprintf(
            g_axis_slave_mapping_code,
            sizeof(g_axis_slave_mapping_code),
            "%s",
            motion_code[0]
                ? motion_code
                : "NATIVE_BUS_MAPPING_PROJECTION_FAILED");
        fprintf(stderr,
                "v5_command_gate_server native BUS mapping projection failed: %s; "
                "BUS motion remains fail-closed\n",
                motion_code);
    }
    if (!v5_linuxcncrsh_gate_preconnect(&g_linuxcncrsh_config)) {
        fprintf(stderr, "v5_command_gate_server linuxcncrsh preconnect failed: %s:%u\n",
                g_linuxcncrsh_config.host, g_linuxcncrsh_config.port);
        return 4;
    }
    if (!v5_jog_watchdog_start(
            &g_jog_watchdog, &g_linuxcncrsh_config,
            &g_linuxcncrsh_lock, &g_stop_requested)) {
        fprintf(stderr, "v5_command_gate_server Jog watchdog failed to start\n");
        return 7;
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);
    if (!v5_command_gate_server_transport_open(
            &transport, g_socket_path, &g_stop_requested, 250U,
            handle_transport_frame, 0)) {
        perror("v5_command_gate_server listen");
        return 1;
    }
    printf("v5_command_gate_server running socket=%s linuxcncrsh=%s:%u\n", g_socket_path, g_linuxcncrsh_config.host, g_linuxcncrsh_config.port);
    fflush(stdout);
    v5_command_gate_server_transport_serve(&transport);
    v5_command_gate_server_transport_close(&transport);
    g_stop_requested = 1;
    v5_jog_watchdog_join(&g_jog_watchdog);
    unlink(g_socket_path);
    return 0;
}
