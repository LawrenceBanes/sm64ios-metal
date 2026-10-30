#include "gfx_scene_bvh.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BVH_LEAF_TRIANGLES 4u
#define BVH_TRACE_STACK_SIZE 64u

struct BuildContext {
    struct GfxSceneBvh *bvh;
    float (*centroids)[3];
    bool failed;
};

static float component_min(float a, float b) { return a < b ? a : b; }
static float component_max(float a, float b) { return a > b ? a : b; }

static float triangle_centroid(const struct GfxBvhTriangle *triangle, int axis) {
    return (triangle->v0[axis] + triangle->v1[axis] + triangle->v2[axis]) / 3.0f;
}

static void swap_indices(struct BuildContext *ctx, uint32_t a, uint32_t b) {
    uint32_t tmp = ctx->bvh->triangle_indices[a];
    ctx->bvh->triangle_indices[a] = ctx->bvh->triangle_indices[b];
    ctx->bvh->triangle_indices[b] = tmp;
}

static void sort_indices(struct BuildContext *ctx, size_t first, size_t end, int axis) {
    if (end - first < 2) return;
    size_t i = first, j = end - 1;
    float pivot = ctx->centroids[ctx->bvh->triangle_indices[first + (end - first) / 2]][axis];
    while (i <= j) {
        while (ctx->centroids[ctx->bvh->triangle_indices[i]][axis] < pivot) ++i;
        while (ctx->centroids[ctx->bvh->triangle_indices[j]][axis] > pivot) {
            if (j == 0) break;
            --j;
        }
        if (i <= j) {
            swap_indices(ctx, (uint32_t)i, (uint32_t)j);
            ++i;
            if (j == 0) break;
            --j;
        }
    }
    if (first < j + 1) sort_indices(ctx, first, j + 1, axis);
    if (i < end) sort_indices(ctx, i, end, axis);
}

static uint32_t build_node(struct BuildContext *ctx, size_t first, size_t count, uint32_t depth) {
    struct GfxSceneBvh *bvh = ctx->bvh;
    if (ctx->failed || bvh->node_count >= bvh->node_capacity || depth > bvh->max_depth) {
        ctx->failed = true;
        return 0;
    }

    uint32_t node_index = (uint32_t)bvh->node_count++;
    struct GfxBvhNode *node = &bvh->nodes[node_index];
    for (int axis = 0; axis < 3; ++axis) {
        node->bounds_min[axis] = FLT_MAX;
        node->bounds_max[axis] = -FLT_MAX;
    }
    float centroid_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float centroid_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (size_t i = first; i < first + count; ++i) {
        uint32_t tri_index = bvh->triangle_indices[i];
        const struct GfxBvhTriangle *tri = &bvh->triangles[tri_index];
        for (int axis = 0; axis < 3; ++axis) {
            float lo = component_min(tri->v0[axis], component_min(tri->v1[axis], tri->v2[axis]));
            float hi = component_max(tri->v0[axis], component_max(tri->v1[axis], tri->v2[axis]));
            node->bounds_min[axis] = component_min(node->bounds_min[axis], lo);
            node->bounds_max[axis] = component_max(node->bounds_max[axis], hi);
            centroid_min[axis] = component_min(centroid_min[axis], ctx->centroids[tri_index][axis]);
            centroid_max[axis] = component_max(centroid_max[axis], ctx->centroids[tri_index][axis]);
        }
    }

    node->left = node->right = 0;
    node->first = (uint32_t)first;
    node->count = (uint32_t)count;
    if (count <= BVH_LEAF_TRIANGLES) return node_index;

    int axis = 0;
    if (centroid_max[1] - centroid_min[1] > centroid_max[axis] - centroid_min[axis]) axis = 1;
    if (centroid_max[2] - centroid_min[2] > centroid_max[axis] - centroid_min[axis]) axis = 2;
    if (centroid_max[axis] <= centroid_min[axis]) return node_index;

    sort_indices(ctx, first, first + count, axis);
    size_t left_count = count / 2;
    if (left_count == 0 || left_count == count) return node_index;
    node->count = 0;
    node->left = build_node(ctx, first, left_count, depth + 1);
    node->right = build_node(ctx, first + left_count, count - left_count, depth + 1);
    return node_index;
}

void gfx_scene_bvh_destroy(struct GfxSceneBvh *bvh) {
    if (!bvh) return;
    free(bvh->nodes);
    free(bvh->triangle_indices);
    memset(bvh, 0, sizeof(*bvh));
}

