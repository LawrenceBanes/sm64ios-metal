#ifndef PORTAL_MATH_H
#define PORTAL_MATH_H

/* Small renderer-independent portal transform helpers shared by gameplay and
 * host tests. SM64 world axes are X/Z horizontal and +Y up. */
typedef struct PortalFrame {
    float position[3];
    float normal[3];
} PortalFrame;

static inline float portal_dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void portal_right(const PortalFrame *frame, float right[3]) {
    /* Cross(+Y, outward normal): local +X is portal-right when viewed from
     * the front. */
    right[0] = frame->normal[2];
    right[1] = 0.0f;
    right[2] = -frame->normal[0];
}

static inline void portal_transform_vector(const PortalFrame *from, const PortalFrame *to,
                                           const float input[3], float output[3]) {
    float sourceRight[3];
    float targetRight[3];
    float localX;
    float localY;
    float localZ;

    portal_right(from, sourceRight);
    portal_right(to, targetRight);
    localX = portal_dot3(input, sourceRight);
    localY = input[1];
    localZ = portal_dot3(input, from->normal);

    /* A half turn around the portal's up axis maps entry motion into motion
     * out of the destination while preserving the velocity magnitude. */
    output[0] = -localX * targetRight[0] - localZ * to->normal[0];
    output[1] = localY;
    output[2] = -localX * targetRight[2] - localZ * to->normal[2];
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

/* Collision resolution may nudge an exit out of nearby geometry, but a large
 * correction means the portal would place the player inside an obstruction. */
static inline int portal_exit_correction_is_safe(const float requested[3],
                                                  const float resolved[3],
                                                  float maximumCorrection) {
    const float dx = requested[0] - resolved[0];
    const float dy = requested[1] - resolved[1];
    const float dz = requested[2] - resolved[2];
    return dx * dx + dy * dy + dz * dz <= maximumCorrection * maximumCorrection;
}

/* Returns true only for a front-to-back crossing through the clear part of a
 * rectangular wall aperture. Coordinates use Mario's feet as the Y position. */
static inline int portal_segment_crosses(const PortalFrame *frame, const float previous[3],
                                         const float current[3], float halfWidth,
                                         float bottom, float top, float marioRadius,
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
    float t;
    float x;
    float feetY;
    float right[3];
    float crossing[3];

    if (startDistance <= 0.0f || endDistance > 0.0f || startDistance == endDistance)
        return 0;

    t = startDistance / (startDistance - endDistance);
    crossing[0] = previous[0] + (current[0] - previous[0]) * t - frame->position[0];
    crossing[1] = previous[1] + (current[1] - previous[1]) * t - frame->position[1];
    crossing[2] = previous[2] + (current[2] - previous[2]) * t - frame->position[2];
    portal_right(frame, right);
    x = portal_dot3(crossing, right);
    feetY = crossing[1];

    if (x < -halfWidth + marioRadius || x > halfWidth - marioRadius
        || feetY < bottom || feetY + marioHeight > top)
        return 0;

    if (crossingT != 0) *crossingT = t;
    return 1;
}

static inline int portal_point_in_triangle_xz(const float point[3], const float a[3],
                                              const float b[3], const float c[3]) {
    const float v0x = c[0] - a[0], v0y = c[1] - a[1], v0z = c[2] - a[2];
    const float v1x = b[0] - a[0], v1y = b[1] - a[1], v1z = b[2] - a[2];
    const float v2x = point[0] - a[0], v2y = point[1] - a[1], v2z = point[2] - a[2];
    const float d00 = v0x * v0x + v0y * v0y + v0z * v0z;
    const float d01 = v0x * v1x + v0y * v1y + v0z * v1z;
    const float d02 = v0x * v2x + v0y * v2y + v0z * v2z;
    const float d11 = v1x * v1x + v1y * v1y + v1z * v1z;
    const float d12 = v1x * v2x + v1y * v2y + v1z * v2z;
    const float denominator = d00 * d11 - d01 * d01;
    float u, v;

    if (denominator <= 0.000001f) return 0;
    u = (d11 * d02 - d01 * d12) / denominator;
    v = (d00 * d12 - d01 * d02) / denominator;
    return u >= -0.0001f && v >= -0.0001f && u + v <= 1.0001f;
}

static inline int portal_aperture_fits_triangle(const PortalFrame *frame, const float a[3],
                                                 const float b[3], const float c[3],
                                                 float halfWidth, float halfHeight) {
    float right[3];
    float corner[3];
    int sx, sy;

    portal_right(frame, right);
    for (sx = -1; sx <= 1; sx += 2) {
        for (sy = -1; sy <= 1; sy += 2) {
            corner[0] = frame->position[0] + sx * halfWidth * right[0];
            corner[1] = frame->position[1] + sy * halfHeight;
            corner[2] = frame->position[2] + sx * halfWidth * right[2];
            if (!portal_point_in_triangle_xz(corner, a, b, c)) return 0;
        }
    }
    return 1;
}

#endif
