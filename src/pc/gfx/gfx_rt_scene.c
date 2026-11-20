#include "gfx_rt_scene.h"

#include <math.h>
#include <string.h>

#define INSTANCE_MAP_CAPACITY 32768u
#define INSTANCE_MAP_MASK (INSTANCE_MAP_CAPACITY - 1u)

struct InstanceSlot {
    uint64_t key;
    bool occupied;
};

static struct InstanceSlot previous_instances[INSTANCE_MAP_CAPACITY];
static struct InstanceSlot current_instances[INSTANCE_MAP_CAPACITY];
static struct GfxRTTriangle static_triangles[GFX_RT_SCENE_MAX_TRIANGLES];

static size_t instance_slot(uint64_t key) {
    key ^= key >> 30;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27;
    key *= UINT64_C(0x94d049bb133111eb);
    key ^= key >> 31;
    return (size_t)key & INSTANCE_MAP_MASK;
}

static bool map_contains(const struct InstanceSlot *map, uint64_t key) {
    size_t slot = instance_slot(key);
    for (size_t probe = 0; probe < INSTANCE_MAP_CAPACITY; ++probe) {
        const struct InstanceSlot *entry = &map[(slot + probe) & INSTANCE_MAP_MASK];
        if (!entry->occupied) return false;
        if (entry->key == key) return true;
    }
    return false;
}

static bool map_insert(struct InstanceSlot *map, uint64_t key) {
    size_t slot = instance_slot(key);
    for (size_t probe = 0; probe < INSTANCE_MAP_CAPACITY; ++probe) {
        struct InstanceSlot *entry = &map[(slot + probe) & INSTANCE_MAP_MASK];
        if (entry->occupied && entry->key == key) return true;
        if (!entry->occupied) {
            entry->key = key;
            entry->occupied = true;
            return true;
        }
    }
    return false;
}

static bool transform_point(const float p[3], const float matrix[4][4], float out[3]) {
    for (int column = 0; column < 3; ++column) {
        double value = (double)p[0] * matrix[0][column]
                    + (double)p[1] * matrix[1][column]
                    + (double)p[2] * matrix[2][column]
                    + matrix[3][column];
        if (!isfinite(value) || fabs(value) > 1.0e9) return false;
        out[column] = (float)value;
    }
    return true;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

struct GfxRTScene gfx_rt_scene_capture_static(const struct GfxSceneCaptureFrame *frame) {
    struct GfxRTScene scene = {0};
    scene.triangles = static_triangles;
    if (!frame) return scene;
    scene.source_frame = frame->frame_index;
    scene.has_camera = frame->has_view_matrix && frame->has_projection_matrix;
    if (scene.has_camera) {
        memcpy(scene.view_matrix, frame->view_matrix, sizeof(scene.view_matrix));
        memcpy(scene.projection_matrix, frame->projection_matrix, sizeof(scene.projection_matrix));
    }
    if (frame->triangle_count > GFX_RT_SCENE_MAX_CAPTURE_INPUT) {
        scene.dropped_triangles = frame->triangle_count;
        return scene;
    }
    if (frame->triangle_count && !frame->triangles) return scene;

    memset(current_instances, 0, sizeof(current_instances));
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < frame->triangle_count; ++i) {
        const struct GfxSceneCaptureTriangle *source = &frame->triangles[i];
        if (!source->has_world_transform || !source->instance_id) {
            ++scene.dropped_triangles;
            continue;
        }
        if (!map_insert(current_instances, source->instance_id)) {
            ++scene.dropped_triangles;
            continue;
        }
        if (!map_contains(previous_instances, source->instance_id)) continue;
        if (scene.triangle_count >= GFX_RT_SCENE_MAX_TRIANGLES) {
            ++scene.dropped_triangles;
            continue;
        }

        struct GfxRTTriangle candidate;
        bool valid = true;
        for (int vertex = 0; vertex < 3; ++vertex) {
            valid &= transform_point(source->vertices[vertex].position,
                                     source->object_transform,
                                     candidate.position[vertex]);
        }
        if (!valid) {
            ++scene.dropped_triangles;
            continue;
        }
        static_triangles[scene.triangle_count++] = candidate;
        hash = hash_bytes(hash, &candidate, sizeof(candidate));
    }
    scene.geometry_hash = scene.triangle_count ? hash : 0;
    memcpy(previous_instances, current_instances, sizeof(previous_instances));
    return scene;
}

void gfx_rt_scene_reset(void) {
    memset(previous_instances, 0, sizeof(previous_instances));
    memset(current_instances, 0, sizeof(current_instances));
    memset(static_triangles, 0, sizeof(static_triangles));
}
