#include "v5_state_publisher_service.h"
#include "v5_process_residency.h"

#include <signal.h>
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

static void handle_stop_signal(int signo)
{
    (void)signo;
    v5_state_publisher_request_stop();
}

int main(int argc, char **argv)
{
    const char *path = V5_STATUS_SHM_PATH;
    unsigned int frames = 0u;
    unsigned int interval_ms = V5_STATE_PUBLISHER_INTERVAL_MS;
    int i;
    V5StatePublisherReport report = {0};

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = parse_u32(argv[++i], frames);
        } else if (strcmp(argv[i], "--once") == 0) {
            frames = 1u;
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            interval_ms = parse_u32(argv[++i], interval_ms);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: v5_state_publisher [--path PATH] [--once|--frames N] [--interval-ms 30]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!v5_process_residency_lock("v5_state_publisher")) {
        return 3;
    }

    signal(SIGTERM, handle_stop_signal);
    signal(SIGINT, handle_stop_signal);

    if (!v5_state_publisher_run_loop(path, interval_ms, frames, &report)) {
        return 1;
    }

    printf(
        "v5 state publisher: frames=%u interval_ms=%u path=%s valid_mask=0x%08x flags=0x%08x sample_available=%d\n",
        report.publish_count,
        report.interval_ms,
        report.path ? report.path : path,
        report.valid_mask,
        report.frame_flags,
        report.sample_available);
    return 0;
}
