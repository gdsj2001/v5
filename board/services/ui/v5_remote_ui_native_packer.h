#ifndef V5_REMOTE_UI_NATIVE_PACKER_H
#define V5_REMOTE_UI_NATIVE_PACKER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_REMOTE_UI_PACKER_ABI_VERSION 1u

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} V5RemoteUiPackRect;

typedef struct V5RemoteUiPacker V5RemoteUiPacker;

uint32_t v5_remote_ui_packer_abi_version(void);
V5RemoteUiPacker *v5_remote_ui_packer_open(int framebuffer_fd, size_t frame_size);
void v5_remote_ui_packer_close(V5RemoteUiPacker *packer);
int v5_remote_ui_packer_pack(
    V5RemoteUiPacker *packer,
    uint32_t width,
    uint32_t height,
    size_t stride,
    const V5RemoteUiPackRect *rects,
    size_t rect_count,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif
