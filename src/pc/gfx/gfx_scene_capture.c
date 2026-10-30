#include "gfx_scene_capture.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 256
#define MAX_TRIANGLES 65536

static struct GfxSceneCaptureTriangle *captured_triangles;
static size_t captured_count;
static size_t captured_capacity;
static size_t dropped_count;
static uint64_t frame_index;
static bool capture_enabled;

void gfx_scene_capture_set_enabled(bool enabled) {
    capture_enabled = enabled;
}

bool gfx_scene_capture_is_enabled(void) {
    return capture_enabled;
}

void gfx_scene_capture_begin_frame(void) {
    if (!capture_enabled) return;
    captured_count = 0;
    dropped_count = 0;
    ++frame_index;
}

bool gfx_scene_capture_submit(const struct GfxSceneCaptureTriangle *triangle) {
    if (!capture_enabled || !triangle) return false;
    if (captured_count >= MAX_TRIANGLES) {
        ++dropped_count;
        return false;
    }
    if (captured_count == captured_capacity) {
        size_t next_capacity = captured_capacity ? captured_capacity * 2 : INITIAL_CAPACITY;
        if (next_capacity > MAX_TRIANGLES) next_capacity = MAX_TRIANGLES;
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
