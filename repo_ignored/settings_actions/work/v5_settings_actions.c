#include "v5_settings_actions.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define V5_SETTINGS_RUN_DIR "/run/8ax_v5_product_ui"
#define V5_AUTH_SCRIPT_DIR "/usr/libexec/8ax/auth_download"
#define V5_DRIVE_SCRIPT_DIR "/usr/libexec/8ax/drive_profile"

typedef struct V5SettingsActionSpec {
    V5MainPageActionKind action;
    const char *name;
    const char *owner;
    const char *script;
    const char *arg1;
    const char *result_path;
    int supported;
} V5SettingsActionSpec;

static const V5SettingsActionSpec kSpecs[] = {
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_DNA_REGISTER,
        "device_dna_register",
        "auth_download",
        V5_AUTH_SCRIPT_DIR "/v5_device_dna_register.py",
        0,
        "/opt/8ax/drive-profiles/device_register_status.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_AUTH_DOWNLOAD,
        "device_authorization_download",
        "auth_download",
        V5_AUTH_SCRIPT_DIR "/v5_device_authorization_download.py",
        0,
        "/run/8ax_v5_auth_download/device_authorization_download_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_SERVER_DOWNLOAD,
        "drive_profile_server_download",
        "auth_download",
        V5_AUTH_SCRIPT_DIR "/v5_drive_profile_download.py",
        0,
        "/run/8ax_v5_auth_download/drive_profile_server_download_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_SCAN,
        "drive_scan_slaves",
        "drive_profile",
        V5_DRIVE_SCRIPT_DIR "/v5_drive_bus_action.py",
        "scan",
        "/run/8ax_v5_drive/drive_scan_result.json",
        1,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_DRIVE_RESET,
        "drive_factory_reset",
        "drive_profile",
        V5_DRIVE_SCRIPT_DIR "/v5_drive_bus_action.py",
        "factory-reset",
        "/run/8ax_v5_drive/drive_factory_reset_result.json",
        0,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_READ,
        "drive_parameter_read",
        "drive_profile",
        V5_DRIVE_SCRIPT_DIR "/v5_drive_bus_action.py",
        "read",
        "/run/8ax_v5_drive/drive_read_result.json",
        0,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_FAULT_RESET,
        "drive_fault_reset",
        "drive_profile",
        V5_DRIVE_SCRIPT_DIR "/v5_drive_bus_action.py",
        "fault-reset",
        "/run/8ax_v5_drive/drive_fault_reset_result.json",
        0,
    },
    {
        V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE,
        "drive_set_parameters",
        "drive_profile",
        V5_DRIVE_SCRIPT_DIR "/v5_drive_bus_action.py",
        "set-drive",
        "/run/8ax_v5_drive/drive_set_result.json",
        0,
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

static void log_action_event(const V5SettingsActionSpec *spec, int started, const char *message)
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
    fprintf(fp, ",\"supported\":%s,\"started\":%s,\"result_path\":",
            (spec && spec->supported) ? "true" : "false",
            started ? "true" : "false");
    write_json_text(fp, spec ? spec->result_path : "");
    fprintf(fp, ",\"message\":");
    write_json_text(fp, message ? message : "");
    fprintf(fp, "}\n");
    fclose(fp);
}

static int spawn_spec(const V5SettingsActionSpec *spec)
{
    pid_t pid;
    char log_path[256];
    int fd;

    if (!spec || !spec->script || !spec->script[0]) {
        return 0;
    }
    mkdir(V5_SETTINGS_RUN_DIR, 0755);
    snprintf(log_path, sizeof(log_path), V5_SETTINGS_RUN_DIR "/%s_last.log", spec->name);
    pid = fork();
    if (pid < 0) {
        return 0;
    }
    if (pid > 0) {
        return 1;
    }
    setsid();
    fd = open(log_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    if (spec->arg1 && spec->arg1[0]) {
        execl(spec->script, spec->script, "--action", spec->arg1, (char *)0);
    } else {
        execl(spec->script, spec->script, (char *)0);
    }
    _exit(127);
}

int v5_settings_action_start(V5MainPageActionKind action, V5SettingsActionResult *result)
{
    const V5SettingsActionSpec *spec = find_spec(action);
    int started = 0;
    const char *message = "unsupported settings action";
    if (result) {
        memset(result, 0, sizeof(*result));
    }
    if (!spec) {
        return 0;
    }
    if (spec->supported) {
        started = spawn_spec(spec);
        message = started ? "started" : strerror(errno);
    } else {
        (void)spawn_spec(spec);
        message = "fail-closed: canonical drive write/read gate not implemented";
    }
    log_action_event(spec, started, message);
    if (result) {
        result->started = started;
        result->supported = spec->supported;
        result->name = spec->name;
        result->owner = spec->owner;
        result->result_path = spec->result_path;
        result->message = message;
    }
    return spec->supported ? started : 0;
}
