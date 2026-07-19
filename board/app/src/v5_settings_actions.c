#include "v5_settings_actions.h"
#include "v5_settings_action_ipc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define V5_SETTINGS_RUN_DIR "/run/8ax_v5_product_ui"
#define V5_SETTINGS_ACTIOND_SOCKET V5_SETTINGS_RUN_DIR "/settings_actiond.sock"

typedef struct V5SettingsActionSpec {
    V5MainPageActionKind action;
    const char *name;
    const char *owner;
    const char *daemon_action;
    const char *result_path;
    int supported;
} V5SettingsActionSpec;

static char gLastActionMessage[160];

static const V5SettingsActionSpec kSpecs[] = {
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_DNA_REGISTER,
        "settings_dna_register",
        "auth_download",
        "device_dna_register",
        "/run/8ax_v5_auth_download/device_dna_register_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_AUTH_DOWNLOAD,
        "settings_auth_download",
        "auth_download",
        "device_authorization_download",
        "/run/8ax_v5_auth_download/device_authorization_download_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_SERVER_DOWNLOAD,
        "settings_server_download",
        "auth_download",
        "drive_profile_server_download",
        "/run/8ax_v5_auth_download/drive_profile_server_download_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_SCAN,
        "settings_scan",
        "drive_profile",
        "drive_scan_slaves",
        "/run/8ax_v5_drive/drive_scan_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_DRIVE_RESET,
        "settings_drive_reset",
        "drive_profile",
        "drive_factory_reset",
        "/run/8ax_v5_drive/drive_factory_reset_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_READ,
        "settings_read",
        "drive_profile",
        "drive_parameter_read",
        "/run/8ax_v5_drive/drive_read_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_FAULT_RESET,
        "settings_fault_reset",
        "drive_profile",
        "drive_fault_reset",
        "/run/8ax_v5_drive/drive_fault_reset_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE,
        "settings_set_drive",
        "drive_profile",
        "drive_set_parameters",
        "/run/8ax_v5_drive/drive_set_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_SAVE_RETURN,
        "settings_save_and_restart",
        "settings_restart",
        "settings_save_and_restart",
        "/run/8ax_v5_product_ui/settings_save_restart_result.json",
        1,
    },
};

static double monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static const V5SettingsActionSpec *find_spec(V5MainPageActionKind action)
{
    size_t i;
    for (i = 0; i < sizeof(kSpecs) / sizeof(kSpecs[0]); ++i) {
        if (kSpecs[i].action == action) {
            return &kSpecs[i];
        }
    }
    return 0;
}

static void write_json_text(FILE *fp, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    fputc('"', fp);
    while (*p) {
        if (*p == '"' || *p == '\\') {
            fputc('_', fp);
        } else if (*p >= 32U && *p < 127U) {
            fputc((int)*p, fp);
        }
        ++p;
    }
    fputc('"', fp);
}

static void log_action_event(const V5SettingsActionSpec *spec, int accepted, const char *message)
{
    FILE *fp;
    mkdir(V5_SETTINGS_RUN_DIR, 0755);
    fp = fopen(V5_SETTINGS_RUN_DIR "/settings_action_events.jsonl", "ab");
    if (!fp) {
        return;
    }
    fprintf(fp,
            "{\"schema\":\"v5.settings_action.v1\",\"source\":\"v5_lvgl_shell\","
            "\"time_monotonic_s\":%.6f,\"action\":",
            monotonic_seconds());
    write_json_text(fp, spec ? spec->name : "");
    fprintf(fp, ",\"owner\":");
    write_json_text(fp, spec ? spec->owner : "");
    fprintf(fp, ",\"supported\":%s,\"accepted\":%s,\"result_path\":",
            (spec && spec->supported) ? "true" : "false",
            accepted ? "true" : "false");
    write_json_text(fp, spec ? spec->result_path : "");
    fprintf(fp, ",\"message\":");
    write_json_text(fp, message ? message : "");
    fprintf(fp, "}\n");
    fclose(fp);
}

static void format_ipc_error(V5SettingsActionIpcError error, char *message, size_t message_size)
{
    const char *detail = strerror(errno);
    switch (error) {
    case V5_SETTINGS_ACTION_IPC_ERROR_SOCKET:
        snprintf(message, message_size, "%s", detail);
        break;
    case V5_SETTINGS_ACTION_IPC_ERROR_CONNECT:
        snprintf(message, message_size, "settings actiond unavailable: %s", detail);
        break;
    case V5_SETTINGS_ACTION_IPC_ERROR_WRITE:
        snprintf(message, message_size, "settings actiond write failed: %s", detail);
        break;
    default:
        snprintf(message, message_size, "settings actiond no response");
        break;
    }
}

