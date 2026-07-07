#include "v5_drive_profile_snapshot.h"

#include <stdio.h>
#include <string.h>

static const char *const kV5ProfileMaps[] = {
    "config/drive-profiles/public/driver_profile_map.json",
    "config/drive-profiles/private/535e661e9ea313143fed0d86e9d982368ca9a70c7062823e25560f34ceef7f9d_driver_profile_map.json",
};

static V5DriveProfileSnapshot g_snapshot = {
    "boot_closure_unloaded",
    0,
    0U,
    0U,
};

static int append_path(char *out, size_t cap, const char *root, const char *rel)
{
    int n;

    if (!out || cap == 0U || !rel) {
        return 0;
    }

    if (root && root[0]) {
        n = snprintf(out, cap, "%s/%s", root, rel);
    } else {
        n = snprintf(out, cap, "%s", rel);
    }
    return n > 0 && (size_t)n < cap;
}

static size_t count_token_in_file(const char *path, const char *token, int *opened)
{
    FILE *fp;
    char buf[4096];
    size_t token_len;
    size_t overlap = 0U;
    size_t count = 0U;

    if (opened) {
        *opened = 0;
    }
    if (!path || !token || !token[0]) {
        return 0U;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return 0U;
    }
    if (opened) {
        *opened = 1;
    }

    token_len = strlen(token);
    while (!feof(fp)) {
        size_t n = fread(buf + overlap, 1U, sizeof(buf) - overlap, fp);
        size_t total = overlap + n;
        size_t i;

        for (i = 0U; i + token_len <= total; ++i) {
            if (memcmp(buf + i, token, token_len) == 0) {
                ++count;
            }
        }

        if (token_len > 1U && total >= token_len - 1U) {
            overlap = token_len - 1U;
            memmove(buf, buf + total - overlap, overlap);
        } else {
            overlap = total;
        }
    }

    fclose(fp);
    return count;
}

void v5_drive_profile_snapshot_init(V5DriveProfileSnapshot *snapshot)
{
    if (!snapshot) {
        return;
    }

    *snapshot = g_snapshot;
}

void v5_drive_profile_snapshot_load(V5DriveProfileSnapshot *snapshot, const char *project_root)
{
    size_t i;
    size_t profiles = 0U;
    size_t maps = 0U;

    for (i = 0U; i < sizeof(kV5ProfileMaps) / sizeof(kV5ProfileMaps[0]); ++i) {
        char path[1024];
        int opened = 0;

        if (!append_path(path, sizeof(path), project_root, kV5ProfileMaps[i])) {
            continue;
        }
        profiles += count_token_in_file(path, "\"commands\"", &opened);
        if (opened) {
            ++maps;
        }
    }

    g_snapshot.source = "config/drive-profiles";
    g_snapshot.loaded = 1;
    g_snapshot.profile_count = profiles;
    g_snapshot.map_file_count = maps;

    if (snapshot) {
        *snapshot = g_snapshot;
    }
}

size_t v5_drive_profile_snapshot_count(void)
{
    return g_snapshot.profile_count;
}

size_t v5_drive_profile_snapshot_map_file_count(void)
{
    return g_snapshot.map_file_count;
}
