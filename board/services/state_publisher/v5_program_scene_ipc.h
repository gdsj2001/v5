#ifndef V5_PROGRAM_SCENE_IPC_H
#define V5_PROGRAM_SCENE_IPC_H

#include "v5_status_shm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_PROGRAM_SCENE_REQUEST_PATH "/run/v5_program_scene_request.sock"
#define V5_PROGRAM_SCENE_REQUEST_MAGIC 0x56355351u
#define V5_PROGRAM_SCENE_REQUEST_VERSION 2u
#define V5_PROGRAM_SCENE_WCS_COUNT 9u
#define V5_PROGRAM_SCENE_REQUEST_RETRY_LIMIT 3u
#define V5_PROGRAM_SCENE_REQUEST_RETRY_BASE_MS 100u

enum {
    V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL = 1u,
    V5_PROGRAM_SCENE_MESSAGE_VIEW_UPDATE = 2u,
};

typedef struct V5ProgramSceneRequest {
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t point_count;
    uint64_t program_source_identity;
    uint64_t program_generation;
    uint64_t view_generation;
    uint64_t fit_generation;
    uint32_t plane;
    uint32_t program_wcs_mask;
    float width;
    float height;
    float scale;
    float sine;
    float cosine;
    float pan_x;
    float pan_y;
    uint32_t page_visible;
    uint32_t reserved;
    V5StatusPoint points[V5_STATUS_SCENE_POINT_COUNT];
    int8_t wcs_index[V5_STATUS_SCENE_POINT_COUNT];
    uint8_t break_before[V5_STATUS_SCENE_POINT_COUNT];
} V5ProgramSceneRequest;

typedef struct V5ProgramSceneRequestServer {
    int fd;
    int bound;
} V5ProgramSceneRequestServer;

void v5_program_scene_request_init(V5ProgramSceneRequest *request);
int v5_program_scene_request_valid(const V5ProgramSceneRequest *request);
int v5_program_scene_request_merge(
    V5ProgramSceneRequest *current,
    const V5ProgramSceneRequest *candidate);
void v5_program_scene_request_prepare_transport(
    V5ProgramSceneRequest *request,
    const V5StatusDisplayScene *acknowledged_scene);
unsigned int v5_program_scene_request_retry_delay_ms(
    unsigned int retry_count);
uint64_t v5_program_scene_source_identity(const char *sha256);
int v5_program_scene_request_publish(
    const char *path,
    const V5ProgramSceneRequest *request);
void v5_program_scene_request_server_init(V5ProgramSceneRequestServer *server);
int v5_program_scene_request_server_open(
    V5ProgramSceneRequestServer *server,
    const char *path);
int v5_program_scene_request_server_drain_latest(
    V5ProgramSceneRequestServer *server,
    V5ProgramSceneRequest *request);
void v5_program_scene_request_server_close(
    V5ProgramSceneRequestServer *server,
    const char *path);

#ifdef __cplusplus
}
#endif

#endif
