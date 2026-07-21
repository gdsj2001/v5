#include "v5_program_scene_projection.h"

#include <math.h>
#include <string.h>

#define V5_SCENE_3D_RIGHT_X 0.7071067811865476
#define V5_SCENE_3D_RIGHT_Y 0.7071067811865476
#define V5_SCENE_3D_UP_X (-0.4082482904638631)
#define V5_SCENE_3D_UP_Y 0.4082482904638631
#define V5_SCENE_3D_UP_Z 0.8164965809277261

static int finite_axis(const double axis[V5_STATUS_AXIS_COUNT])
{
    unsigned int i;
    for (i = 0U; axis && i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!isfinite(axis[i])) return 0;
    }
    return axis != 0;
}

static void plane_values(
    const double axis[V5_STATUS_AXIS_COUNT],
    uint32_t plane,
    double *u,
    double *v)
{
    if (plane == 1U) { *u = axis[0]; *v = axis[2]; }
    else if (plane == 2U) { *u = axis[1]; *v = axis[2]; }
    else if (plane == 3U) {
        *u = axis[0] * V5_SCENE_3D_RIGHT_X +
            axis[1] * V5_SCENE_3D_RIGHT_Y;
        *v = axis[0] * V5_SCENE_3D_UP_X +
            axis[1] * V5_SCENE_3D_UP_Y +
            axis[2] * V5_SCENE_3D_UP_Z;
    } else { *u = axis[0]; *v = axis[1]; }
}

static void bounds_add_values(
    V5ProgramSceneBounds *bounds,
    double u,
    double v)
{
    if (!bounds || !isfinite(u) || !isfinite(v)) return;
    if (!bounds->valid) {
        bounds->valid = 1;
        bounds->min_u = bounds->max_u = u;
        bounds->min_v = bounds->max_v = v;
        return;
    }
    if (u < bounds->min_u) bounds->min_u = u;
    if (u > bounds->max_u) bounds->max_u = u;
    if (v < bounds->min_v) bounds->min_v = v;
    if (v > bounds->max_v) bounds->max_v = v;
}

void v5_program_scene_bounds_add(
    V5ProgramSceneBounds *bounds,
    const double axis[V5_STATUS_AXIS_COUNT],
    uint32_t plane)
{
    double u;
    double v;
    if (!bounds || !finite_axis(axis)) return;
    plane_values(axis, plane, &u, &v);
    bounds_add_values(bounds, u, v);
}

static int project_plane_values(
    double u,
    double v,
    const V5ProgramSceneBounds *bounds,
    const V5ProgramSceneRequest *request,
    double *x,
    double *y)
{
    double span_u;
    double span_v;
    double min_u;
    double min_v;
    double pad_x;
    double pad_y;
    double draw_w;
    double draw_h;
    double scale;
    double content_w;
    double content_h;
    if (!bounds || !bounds->valid || !request || !x || !y) return 0;
    span_u = bounds->max_u - bounds->min_u;
    span_v = bounds->max_v - bounds->min_v;
    min_u = bounds->min_u;
    min_v = bounds->min_v;
    pad_x = request->width * 0.10;
    pad_y = request->height * 0.10;
    draw_w = request->width - pad_x * 2.0;
    draw_h = request->height - pad_y * 2.0;
    if (fabs(span_u) < 0.001) { span_u = 1.0; min_u -= 0.5; }
    if (fabs(span_v) < 0.001) { span_v = 1.0; min_v -= 0.5; }
    if (draw_w < 1.0) { draw_w = request->width; pad_x = 0.0; }
    if (draw_h < 1.0) { draw_h = request->height; pad_y = 0.0; }
    scale = draw_w / span_u;
    if (draw_h / span_v < scale) scale = draw_h / span_v;
    content_w = span_u * scale;
    content_h = span_v * scale;
    *x = pad_x + (draw_w - content_w) * 0.5 + (u - min_u) * scale;
    *y = pad_y + (draw_h - content_h) * 0.5 +
        content_h - (v - min_v) * scale;
    return isfinite(*x) && isfinite(*y);
}

