#include "gfx_scene_capture.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INITIAL_CAPACITY 256
#define MAX_TRIANGLES 65536

static struct GfxSceneCaptureTriangle *captured_triangles;
static size_t captured_count;
static size_t captured_capacity;
static size_t dropped_count;
static uint64_t frame_index;
static bool capture_enabled;
static bool geometry_capture_enabled;
static float frame_view_matrix[4][4];
static float frame_projection_matrix[4][4];
static bool frame_has_view_matrix;
static bool frame_has_projection_matrix;
static bool frame_view_ambiguous;
static size_t triangle_limit = MAX_TRIANGLES;

void gfx_scene_capture_set_triangle_limit(size_t maximum) {
    triangle_limit = maximum == 0 || maximum > MAX_TRIANGLES ? MAX_TRIANGLES : maximum;
}

void gfx_scene_capture_set_enabled(bool enabled) {
    capture_enabled = enabled;
    geometry_capture_enabled = enabled;
}

bool gfx_scene_capture_is_enabled(void) {
    return capture_enabled;
}

void gfx_scene_capture_set_geometry_enabled(bool enabled) {
    geometry_capture_enabled = capture_enabled && enabled;
}

bool gfx_scene_capture_is_geometry_enabled(void) {
    return capture_enabled && geometry_capture_enabled;
}

void gfx_scene_capture_begin_frame(void) {
    if (!capture_enabled) return;
    captured_count = 0;
    dropped_count = 0;
    frame_has_view_matrix = false;
    frame_has_projection_matrix = false;
    frame_view_ambiguous = false;
    memset(frame_view_matrix, 0, sizeof(frame_view_matrix));
    memset(frame_projection_matrix, 0, sizeof(frame_projection_matrix));
    ++frame_index;
}

void gfx_scene_capture_set_view_matrix(const float matrix[4][4]) {
    if (!capture_enabled || !matrix || frame_view_ambiguous) return;
    if (frame_has_view_matrix && memcmp(frame_view_matrix, matrix, sizeof(frame_view_matrix)) == 0) return;
    if (frame_has_view_matrix && captured_count > 0) {
        /* One frame record has one camera. If it changes after captured geometry,
         * invalidate the split rather than label mixed-view geometry as world space. */
        frame_has_view_matrix = false;
        frame_view_ambiguous = true;
        for (size_t i = 0; i < captured_count; ++i)
            captured_triangles[i].has_world_transform = false;
        return;
    }
    memcpy(frame_view_matrix, matrix, sizeof(frame_view_matrix));
    frame_has_view_matrix = true;
}

void gfx_scene_capture_set_projection_matrix(const float matrix[4][4]) {
    if (!capture_enabled || !matrix) return;
    memcpy(frame_projection_matrix, matrix, sizeof(frame_projection_matrix));
    frame_has_projection_matrix = true;
}

bool gfx_scene_capture_split_modelview(const float modelview[4][4], const float view[4][4], float object_transform[4][4]) {
    if (!modelview || !view || !object_transform) return false;

    /* RSP transforms vertices as row vectors: local * object * view * projection. */
    double augmented[4][8];
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            augmented[row][column] = view[row][column];
            augmented[row][column + 4] = row == column ? 1.0 : 0.0;
        }
    }
    for (int column = 0; column < 4; ++column) {
        int pivot_row = column;
        for (int row = column + 1; row < 4; ++row) {
            if (fabs(augmented[row][column]) > fabs(augmented[pivot_row][column])) pivot_row = row;
        }
        if (fabs(augmented[pivot_row][column]) < 1.0e-10) return false;
        if (pivot_row != column) {
            for (int k = 0; k < 8; ++k) {
                double tmp = augmented[column][k];
                augmented[column][k] = augmented[pivot_row][k];
                augmented[pivot_row][k] = tmp;
            }
        }
        double pivot = augmented[column][column];
        for (int k = 0; k < 8; ++k) augmented[column][k] /= pivot;
        for (int row = 0; row < 4; ++row) {
            if (row == column) continue;
            double factor = augmented[row][column];
            for (int k = 0; k < 8; ++k) augmented[row][k] -= factor * augmented[column][k];
        }
    }

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            double value = 0.0;
            for (int k = 0; k < 4; ++k) value += (double)modelview[row][k] * augmented[k][column + 4];
            if (!isfinite(value) || fabs(value) > 3.402823466e+38) return false;
            object_transform[row][column] = (float)value;
        }
    }
    return true;
}

bool gfx_scene_capture_submit(const struct GfxSceneCaptureTriangle *triangle) {
    if (!capture_enabled || !geometry_capture_enabled || !triangle) return false;
    if (captured_count >= triangle_limit) {
        ++dropped_count;
        return false;
    }
    if (captured_count == captured_capacity) {
        size_t next_capacity = captured_capacity ? captured_capacity * 2 : INITIAL_CAPACITY;
        if (next_capacity > triangle_limit) next_capacity = triangle_limit;
        void *next = realloc(captured_triangles, next_capacity * sizeof(*captured_triangles));
        if (!next) {
            ++dropped_count;
            return false;
        }
        captured_triangles = next;
        captured_capacity = next_capacity;
    }
    captured_triangles[captured_count++] = *triangle;
    return true;
}

struct GfxSceneCaptureFrame gfx_scene_capture_get_frame(void) {
    struct GfxSceneCaptureFrame frame;
    frame.triangles = captured_triangles;
    frame.triangle_count = capture_enabled ? captured_count : 0;
    frame.dropped_triangles = capture_enabled ? dropped_count : 0;
    frame.frame_index = frame_index;
    memcpy(frame.view_matrix, frame_view_matrix, sizeof(frame.view_matrix));
    memcpy(frame.projection_matrix, frame_projection_matrix, sizeof(frame.projection_matrix));
    frame.has_view_matrix = capture_enabled && frame_has_view_matrix;
    frame.has_projection_matrix = capture_enabled && frame_has_projection_matrix;
    return frame;
}

uint64_t gfx_scene_capture_hash_combine(uint64_t parent, uintptr_t source) {
    uint64_t value = (uint64_t)source;
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (parent ^ value) * UINT64_C(0x100000001b3);
}

uint64_t gfx_scene_capture_hash_bytes(uint64_t parent, const void *data, size_t size) {
    const unsigned char *bytes = data;
    uint64_t hash = parent ? parent : UINT64_C(0xcbf29ce484222325);
    if (!data && size != 0) return 0;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(0x100000001b3);
    }
    hash ^= hash >> 30;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27;
    hash *= UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 31;
    return hash;
}
