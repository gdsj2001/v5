#include "v5_linuxcncrsh_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int parse_u32(const char *text, unsigned int default_value)
{
    char *end = 0;
    unsigned long value;
    if (!text || !text[0]) {
        return default_value;
    }
    value = strtoul(text, &end, 10);
    if (!end || *end || value > 0xfffffffful) {
        return default_value;
    }
    return (unsigned int)value;
}

static void usage(void)
{
    printf("usage: v5_linuxcncrsh_golden_run --program /tmp/v5_golden/cc.ngc [--start] [--host 127.0.0.1] [--port 5007] [--password TEXT] [--timeout-ms 1000]\n");
    printf("opens the exact LinuxCNC native program path through command gate; --start also requires V5_ALLOW_MOTION=1\n");
}

static int prepare_program_open(
    const char *program_path,
    V5CommandPrepared *prepared,
    V5CommandRequest *request)
{
    if (!program_path || !program_path[0] || !prepared || !request) {
        return 0;
    }
    memset(request, 0, sizeof(*request));
    request->kind = V5_COMMAND_PROGRAM_OPEN;
    request->text_value = program_path;
    return v5_command_gate_prepare(request, prepared);
}

static int prepare_start(
    const char *program_path,
    V5CommandPrepared *prepared,
    V5CommandRequest *request)
{
    if (!program_path || !program_path[0] || !prepared || !request) {
        return 0;
    }
    memset(request, 0, sizeof(*request));
    request->kind = V5_COMMAND_START;
    request->text_value = program_path;
    return v5_command_gate_prepare(request, prepared);
}

static int send_prepared_or_report(
    const char *label,
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request)
{
    char line[384];
    V5LinuxcncrshSendStatus status;

    if (!v5_linuxcncrsh_format_line(prepared, request, line, sizeof(line))) {
        fprintf(stderr, "%s command format failed\n", label);
        return 0;
    }
    status = v5_linuxcncrsh_send_line(config, line);
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        fprintf(stderr, "%s command send failed status=%d line=%s\n", label, (int)status, line);
        return 0;
    }
    printf("%s sent: %s\n", label, line);
    return 1;
}

int main(int argc, char **argv)
{
    V5LinuxcncrshConfig config;
    V5CommandPrepared prepared;
    V5CommandRequest request;
    const char *program_path = 0;
    int start = 0;
    int i;

    memset(&config, 0, sizeof(config));
    config.host = "127.0.0.1";
    config.port = 5007U;
    config.connect_password = "EMC";
    config.client_name = "v5_golden";
    config.timeout_ms = 1000U;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--program") == 0 && i + 1 < argc) {
            program_path = argv[++i];
        } else if (strcmp(argv[i], "--start") == 0) {
            start = 1;
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            config.host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.port = (unsigned short)parse_u32(argv[++i], config.port);
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            config.connect_password = argv[++i];
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            config.timeout_ms = parse_u32(argv[++i], config.timeout_ms);
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!program_path || !program_path[0]) {
        fprintf(stderr, "--program is required\n");
        return 3;
    }
    if (start && (!getenv("V5_ALLOW_MOTION") || strcmp(getenv("V5_ALLOW_MOTION"), "1") != 0)) {
        fprintf(stderr, "V5_ALLOW_MOTION=1 is required for --start\n");
        return 4;
    }

    if (!start) {
        if (!prepare_program_open(program_path, &prepared, &request) ||
            !send_prepared_or_report("program_open", &config, &prepared, &request)) {
            return 5;
        }
        printf("golden program opened without start\n");
        return 0;
    }

    if (!prepare_start(program_path, &prepared, &request) ||
        !send_prepared_or_report("start", &config, &prepared, &request)) {
        return 6;
    }

    printf("golden start submitted\n");
    return 0;
}
