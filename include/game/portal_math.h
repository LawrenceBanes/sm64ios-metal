#ifndef PORTAL_MATH_H
#define PORTAL_MATH_H

#include <math.h>

/* Renderer-independent portal math shared by game code and host tests.
 * SM64 world axes are X/Z horizontal and +Y up. The frame's normal points
 * out of the portal toward the side a player enters from. */
typedef struct PortalFrame {
    float position[3];
    float normal[3];
    float right[3];
    float up[3];
} PortalFrame;

typedef struct PortalTriangle {
    float vertex[3][3];
    float normal[3];
} PortalTriangle;

static inline float portal_dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline float portal_length3(const float v[3]) {
    return sqrtf(portal_dot3(v, v));
}

static inline void portal_cross3(const float a[3], const float b[3], float result[3]) {
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

static inline int portal_normalize3(float v[3]) {
    float length = portal_length3(v);
    if (length < 0.00001f) return 0;
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
    return 1;
}

/* Build an orthonormal basis around any wall, floor, ceiling, or slope normal.
 * preferredUp controls roll; if it is parallel to the normal, a stable
 * fallback axis is projected into the portal plane. */
static inline int portal_frame_set_basis(PortalFrame *frame, const float normal[3],
                                         const float preferredUp[3]) {
    float candidateUp[3];
    float projection;
    if (!frame) return 0;
    frame->normal[0] = normal[0];
    frame->normal[1] = normal[1];
    frame->normal[2] = normal[2];
    if (!portal_normalize3(frame->normal)) return 0;

    candidateUp[0] = preferredUp ? preferredUp[0] : 0.0f;
    candidateUp[1] = preferredUp ? preferredUp[1] : 1.0f;
    candidateUp[2] = preferredUp ? preferredUp[2] : 0.0f;
    projection = portal_dot3(candidateUp, frame->normal);
    candidateUp[0] -= projection * frame->normal[0];
    candidateUp[1] -= projection * frame->normal[1];
    candidateUp[2] -= projection * frame->normal[2];
    if (!portal_normalize3(candidateUp)) {
        candidateUp[0] = 0.0f; candidateUp[1] = 0.0f; candidateUp[2] = 1.0f;
        projection = portal_dot3(candidateUp, frame->normal);
        candidateUp[0] -= projection * frame->normal[0];
        candidateUp[1] -= projection * frame->normal[1];
        candidateUp[2] -= projection * frame->normal[2];
        if (!portal_normalize3(candidateUp)) {
            candidateUp[0] = 1.0f; candidateUp[1] = 0.0f; candidateUp[2] = 0.0f;
            projection = portal_dot3(candidateUp, frame->normal);
            candidateUp[0] -= projection * frame->normal[0];
            candidateUp[1] -= projection * frame->normal[1];
            candidateUp[2] -= projection * frame->normal[2];
            if (!portal_normalize3(candidateUp)) return 0;
        }
    }

    portal_cross3(candidateUp, frame->normal, frame->right);
    if (!portal_normalize3(frame->right)) return 0;
    portal_cross3(frame->normal, frame->right, frame->up);
    return portal_normalize3(frame->up);
}

static inline void portal_transform_vector(const PortalFrame *from, const PortalFrame *to,
                                           const float input[3], float output[3]) {
    float localRight = portal_dot3(input, from->right);
    float localUp = portal_dot3(input, from->up);
    float localNormal = portal_dot3(input, from->normal);
    /* The half-turn about portal-up maps incoming motion to outgoing motion. */
    output[0] = -localRight * to->right[0] + localUp * to->up[0] - localNormal * to->normal[0];
    output[1] = -localRight * to->right[1] + localUp * to->up[1] - localNormal * to->normal[1];
    output[2] = -localRight * to->right[2] + localUp * to->up[2] - localNormal * to->normal[2];
}

static inline void portal_transform_point(const PortalFrame *from, const PortalFrame *to,
                                          const float input[3], float exitOffset,
                                          float output[3]) {
    float relative[3] = {
        input[0] - from->position[0],
        input[1] - from->position[1],
        input[2] - from->position[2],
    };
    float transformed[3];
    portal_transform_vector(from, to, relative, transformed);
    output[0] = to->position[0] + transformed[0] + exitOffset * to->normal[0];
    output[1] = to->position[1] + transformed[1] + exitOffset * to->normal[1];
    output[2] = to->position[2] + transformed[2] + exitOffset * to->normal[2];
}

/* Axis-aligned SM64 player capsule support radius along any portal tangent. */
static inline float portal_capsule_axis_extent(const float axis[3], float radius,
                                               float playerHeight) {
    return radius * sqrtf(axis[0] * axis[0] + axis[2] * axis[2])
         + (playerHeight * 0.5f) * fabsf(axis[1]);
}

static inline int portal_exit_correction_is_safe(const float requested[3],
                                                  const float resolved[3],
                                                  float maximumCorrection) {
    const float dx = requested[0] - resolved[0];
    const float dy = requested[1] - resolved[1];
    const float dz = requested[2] - resolved[2];
    return dx * dx + dy * dy + dz * dz <= maximumCorrection * maximumCorrection;
}

static inline int portal_exit_resolve_floor(float *feetY, float floorHeight,
                                            int hasFloor, int airborne,
                                            float maximumGroundSnap) {
    /* An airborne exit can safely continue falling when the destination is
     * beside a wall or over a drop. Grounded exits still require support. */
    if (!hasFloor) return airborne;
    if (airborne) return floorHeight <= *feetY;
    if (floorHeight > *feetY) {
        if (floorHeight - *feetY > maximumGroundSnap) return 0;
        *feetY = floorHeight;
    }
    return 1;
}

/* previous/current are the player capsule centers. Portal-local up/right
 * bounds account for the capsule's projection, so the full body fits. */
static inline int portal_segment_crosses(const PortalFrame *frame, const float previous[3],
                                         const float current[3], float halfWidth,
                                         float halfHeight, float marioRadius,
                                         float marioHeight, float *crossingT) {
    float previousRelative[3] = {
        previous[0] - frame->position[0], previous[1] - frame->position[1],
        previous[2] - frame->position[2],
    };
    float currentRelative[3] = {
        current[0] - frame->position[0], current[1] - frame->position[1],
        current[2] - frame->position[2],
    };
    float startDistance = portal_dot3(previousRelative, frame->normal);
    float endDistance = portal_dot3(currentRelative, frame->normal);
    float t, localRight, localUp, rightExtent, upExtent;
    float crossing[3];

    if (startDistance <= 0.0f || endDistance > 0.0f || startDistance == endDistance)
        return 0;
    t = startDistance / (startDistance - endDistance);
    crossing[0] = previous[0] + (current[0] - previous[0]) * t - frame->position[0];
    crossing[1] = previous[1] + (current[1] - previous[1]) * t - frame->position[1];
    crossing[2] = previous[2] + (current[2] - previous[2]) * t - frame->position[2];
    localRight = portal_dot3(crossing, frame->right);
    localUp = portal_dot3(crossing, frame->up);
    rightExtent = portal_capsule_axis_extent(frame->right, marioRadius, marioHeight);
    upExtent = portal_capsule_axis_extent(frame->up, marioRadius, marioHeight);
    if (fabsf(localRight) > halfWidth - rightExtent
        || fabsf(localUp) > halfHeight - upExtent)
        return 0;
    if (crossingT) *crossingT = t;
    return 1;
}

static inline void portal_project_to_frame(const PortalFrame *frame, const float point[3],
                                           float *localRight, float *localUp,
                                           float *localDepth) {
    float relative[3] = {
        point[0] - frame->position[0], point[1] - frame->position[1],
        point[2] - frame->position[2],
    };
    if (localRight) *localRight = portal_dot3(relative, frame->right);
    if (localUp) *localUp = portal_dot3(relative, frame->up);
    if (localDepth) *localDepth = portal_dot3(relative, frame->normal);
}

static inline int portal_point_in_triangle_2d(float x, float y,
                                              const float a[2], const float b[2],
                                              const float c[2]) {
    float v0x = c[0] - a[0], v0y = c[1] - a[1];
    float v1x = b[0] - a[0], v1y = b[1] - a[1];
    float v2x = x - a[0], v2y = y - a[1];
    float d00 = v0x * v0x + v0y * v0y;
    float d01 = v0x * v1x + v0y * v1y;
    float d02 = v0x * v2x + v0y * v2y;
    float d11 = v1x * v1x + v1y * v1y;
    float d12 = v1x * v2x + v1y * v2y;
    float denominator = d00 * d11 - d01 * d01;
    float u, v;
    if (denominator <= 0.000001f) return 0;
    u = (d11 * d02 - d01 * d12) / denominator;
    v = (d00 * d12 - d01 * d02) / denominator;
    return u >= -0.0001f && v >= -0.0001f && u + v <= 1.0001f;
}

static inline int portal_triangle_matches_frame(const PortalFrame *frame,
                                                const PortalTriangle *triangle,
                                                float minimumNormalDot,
                                                float maximumPlaneDeviation) {
    int i;
    float alignment = fabsf(portal_dot3(frame->normal, triangle->normal));
    if (alignment < minimumNormalDot) return 0;
    for (i = 0; i < 3; ++i) {
        float depth;
        portal_project_to_frame(frame, triangle->vertex[i], NULL, NULL, &depth);
        if (fabsf(depth) > maximumPlaneDeviation) return 0;
    }
    return 1;
}

static inline int portal_point_in_triangle_frame(const PortalFrame *frame,
                                                 float localRight, float localUp,
                                                 const float vertices[3][3]) {
    float projected[3][2];
    int i;
    for (i = 0; i < 3; ++i)
        portal_project_to_frame(frame, vertices[i], &projected[i][0], &projected[i][1], NULL);
    return portal_point_in_triangle_2d(localRight, localUp,
                                       projected[0], projected[1], projected[2]);
}

/* Pure host-side oracle for a footprint covered by a union of collision
 * triangles. Sampling includes the full perimeter and interior; shared edges
 * are inclusive so a seam between triangles is not treated as a gap. */
static inline int portal_aperture_covered_by_triangles(const PortalFrame *frame,
                                                       const PortalTriangle *triangles,
                                                       unsigned triangleCount,
                                                       float halfWidth, float halfHeight,
                                                       float sampleSpacing,
                                                       float minimumNormalDot,
                                                       float maximumPlaneDeviation) {
    unsigned columns, rows, ix, iy, triangleIndex;
    if (!frame || !triangles || triangleCount == 0 || sampleSpacing <= 0.0f) return 0;
    columns = (unsigned)ceilf((2.0f * halfWidth) / sampleSpacing);
    rows = (unsigned)ceilf((2.0f * halfHeight) / sampleSpacing);
    if (columns == 0) columns = 1;
    if (rows == 0) rows = 1;
    for (iy = 0; iy <= rows; ++iy) {
        float y = -halfHeight + (2.0f * halfHeight * iy) / rows;
        for (ix = 0; ix <= columns; ++ix) {
            float x = -halfWidth + (2.0f * halfWidth * ix) / columns;
            int covered = 0;
            for (triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex) {
                const PortalTriangle *triangle = &triangles[triangleIndex];
                if (portal_triangle_matches_frame(frame, triangle, minimumNormalDot,
                                                  maximumPlaneDeviation)
                    && portal_point_in_triangle_frame(frame, x, y, triangle->vertex)) {
                    covered = 1;
                    break;
                }
            }
            if (!covered) return 0;
        }
    }
    return 1;
}

#endif
