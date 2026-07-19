#include "v5_program_scene_ipc.h"
#include "v5_toolpath_viewport.h"

#include <math.h>
#include <string.h>

uint64_t v5_program_scene_source_identity(const char *sha256)
{
    uint64_t value = 1469598103934665603ULL;
    const unsigned char *cursor = (const unsigned char *)sha256;
    if (!cursor || !cursor[0]) return 0ULL;
    while (*cursor) { value ^= (uint64_t)*cursor++; value *= 1099511628211ULL; }
    return value ? value : 1ULL;
}

void v5_program_scene_request_init(V5ProgramSceneRequest *request)
{
    const V5ToolpathViewport *viewport = v5_toolpath_viewport();
    if (!request) return;
    memset(request, 0, sizeof(*request));
    request->magic = V5_PROGRAM_SCENE_REQUEST_MAGIC;
    request->version = V5_PROGRAM_SCENE_REQUEST_VERSION;
    request->total_size = (uint32_t)sizeof(*request);
    request->view_generation = 1ULL;
    request->fit_generation = 1ULL;
    request->plane = V5_STATUS_SCENE_PLANE_3D;
    request->width = (float)viewport->width;
    request->height = (float)viewport->height;
    request->scale = 1.0f;
    request->cosine = 1.0f;
    request->page_visible = 1U;
    request->reserved = V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL;
}

int v5_program_scene_request_valid(const V5ProgramSceneRequest *request)
{
    uint32_t i;
    uint32_t axis;
    if (!request || request->magic != V5_PROGRAM_SCENE_REQUEST_MAGIC ||
        request->version != V5_PROGRAM_SCENE_REQUEST_VERSION ||
        request->total_size != sizeof(*request) ||
        request->point_count > V5_STATUS_SCENE_POINT_COUNT ||
        (request->reserved != V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL &&
         request->reserved != V5_PROGRAM_SCENE_MESSAGE_VIEW_UPDATE) ||
        (request->reserved == V5_PROGRAM_SCENE_MESSAGE_VIEW_UPDATE &&
         request->point_count != 0U) ||
        request->view_generation == 0ULL || request->fit_generation == 0ULL ||
        request->plane > V5_STATUS_SCENE_PLANE_3D ||
        request->page_visible > 1U ||
        (request->program_wcs_mask & ~((1U << V5_PROGRAM_SCENE_WCS_COUNT) - 1U)) != 0U ||
        !isfinite(request->width) || request->width <= 0.0f ||
        !isfinite(request->height) || request->height <= 0.0f ||
        !isfinite(request->scale) || request->scale <= 0.0f ||
        !isfinite(request->sine) || !isfinite(request->cosine) ||
        !isfinite(request->pan_x) || !isfinite(request->pan_y)) {
        return 0;
    }
    if (request->point_count > 0U &&
        (request->program_source_identity == 0ULL ||
         request->program_generation == 0ULL)) {
        return 0;
    }
    if (request->reserved != V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL) {
        return 1;
    }
    for (i = 0U; i < request->point_count; ++i) {
        if (request->wcs_index[i] < 0 ||
            request->wcs_index[i] >= (int8_t)V5_PROGRAM_SCENE_WCS_COUNT ||
            request->break_before[i] > 1U) {
            return 0;
        }
        for (axis = 0U; axis < V5_STATUS_AXIS_COUNT; ++axis) {
            if (!isfinite(request->points[i].axis[axis])) {
                return 0;
            }
        }
    }
    return 1;
}

#ifdef _WIN32

int v5_program_scene_request_publish(const char *path, const V5ProgramSceneRequest *request)
{ (void)path; return v5_program_scene_request_valid(request); }
void v5_program_scene_request_server_init(V5ProgramSceneRequestServer *server)
{ if (server) { server->fd = -1; server->bound = 0; } }
int v5_program_scene_request_server_open(V5ProgramSceneRequestServer *server, const char *path)
{ (void)path; if (!server) return 0; server->bound = 1; return 1; }
int v5_program_scene_request_server_drain_latest(V5ProgramSceneRequestServer *server, V5ProgramSceneRequest *request)
{ (void)server; (void)request; return 0; }
void v5_program_scene_request_server_close(V5ProgramSceneRequestServer *server, const char *path)
{ (void)path; v5_program_scene_request_server_init(server); }

#else

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char *scene_request_path(const char *path)
{
    return path && path[0] ? path : V5_PROGRAM_SCENE_REQUEST_PATH;
}