static V5StatusScreenPoint project_plane_point(
    double u,
    double v,
    const V5ProgramSceneBounds *bounds,
    const V5ProgramSceneRequest *request)
{
    V5StatusScreenPoint point = {0.0f, 0.0f};
    double x;
    double y;
    if (!project_plane_values(u, v, bounds, request, &x, &y)) return point;
    if (request->scale != 1.0f || request->sine != 0.0f ||
        request->cosine != 1.0f || request->pan_x != 0.0f ||
        request->pan_y != 0.0f) {
        const double cx = request->width * 0.5;
        const double cy = request->height * 0.5;
        const double dx = x - cx;
        const double dy = y - cy;
        const double rx = dx * request->cosine - dy * request->sine;
        const double ry = dx * request->sine + dy * request->cosine;
        x = cx + rx * request->scale + request->pan_x;
        y = cy + ry * request->scale + request->pan_y;
    }
    point.x = (float)x;
    point.y = (float)y;
    return point;
}

static V5StatusScreenPoint project_point(
    const double axis[V5_STATUS_AXIS_COUNT],
    const V5ProgramSceneBounds *bounds,
    const V5ProgramSceneRequest *request)
{
    double u;
    double v;
    plane_values(axis, request->plane, &u, &v);
    return project_plane_point(u, v, bounds, request);
}

typedef struct V5ProgramScenePlaneMatrix {
    double u[4];
    double v[4];
} V5ProgramScenePlaneMatrix;

static void prepare_plane_matrix(
    const V5ProgramScenePoseMatrix *pose,
    uint32_t plane,
    V5ProgramScenePlaneMatrix *matrix)
{
    unsigned int column;
    memset(matrix, 0, sizeof(*matrix));
    for (column = 0U; column < 4U; ++column) {
        if (plane == 1U) {
            matrix->u[column] = pose->value[0][column];
            matrix->v[column] = pose->value[2][column];
        } else if (plane == 2U) {
            matrix->u[column] = pose->value[1][column];
            matrix->v[column] = pose->value[2][column];
        } else if (plane == 3U) {
            matrix->u[column] = pose->value[0][column] * V5_SCENE_3D_RIGHT_X +
                pose->value[1][column] * V5_SCENE_3D_RIGHT_Y;
            matrix->v[column] = pose->value[0][column] * V5_SCENE_3D_UP_X +
                pose->value[1][column] * V5_SCENE_3D_UP_Y +
                pose->value[2][column] * V5_SCENE_3D_UP_Z;
        } else {
            matrix->u[column] = pose->value[0][column];
            matrix->v[column] = pose->value[1][column];
        }
    }
}

static void transform_plane_values(
    const V5ProgramScenePlaneMatrix *matrix,
    const double axis[V5_STATUS_AXIS_COUNT],
    double *u,
    double *v)
{
    *u = matrix->u[0] * axis[0] + matrix->u[1] * axis[1] +
        matrix->u[2] * axis[2] + matrix->u[3];
    *v = matrix->v[0] * axis[0] + matrix->v[1] * axis[1] +
        matrix->v[2] * axis[2] + matrix->v[3];
}

static int same_integer_pixel(
    V5StatusScreenPoint left,
    V5StatusScreenPoint right)
{
    return lroundf(left.x) == lroundf(right.x) &&
        lroundf(left.y) == lroundf(right.y);
}

static void compact_program_pixel_duplicates(V5StatusDisplayScene *scene)
{
    unsigned int read_index;
    unsigned int write_index;
    if (!scene || scene->point_count < 2U) return;
    write_index = 1U;
    for (read_index = 1U; read_index < scene->point_count; ++read_index) {
        const int starts_segment = scene->break_before[read_index] != 0U;
        if (!starts_segment && same_integer_pixel(
                scene->points[write_index - 1U],
                scene->points[read_index])) {
            continue;
        }
        scene->points[write_index] = scene->points[read_index];
        scene->break_before[write_index] =
            scene->break_before[read_index] ? 1U : 0U;
        write_index += 1U;
    }
    scene->point_count = write_index;
}