bool gfx_scene_bvh_build(struct GfxSceneBvh *bvh,
                         const struct GfxBvhTriangle *triangles, size_t triangle_count,
                         uint32_t max_depth) {
    if (!bvh || (triangle_count && !triangles) || triangle_count > UINT32_MAX || max_depth == 0)
        return false;
    gfx_scene_bvh_destroy(bvh);
    bvh->triangles = triangles;
    bvh->triangle_count = triangle_count;
    bvh->max_depth = max_depth;
    if (triangle_count == 0) return true;
    if (triangle_count > (SIZE_MAX / sizeof(*bvh->nodes) - 1) / 2 ||
        triangle_count > SIZE_MAX / sizeof(*bvh->triangle_indices) ||
        triangle_count > SIZE_MAX / sizeof(float[3])) {
        gfx_scene_bvh_destroy(bvh);
        return false;
    }

    bvh->node_capacity = triangle_count * 2;
    bvh->nodes = calloc(bvh->node_capacity, sizeof(*bvh->nodes));
    bvh->triangle_indices = malloc(triangle_count * sizeof(*bvh->triangle_indices));
    float (*centroids)[3] = malloc(triangle_count * sizeof(*centroids));
    if (!bvh->nodes || !bvh->triangle_indices || !centroids) {
        free(centroids);
        gfx_scene_bvh_destroy(bvh);
        return false;
    }
    for (size_t i = 0; i < triangle_count; ++i) {
        bvh->triangle_indices[i] = (uint32_t)i;
        for (int axis = 0; axis < 3; ++axis)
            centroids[i][axis] = triangle_centroid(&triangles[i], axis);
    }
    struct BuildContext ctx = {bvh, centroids, false};
    build_node(&ctx, 0, triangle_count, 0);
    free(centroids);
    if (ctx.failed) {
        gfx_scene_bvh_destroy(bvh);
        return false;
    }
    return true;
}

static bool intersect_bounds(const struct GfxBvhNode *node, const struct GfxBvhRay *ray,
                             float limit) {
    float near_t = ray->t_min, far_t = limit;
    for (int axis = 0; axis < 3; ++axis) {
        float direction = ray->direction[axis];
        if (fabsf(direction) < 1.0e-20f) {
            if (ray->origin[axis] < node->bounds_min[axis] || ray->origin[axis] > node->bounds_max[axis])
                return false;
            continue;
        }
        float inv = 1.0f / direction;
        float t0 = (node->bounds_min[axis] - ray->origin[axis]) * inv;
        float t1 = (node->bounds_max[axis] - ray->origin[axis]) * inv;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        near_t = component_max(near_t, t0);
        far_t = component_min(far_t, t1);
        if (near_t > far_t) return false;
    }
    return far_t >= ray->t_min;
}

static bool intersect_triangle(const struct GfxBvhTriangle *tri, const struct GfxBvhRay *ray,
                               float limit, float *distance, float *u_out, float *v_out) {
    float e1[3], e2[3], p[3], tvec[3], q[3];
    for (int i = 0; i < 3; ++i) {
        e1[i] = tri->v1[i] - tri->v0[i];
        e2[i] = tri->v2[i] - tri->v0[i];
    }
    p[0] = ray->direction[1] * e2[2] - ray->direction[2] * e2[1];
    p[1] = ray->direction[2] * e2[0] - ray->direction[0] * e2[2];
    p[2] = ray->direction[0] * e2[1] - ray->direction[1] * e2[0];
    float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (fabsf(det) < 1.0e-8f) return false;
    float inv_det = 1.0f / det;
    for (int i = 0; i < 3; ++i) tvec[i] = ray->origin[i] - tri->v0[i];
    float u = (tvec[0] * p[0] + tvec[1] * p[1] + tvec[2] * p[2]) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;
    q[0] = tvec[1] * e1[2] - tvec[2] * e1[1];
    q[1] = tvec[2] * e1[0] - tvec[0] * e1[2];
    q[2] = tvec[0] * e1[1] - tvec[1] * e1[0];
    float v = (ray->direction[0] * q[0] + ray->direction[1] * q[1] + ray->direction[2] * q[2]) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;
    float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv_det;
    if (t < ray->t_min || t > limit) return false;
    *distance = t;
    *u_out = u;
    *v_out = v;
    return true;
}

enum GfxBvhTraceResult gfx_scene_bvh_trace(const struct GfxSceneBvh *bvh,
                                           const struct GfxBvhRay *ray,
                                           struct GfxBvhHit *hit) {
    if (!bvh || !ray || !bvh->nodes || bvh->node_count == 0) return GFX_BVH_TRACE_MISS;
    uint32_t stack[BVH_TRACE_STACK_SIZE];
    size_t stack_size = 0;
    stack[stack_size++] = 0;
    float nearest = ray->t_max;
    bool found = false;
    struct GfxBvhHit result;
    while (stack_size) {
        uint32_t node_index = stack[--stack_size];
        if (node_index >= bvh->node_count) return GFX_BVH_TRACE_STACK_OVERFLOW;
        const struct GfxBvhNode *node = &bvh->nodes[node_index];
        if (!intersect_bounds(node, ray, nearest)) continue;
        if (node->count) {
            for (uint32_t i = 0; i < node->count; ++i) {
                uint32_t tri_index = bvh->triangle_indices[node->first + i];
                float t, u, v;
                if (intersect_triangle(&bvh->triangles[tri_index], ray, nearest, &t, &u, &v)) {
                    nearest = t;
                    result.triangle_index = tri_index;
                    result.instance_id = bvh->triangles[tri_index].instance_id;
                    result.material_id = bvh->triangles[tri_index].material_id;
                    result.distance = t;
                    result.barycentric_u = u;
                    result.barycentric_v = v;
                    found = true;
                }
            }
        } else {
            if (stack_size + 2 > BVH_TRACE_STACK_SIZE) return GFX_BVH_TRACE_STACK_OVERFLOW;
            stack[stack_size++] = node->right;
            stack[stack_size++] = node->left;
        }
    }
    if (!found) return GFX_BVH_TRACE_MISS;
    if (hit) *hit = result;
    return GFX_BVH_TRACE_HIT;
}
