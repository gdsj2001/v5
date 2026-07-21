#define _POSIX_C_SOURCE 200809L

#include "v5_remote_ui_native_packer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t frame[4u * 4u * 4u];
    uint8_t output[32u];
    uint8_t expected[32u];
    V5RemoteUiPackRect rects[2] = {
        {1u, 1u, 2u, 2u},
        {0u, 3u, 4u, 1u},
    };
    V5RemoteUiPackRect invalid = {3u, 3u, 2u, 1u};
    V5RemoteUiPacker *packer = NULL;
    FILE *file = NULL;
    size_t output_size = 0u;
    size_t cursor = 0u;
    size_t row;
    size_t index;
    int result = 1;

    for (index = 0u; index < sizeof(frame); ++index)
        frame[index] = (uint8_t)(index + 1u);
    file = tmpfile();
    if (file == NULL || fwrite(frame, 1u, sizeof(frame), file) != sizeof(frame) || fflush(file) != 0)
        goto done;
    packer = v5_remote_ui_packer_open(fileno(file), sizeof(frame));
    if (packer == NULL || v5_remote_ui_packer_abi_version() != V5_REMOTE_UI_PACKER_ABI_VERSION)
        goto done;
    for (row = 0u; row < 2u; ++row) {
        memcpy(expected + cursor, frame + ((1u + row) * 16u) + 4u, 8u);
        cursor += 8u;
    }
    memcpy(expected + cursor, frame + 3u * 16u, 16u);
    cursor += 16u;
    if (v5_remote_ui_packer_pack(
            packer, 4u, 4u, 16u, rects, 2u, output, sizeof(output), &output_size) != 0 ||
        output_size != cursor || memcmp(output, expected, cursor) != 0)
        goto done;
    if (v5_remote_ui_packer_pack(
            packer, 4u, 4u, 16u, &invalid, 1u, output, sizeof(output), &output_size) >= 0)
        goto done;
    result = 0;

done:
    v5_remote_ui_packer_close(packer);
    if (file != NULL)
        fclose(file);
    if (result == 0)
        puts("v5 remote UI native packer smoke: ok");
    return result;
}