int v5_program_scene_bounds_outside_viewport(
    const V5ProgramSceneBounds *candidate,
    const V5ProgramSceneBounds *frozen,
    const V5ProgramSceneRequest *request)
{
    const double u[2] = {
        candidate ? candidate->min_u : 0.0,
        candidate ? candidate->max_u : 0.0};
    const double v[2] = {
        candidate ? candidate->min_v : 0.0,
        candidate ? candidate->max_v : 0.0};
    unsigned int ui;
    unsigned int vi;
    if (!candidate || !candidate->valid || !frozen || !frozen->valid ||
        !request) return 1;
    for (ui = 0U; ui < 2U; ++ui) {
        for (vi = 0U; vi < 2U; ++vi) {
            double x;
            double y;
            if (!project_plane_values(
                    u[ui], v[vi], frozen, request, &x, &y) ||
                x < -0.5 || x > (double)request->width + 0.5 ||
                y < -0.5 || y > (double)request->height + 0.5) return 1;
        }
    }
    return 0;
}

int v5_program_scene_fit_key_changed(
    const V5ProgramSceneProducer *producer,
    const V5ProgramSceneRequest *request)
{
    return !producer || !request || !producer->fit_bounds.valid ||
        producer->fit_program_source_identity !=
            request->program_source_identity ||
        producer->fit_program_generation != request->program_generation ||
        producer->fit_request_generation != request->fit_generation ||
        producer->fit_plane != request->plane ||
        producer->fit_width != request->width ||
        producer->fit_height != request->height;
}

void v5_program_scene_commit_fit(
    V5ProgramSceneProducer *producer,
    const V5ProgramSceneBounds *bounds)
{
    producer->fit_bounds = *bounds;
    producer->fit_program_source_identity =
        producer->request.program_source_identity;
    producer->fit_program_generation = producer->request.program_generation;
    producer->fit_request_generation = producer->request.fit_generation;
    producer->fit_plane = producer->request.plane;
    producer->fit_width = producer->request.width;
    producer->fit_height = producer->request.height;
    if (++producer->fit_generation == 0ULL) producer->fit_generation = 1ULL;
}

static void dirty_reset(V5StatusDisplayScene *scene)
{
    scene->dirty_x1 = 1;
    scene->dirty_y1 = 1;
    scene->dirty_x2 = 0;
    scene->dirty_y2 = 0;
}

static void dirty_add(V5StatusDisplayScene *scene, V5StatusScreenPoint point)
{
    int x = (int)floor((double)point.x);
    int y = (int)floor((double)point.y);
    if (scene->dirty_x2 < scene->dirty_x1) {
        scene->dirty_x1 = scene->dirty_x2 = (int16_t)x;
        scene->dirty_y1 = scene->dirty_y2 = (int16_t)y;
        return;
    }
    if (x < scene->dirty_x1) scene->dirty_x1 = (int16_t)x;
    if (x > scene->dirty_x2) scene->dirty_x2 = (int16_t)x;
    if (y < scene->dirty_y1) scene->dirty_y1 = (int16_t)y;
    if (y > scene->dirty_y2) scene->dirty_y2 = (int16_t)y;
}

