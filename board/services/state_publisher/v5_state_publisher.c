#include "v5_state_publisher_service.h"
#include "v5_native_rtcp_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int main(int argc, char **argv)
{
    const char *path = V5_STATUS_SHM_PATH;
    unsigned int frames = 1u;
    unsigned int interval_ms = V5_STATE_PUBLISHER_INTERVAL_MS;
    int unlink_after = 0;
    int i;
    V5StatusShmFrame readback;
    V5NativeReadback rtcp_readback;
    V5StatePublisherReport report = {0};

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = parse_u32(argv[++i], frames);
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            interval_ms = parse_u32(argv[++i], interval_ms);
        } else if (strcmp(argv[i], "--daemon") == 0) {
            frames = 0u;
        } else if (strcmp(argv[i], "--unlink-after") == 0) {
            unlink_after = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: v5_state_publisher_smoke [--path PATH] [--frames N|--daemon] [--interval-ms 30] [--unlink-after]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!v5_state_publisher_run_loop(path, interval_ms, frames, &report)) {
        return 1;
    }
    if (!v5_status_shm_read_from_path(path, &readback)) {
        return 3;
    }
    if (!report.rtcp_status_published) {
        fprintf(stderr, "rtcp status block was not published\n");
        return 4;
    }
    if (!report.sample_available && readback.typed_valid_mask != 0U) {
        fprintf(stderr, "unavailable native sample must not publish valid default values: mask=0x%08x\n", readback.typed_valid_mask);
        return 6;
    }
    v5_native_readback_init(&rtcp_readback);
    if (v5_native_rtcp_status_read(0, V5_NATIVE_RTCP_STATUS_DEFAULT_MAX_AGE_MS, &rtcp_readback) ||
        v5_native_readback_rtcp_known(&rtcp_readback)) {
        fprintf(stderr, "invalid rtcp status block was accepted as actual\n");
        return 5;
    }
    printf(
        "v5 state publisher shm: version=%u valid_mask=0x%08x flags=0x%08x sample_available=%d rtcp_status_published=%u frames=%u interval_ms=%u bytes=%u path=%s\n",
        readback.version,
        readback.typed_valid_mask,
        readback.flags,
        report.sample_available,
        report.rtcp_status_published,
        report.publish_count,
        report.interval_ms,
        readback.total_size,
        path);
    if (unlink_after) {
        unlink(path);
    }
    return 0;
}