static int send_daemon_request(const V5SettingsActionSpec *spec, char *message, size_t message_size)
{
    char request[256];
    char response[256];
    V5SettingsActionIpcError error;

    if (!spec || !spec->daemon_action || !spec->daemon_action[0]) {
        snprintf(message, message_size, "missing action");
        return 0;
    }

    snprintf(request, sizeof(request), "{\"action\":\"%s\"}\n", spec->daemon_action);
    if (!v5_settings_action_ipc_request(V5_SETTINGS_ACTIOND_SOCKET,
                                        request,
                                        1000U,
                                        response,
                                        sizeof(response),
                                        &error)) {
        format_ipc_error(error, message, message_size);
        return 0;
    }
    if (v5_settings_action_ipc_response_accepted(response)) {
        snprintf(message, message_size, "accepted by resident actiond");
        return 1;
    }
    snprintf(message, message_size, "settings actiond rejected");
    return 0;
}

static int send_status_request(char *response, size_t response_size)
{
    const char *request = "{\"query\":\"last_status\"}\n";
    if (!response || response_size == 0U) {
        return 0;
    }
    return v5_settings_action_ipc_request(V5_SETTINGS_ACTIOND_SOCKET,
                                          request,
                                          50U,
                                          response,
                                          response_size,
                                          0);
}

static int settings_action_run_id_is_valid(const char *run_id)
{
    size_t i;
    if (!run_id || !run_id[0] || strlen(run_id) >= 64U) {
        return 0;
    }
    for (i = 0U; run_id[i]; ++i) {
        char ch = run_id[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') || ch == '-' || ch == '_')) {
            return 0;
        }
    }
    return 1;
}

int v5_settings_action_cancel(const char *run_id)
{
    char request[160];
    char response[512];
    if (!settings_action_run_id_is_valid(run_id)) {
        return 0;
    }
    snprintf(request, sizeof(request), "{\"action\":\"job_cancel\",\"run_id\":\"%s\"}\n", run_id);
    if (!v5_settings_action_ipc_request(V5_SETTINGS_ACTIOND_SOCKET,
                                        request,
                                        1000U,
                                        response,
                                        sizeof(response),
                                        0)) {
        return 0;
    }
    return v5_settings_action_ipc_response_accepted(response);
}

static int json_bool_value(const char *json, const char *key)
{
    const char *p;
    char pattern[80];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key ? key : "");
    p = strstr(json ? json : "", pattern);
    if (!p) {
        return 0;
    }
    p = strchr(p, ':');
    if (!p) {
        return 0;
    }
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return strncmp(p, "true", 4) == 0;
}

static void json_string_value(const char *json, const char *key, char *out, size_t out_size)
{
    const char *p;
    char pattern[80];
    size_t n = 0U;
    int escaped = 0;
    if (!out || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    snprintf(pattern, sizeof(pattern), "\"%s\"", key ? key : "");
    p = strstr(json ? json : "", pattern);
    if (!p) {
        return;
    }
    p = strchr(p, ':');
    if (!p) {
        return;
    }
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != '"') {
        return;
    }
    ++p;
    while (*p && n + 1U < out_size) {
        char ch = *p++;
        if (escaped) {
            switch (ch) {
            case 'n': out[n++] = '\n'; break;
            case 'r': out[n++] = '\r'; break;
            case 't': out[n++] = '\t'; break;
            case '"': out[n++] = '"'; break;
            case '\\': out[n++] = '\\'; break;
            default: out[n++] = ch; break;
            }
            escaped = 0;
            continue;
        }
        if (ch == '\\') {
            escaped = 1;
            continue;
        }
        if (ch == '"') {
            break;
        }
        out[n++] = ch;
    }
    out[n] = '\0';
}

int v5_settings_action_restart_commit(const char *run_id)
{
    char request[192];
    char response[512];
    char response_run_id[64];
    char response_code[72];
    if (!settings_action_run_id_is_valid(run_id)) {
        return 0;
    }
    snprintf(request,
             sizeof(request),
             "{\"action\":\"settings_save_restart_commit\",\"run_id\":\"%s\"}\n",
             run_id);
    if (!v5_settings_action_ipc_request(V5_SETTINGS_ACTIOND_SOCKET,
                                        request,
                                        1000U,
                                        response,
                                        sizeof(response),
                                        0) ||
        !v5_settings_action_ipc_response_accepted(response)) {
        return 0;
    }
    json_string_value(response, "run_id", response_run_id, sizeof(response_run_id));
    json_string_value(response, "code", response_code, sizeof(response_code));
    return strcmp(response_run_id, run_id) == 0 &&
           strcmp(response_code, "SETTINGS_SAVE_RESTART_COMMIT_ACK") == 0;
}