int v5_program_scene_transform_project_program(
    V5ProgramSceneProducer *producer,
    int emit_scene)
{
    V5StatusDisplayScene *scene;
    V5ProgramScenePlaneMatrix matrix;
    unsigned int i;
    if (!producer || !producer->pose_valid) return 0;
    scene = &producer->scene;
    prepare_plane_matrix(
        &producer->pose_matrix, producer->request.plane, &matrix);
    if (emit_scene) {
        if (!producer->fit_bounds.valid) return 0;
        memset(scene, 0, sizeof(*scene));
        dirty_reset(scene);
        scene->point_count = producer->static_point_count;
    }
    producer->point_traversal_count += 1ULL;
    for (i = 0U; i < producer->static_point_count; ++i) {
        double u;
        double v;
        transform_plane_values(
            &matrix, producer->base_points[i].axis, &u, &v);
        bounds_add_values(&producer->pose_bounds, u, v);
        if (emit_scene) {
            scene->points[i] = project_plane_point(
                u, v, &producer->fit_bounds, &producer->request);
            scene->break_before[i] =
                producer->request.break_before[i] ? 1U : 0U;
            dirty_add(scene, scene->points[i]);
        }
    }
    if (!emit_scene) return producer->pose_bounds.valid;
    compact_program_pixel_duplicates(scene);
    for (i = 0U; i < producer->world_segment_count; ++i) {
        const V5ProgramSceneWorldSegment *world =
            &producer->world_segments[i];
        V5StatusSceneSegment *segment = &scene->segments[i];
        segment->start = project_point(
            world->start.axis, &producer->fit_bounds, &producer->request);
        segment->end = project_point(
            world->end.axis, &producer->fit_bounds, &producer->request);
        segment->role = world->role;
        segment->index = world->index;
        segment->flags = 1U;
        dirty_add(scene, segment->start);
        dirty_add(scene, segment->end);
    }
    scene->segment_count = producer->world_segment_count;
    for (i = 0U; i < producer->world_marker_count; ++i) {
        const V5ProgramSceneWorldMarker *world = &producer->world_markers[i];
        V5StatusSceneMarker *marker = &scene->markers[i];
        marker->point = project_point(
            world->point.axis, &producer->fit_bounds, &producer->request);
        marker->role = world->role;
        marker->index = world->index;
        marker->flags = 1U;
        dirty_add(scene, marker->point);
    }
    scene->marker_count = producer->world_marker_count;
    producer->project_count += 1ULL;
    return 1;
}

void v5_program_scene_prepare_dynamic_update(
    V5ProgramSceneProducer *producer)
{
    V5StatusDisplayScene *scene = &producer->scene;
    unsigned int i;
    dirty_reset(scene);
    for (i = producer->world_segment_count; i < scene->segment_count; ++i) {
        dirty_add(scene, scene->segments[i].start);
        dirty_add(scene, scene->segments[i].end);
    }
    for (i = producer->world_marker_count; i < scene->marker_count; ++i) {
        dirty_add(scene, scene->markers[i].point);
    }
    scene->segment_count = producer->world_segment_count;
    scene->marker_count = producer->world_marker_count;
}

void v5_program_scene_add_dynamic_segment(
    V5ProgramSceneProducer *producer,
    const double start[V5_STATUS_AXIS_COUNT],
    const double end[V5_STATUS_AXIS_COUNT],
    uint16_t role)
{
    V5StatusDisplayScene *scene = &producer->scene;
    V5StatusSceneSegment *segment;
    if (scene->segment_count >= V5_STATUS_SCENE_SEGMENT_COUNT) return;
    segment = &scene->segments[scene->segment_count++];
    segment->start = project_point(
        start, &producer->fit_bounds, &producer->request);
    segment->end = project_point(
        end, &producer->fit_bounds, &producer->request);
    segment->role = role;
    segment->index = 0U;
    segment->flags = 1U;
    dirty_add(scene, segment->start);
    dirty_add(scene, segment->end);
}

void v5_program_scene_add_dynamic_marker(
    V5ProgramSceneProducer *producer,
    const double point[V5_STATUS_AXIS_COUNT],
    uint16_t role)
{
    V5StatusDisplayScene *scene = &producer->scene;
    V5StatusSceneMarker *marker;
    if (scene->marker_count >= V5_STATUS_SCENE_MARKER_COUNT) return;
    marker = &scene->markers[scene->marker_count++];
    marker->point = project_point(
        point, &producer->fit_bounds, &producer->request);
    marker->role = role;
    marker->index = 0U;
    marker->flags = 1U;
    dirty_add(scene, marker->point);
}