int v5_program_scene_request_publish(const char *path, const V5ProgramSceneRequest *request)
{
    struct sockaddr_un address;
    int fd;
    ssize_t sent;
    size_t send_size;
    const char *target = scene_request_path(path);
    if (!v5_program_scene_request_valid(request) || strlen(target) >= sizeof(address.sun_path)) return 0;
    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    send_size = request->reserved == V5_PROGRAM_SCENE_MESSAGE_VIEW_UPDATE ?
        offsetof(V5ProgramSceneRequest, points) : sizeof(*request);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, target, strlen(target) + 1U);
    sent = sendto(fd, request, send_size, MSG_DONTWAIT,
        (const struct sockaddr *)&address, sizeof(address));
    close(fd);
    return sent == (ssize_t)send_size;
}

void v5_program_scene_request_server_init(V5ProgramSceneRequestServer *server)
{
    if (!server) return;
    server->fd = -1;
    server->bound = 0;
}

int v5_program_scene_request_server_open(V5ProgramSceneRequestServer *server, const char *path)
{
    struct sockaddr_un address;
    const char *target = scene_request_path(path);
    int flags;
    if (!server || strlen(target) >= sizeof(address.sun_path)) return 0;
    v5_program_scene_request_server_close(server, path);
    server->fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (server->fd < 0) return 0;
    flags = fcntl(server->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(server->fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        v5_program_scene_request_server_close(server, path);
        return 0;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, target, strlen(target) + 1U);
    unlink(target);
    if (bind(server->fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        v5_program_scene_request_server_close(server, path);
        return 0;
    }
    server->bound = 1;
    return 1;
}

int v5_program_scene_request_server_drain_latest(
    V5ProgramSceneRequestServer *server,
    V5ProgramSceneRequest *request)
{
    V5ProgramSceneRequest candidate;
    int received = 0;
    if (!server || server->fd < 0 || !request) return 0;
    for (;;) {
        size_t expected;
        memset(&candidate, 0, sizeof(candidate));
        ssize_t count = recv(server->fd, &candidate, sizeof(candidate), 0);
        if (count < 0) {
            if (errno == EINTR) continue;
            break;
        }
        expected =
            candidate.reserved == V5_PROGRAM_SCENE_MESSAGE_VIEW_UPDATE ?
            offsetof(V5ProgramSceneRequest, points) : sizeof(candidate);
        if (count == (ssize_t)expected &&
            v5_program_scene_request_merge(request, &candidate)) {
            received = 1;
        }
    }
    return received;
}

void v5_program_scene_request_server_close(V5ProgramSceneRequestServer *server, const char *path)
{
    const char *target = scene_request_path(path);
    if (!server) return;
    if (server->fd >= 0) close(server->fd);
    if (server->bound) unlink(target);
    v5_program_scene_request_server_init(server);
}

#endif

int v5_program_scene_request_merge(
    V5ProgramSceneRequest *current,
    const V5ProgramSceneRequest *candidate)
{
    if (!current || !v5_program_scene_request_valid(candidate)) return 0;
    if (candidate->reserved == V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL) {
        *current = *candidate;
        return 1;
    }
    if (!v5_program_scene_request_valid(current) ||
        current->program_source_identity !=
            candidate->program_source_identity ||
        current->program_generation != candidate->program_generation) {
        return 0;
    }
    current->view_generation = candidate->view_generation;
    current->fit_generation = candidate->fit_generation;
    current->plane = candidate->plane;
    current->width = candidate->width;
    current->height = candidate->height;
    current->scale = candidate->scale;
    current->sine = candidate->sine;
    current->cosine = candidate->cosine;
    current->pan_x = candidate->pan_x;
    current->pan_y = candidate->pan_y;
    current->page_visible = candidate->page_visible;
    return 1;
}

void v5_program_scene_request_prepare_transport(
    V5ProgramSceneRequest *request,
    const V5StatusDisplayScene *acknowledged_scene)
{
    int acknowledged;
    if (!request) return;
    acknowledged = acknowledged_scene &&
        (acknowledged_scene->flags & V5_STATUS_SCENE_FLAG_VALID) != 0U &&
        request->program_source_identity != 0ULL &&
        request->program_generation != 0ULL &&
        acknowledged_scene->program_source_identity ==
            request->program_source_identity &&
        acknowledged_scene->program_generation ==
            request->program_generation;
    request->reserved = acknowledged ?
        V5_PROGRAM_SCENE_MESSAGE_VIEW_UPDATE :
        V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL;
    if (acknowledged) request->point_count = 0U;
}

unsigned int v5_program_scene_request_retry_delay_ms(
    unsigned int retry_count)
{
    if (retry_count >= V5_PROGRAM_SCENE_REQUEST_RETRY_LIMIT) return 0U;
    return V5_PROGRAM_SCENE_REQUEST_RETRY_BASE_MS << retry_count;
}
