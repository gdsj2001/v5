#include "v5_linuxcncrsh_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

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

static void poll_sleep_ms(unsigned int delay_ms)
{
#ifndef _WIN32
    usleep(delay_ms * 1000U);
#else
    (void)delay_ms;
#endif
}

static int ensure_machine_on(const V5LinuxcncrshConfig *config)
{
    char transcript[1024];
    int enabled = 0;
    unsigned int attempt;
    V5LinuxcncrshSendStatus status;

    transcript[0] = '\0';
    if (!v5_linuxcncrsh_probe_machine_enabled(config, &enabled, transcript, sizeof(transcript))) {
        fprintf(stderr, "machine state probe failed before Machine On\n");
        return 0;
    }
    if (enabled) {
        printf("machine already on\n%s\n", transcript);
        return 1;
    }

    status = v5_linuxcncrsh_send_machine_on_sequence(config);
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        fprintf(stderr, "machine on send failed status=%d state=%s\n", (int)status, transcript);
        return 0;
    }

    for (attempt = 0U; attempt < 30U; ++attempt) {
        transcript[0] = '\0';
        if (v5_linuxcncrsh_probe_machine_enabled(config, &enabled, transcript, sizeof(transcript)) && enabled) {
            printf("machine on confirmed\n%s\n", transcript);
            return 1;
        }
        poll_sleep_ms(100U);
    }

    fprintf(stderr, "machine on not confirmed after Set Machine On state=%s\n", transcript);
    return 0;
}

int main(int argc, char **argv)
{
    V5LinuxcncrshConfig config;
    char transcript[1024];
    int i;
    int machine_on = 0;

    memset(&config, 0, sizeof(config));
    config.host = "127.0.0.1";
    config.port = 5007U;
    config.connect_password = "EMC";
    config.client_name = "v5_probe";
    config.timeout_ms = 1000U;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            config.host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.port = (unsigned short)parse_u32(argv[++i], config.port);
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            config.connect_password = argv[++i];
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            config.timeout_ms = parse_u32(argv[++i], config.timeout_ms);
        } else if (strcmp(argv[i], "--machine-on") == 0) {
            machine_on = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: v5_linuxcncrsh_probe [--host 127.0.0.1] [--port 5007] [--password TEXT] [--timeout-ms 1000] [--machine-on]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (machine_on) {
        return ensure_machine_on(&config) ? 0 : 1;
    }

    if (!v5_linuxcncrsh_probe_machine(&config, transcript, sizeof(transcript))) {
        fprintf(stderr, "linuxcncrsh machine probe failed host=%s port=%u\n", config.host, config.port);
        return 1;
    }
    printf("linuxcncrsh machine probe ok host=%s port=%u\n%s\n", config.host, config.port, transcript);
    return 0;
}