int v5_settings_axis_zero_start(const char *axis,
                                const char *driver_mode,
                                const char *target_scope,
                                const char *apply_mode,
                                const char *slave_index,
                                const char *home_offset,
                                V5SettingsActionResult *result)
{
    V5SettingsActionSpec spec = {
        V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE,
        "settings_axis_zero",
        "drive_profile",
        "settings_axis_zero",
        "/run/8ax_v5_drive/settings_axis_zero_result.json",
        1,
    };
    char request[512];
    char response_message[160];
    char response[256];
    V5SettingsActionIpcError error;

    if (result) {
        memset(result, 0, sizeof(*result));
    }
    snprintf(request,
             sizeof(request),
             "{\"action\":\"settings_axis_zero\",\"axis\":\"%s\",\"driver_mode\":\"%s\",\"target_scope\":\"%s\",\"apply_mode\":\"%s\",\"slave_index\":\"%s\",\"home_offset\":\"%s\",\"tolerance_mm_deg\":0.1}\n",
             axis ? axis : "",
             driver_mode ? driver_mode : "",
             target_scope ? target_scope : "",
             apply_mode ? apply_mode : "",
             slave_index ? slave_index : "",
             home_offset ? home_offset : "");

    response_message[0] = '\0';
    if (!v5_settings_action_ipc_request(V5_SETTINGS_ACTIOND_SOCKET,
                                        request,
                                        1000U,
                                        response,
                                        sizeof(response),
                                        &error)) {
        format_ipc_error(error, response_message, sizeof(response_message));
        log_action_event(&spec, 0, response_message);
        return 0;
    }
    if (!v5_settings_action_ipc_response_accepted(response)) {
        snprintf(response_message, sizeof(response_message), "settings actiond rejected");
        log_action_event(&spec, 0, response_message);
        return 0;
    }
    snprintf(response_message, sizeof(response_message), "accepted by resident actiond");
    log_action_event(&spec, 1, response_message);
    if (result) {
        result->started = 1;
        result->supported = 1;
        result->name = spec.name;
        result->daemon_action = spec.daemon_action;
        result->owner = spec.owner;
        result->result_path = spec.result_path;
        result->message = response_message;
    }
    return 1;
}

int v5_settings_action_poll_status(V5SettingsActionStatus *status)
{
    char response[2048];
    if (!status) {
        return 0;
    }
    memset(status, 0, sizeof(*status));
    if (!send_status_request(response, sizeof(response))) {
        return 0;
    }
    status->available = json_bool_value(response, "available");
    status->busy = json_bool_value(response, "busy");
    status->ok = json_bool_value(response, "ok");
    json_string_value(response, "action", status->action, sizeof(status->action));
    json_string_value(response, "run_id", status->run_id, sizeof(status->run_id));
    json_string_value(response, "code", status->code, sizeof(status->code));
    json_string_value(response, "message_cn", status->message, sizeof(status->message));
    json_string_value(response, "vpsDistributionId",
                      status->vps_distribution_id,
                      sizeof(status->vps_distribution_id));
    json_string_value(response, "result_path", status->result_path, sizeof(status->result_path));
    json_string_value(response, "axis", status->axis, sizeof(status->axis));
    json_string_value(response, "state", status->state, sizeof(status->state));
    status->cancel_allowed = json_bool_value(response, "cancel_allowed");
    status->restart_required = json_bool_value(response, "restart_required");
    status->restart_deferred = json_bool_value(response, "restart_deferred");
    status->backend_restart_required = json_bool_value(response, "backend_restart_required");
    return status->available;
}

int v5_settings_action_start(V5MainPageActionKind action, V5SettingsActionResult *result)
{
    const V5SettingsActionSpec *spec = find_spec(action);
    int accepted = 0;
    char *message = gLastActionMessage;
    message[0] = '\0';

    if (result) {
        memset(result, 0, sizeof(*result));
    }
    if (!spec) {
        return 0;
    }

    accepted = send_daemon_request(spec, message, sizeof(gLastActionMessage));
    log_action_event(spec, accepted, message);
    if (result) {
        result->started = accepted;
        result->supported = spec->supported;
        result->name = spec->name;
        result->daemon_action = spec->daemon_action;
        result->owner = spec->owner;
        result->result_path = spec->result_path;
        result->message = message;
    }
    return accepted;
}
