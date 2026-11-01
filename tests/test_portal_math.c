#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "game/portal_math.h"

static PortalFrame frame_at(const float position[3], const float normal[3],
                            const float preferredUp[3]) {
    PortalFrame frame = {0};
    frame.position[0] = position[0];
    frame.position[1] = position[1];
    frame.position[2] = position[2];
    assert(portal_frame_set_basis(&frame, normal, preferredUp));
    return frame;
}

static void test_wall_to_wall_momentum(void) {
    const float sourcePosition[3] = {0.0f, 100.0f, 0.0f};
    const float destinationPosition[3] = {500.0f, 200.0f, 100.0f};
    const float sourceNormal[3] = {0.0f, 0.0f, 1.0f};
    const float destinationNormal[3] = {1.0f, 0.0f, 0.0f};
    const float worldUp[3] = {0.0f, 1.0f, 0.0f};
    PortalFrame source = frame_at(sourcePosition, sourceNormal, worldUp);
    PortalFrame destination = frame_at(destinationPosition, destinationNormal, worldUp);
    float entering[3] = {0.0f, 0.0f, -40.0f};
    float exiting[3];
    float point[3] = {10.0f, 110.0f, -20.0f};
    float mappedPoint[3];

    portal_transform_vector(&source, &destination, entering, exiting);
    assert(fabsf(exiting[0] - 40.0f) < 0.001f);
    assert(fabsf(exiting[1]) < 0.001f);
    assert(fabsf(exiting[2]) < 0.001f);
    assert(fabsf(portal_length3(entering) - portal_length3(exiting)) < 0.001f);

    portal_transform_point(&source, &destination, point, 64.0f, mappedPoint);
    assert(fabsf(mappedPoint[0] - 584.0f) < 0.001f);
    assert(fabsf(mappedPoint[1] - 210.0f) < 0.001f);
    assert(fabsf(mappedPoint[2] - 110.0f) < 0.001f);
}

static void test_wall_and_floor_crossing(void) {
    const float wallPosition[3] = {0.0f, 0.0f, 0.0f};
    const float wallNormal[3] = {0.0f, 0.0f, 1.0f};
    const float floorNormal[3] = {0.0f, 1.0f, 0.0f};
    const float wallUp[3] = {0.0f, 1.0f, 0.0f};
    const float floorUp[3] = {0.0f, 0.0f, 1.0f};
    PortalFrame wall = frame_at(wallPosition, wallNormal, wallUp);
    PortalFrame floor = frame_at(wallPosition, floorNormal, floorUp);
    float previous[3] = {0.0f, 80.0f, 12.0f};
    float current[3] = {0.0f, 80.0f, -8.0f};
    float crossing = -1.0f;
    float velocity[3] = {0.0f, -40.0f, 0.0f};
    float mapped[3];

    assert(portal_segment_crosses(&wall, previous, current, 100.0f, 200.0f,
                                  35.0f, 160.0f, &crossing));
    assert(fabsf(crossing - 0.6f) < 0.001f);

    previous[0] = current[0] = 0.0f;
    previous[1] = 12.0f;
    current[1] = -8.0f;
    previous[2] = current[2] = 0.0f;
    assert(portal_segment_crosses(&floor, previous, current, 100.0f, 200.0f,
                                  35.0f, 160.0f, NULL));
    portal_transform_vector(&floor, &wall, velocity, mapped);
    assert(fabsf(mapped[0]) < 0.001f);
    assert(fabsf(mapped[1]) < 0.001f);
    assert(fabsf(mapped[2] - 40.0f) < 0.001f);
    assert(fabsf(portal_length3(velocity) - portal_length3(mapped)) < 0.001f);

    /* Reverse-normal ceiling entry has the same world-space support. */
    {
        const float ceilingNormal[3] = {0.0f, -1.0f, 0.0f};
        PortalFrame ceiling = frame_at(wallPosition, ceilingNormal, floorUp);
        previous[1] = 12.0f;
        current[1] = -8.0f;
        assert(!portal_segment_crosses(&ceiling, previous, current, 100.0f, 200.0f,
                                      35.0f, 160.0f, NULL));
        previous[1] = -12.0f;
        current[1] = 8.0f;
        assert(portal_segment_crosses(&ceiling, previous, current, 100.0f, 200.0f,
                                      35.0f, 160.0f, NULL));
    }
}

static void test_sloped_frame_basis_and_momentum(void) {
    const float position[3] = {30.0f, 50.0f, -20.0f};
    const float normal[3] = {0.3f, 0.8f, 0.52f};
    const float worldUp[3] = {0.0f, 1.0f, 0.0f};
    const float otherNormal[3] = {-0.4f, 0.2f, 0.89f};
    PortalFrame slope = frame_at(position, normal, worldUp);
    PortalFrame other = frame_at(position, otherNormal, worldUp);
    const float velocity[3] = {12.0f, -30.0f, 7.0f};
    float transformed[3];

    assert(fabsf(portal_length3(slope.normal) - 1.0f) < 0.001f);
    assert(fabsf(portal_length3(slope.right) - 1.0f) < 0.001f);
    assert(fabsf(portal_length3(slope.up) - 1.0f) < 0.001f);
    assert(fabsf(portal_dot3(slope.normal, slope.right)) < 0.001f);
    assert(fabsf(portal_dot3(slope.normal, slope.up)) < 0.001f);
    assert(fabsf(portal_dot3(slope.right, slope.up)) < 0.001f);
    portal_transform_vector(&slope, &other, velocity, transformed);
    assert(fabsf(portal_length3(velocity) - portal_length3(transformed)) < 0.001f);
}

static void test_aperture_capsule_edges(void) {
    const float p[3] = {0.0f, 0.0f, 0.0f};
    const float n[3] = {0.0f, 0.0f, 1.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    PortalFrame portal = frame_at(p, n, up);
    float previous[3] = {0.0f, 80.0f, 12.0f};
    float current[3] = {0.0f, 80.0f, -8.0f};

    assert(portal_segment_crosses(&portal, previous, current, 90.0f, 200.0f,
                                  35.0f, 160.0f, NULL));
    previous[0] = current[0] = 60.0f;
    assert(!portal_segment_crosses(&portal, previous, current, 90.0f, 200.0f,
                                   35.0f, 160.0f, NULL));
    previous[0] = current[0] = 0.0f;
    previous[1] = current[1] = 130.0f;
    assert(!portal_segment_crosses(&portal, previous, current, 90.0f, 200.0f,
                                   35.0f, 160.0f, NULL));
}

static void test_coplanar_triangle_union(void) {
    const float p[3] = {0.0f, 0.0f, 0.0f};
    const float n[3] = {0.0f, 0.0f, 1.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    PortalFrame frame = frame_at(p, n, up);
    PortalTriangle splitWall[4] = {
        {{{-100,-200,0},{0,-200,0},{0,200,0}}, {0,0,1}},
        {{{-100,-200,0},{0,200,0},{-100,200,0}}, {0,0,1}},
        {{{0,-200,0},{100,-200,0},{100,200,0}}, {0,0,1}},
        {{{0,-200,0},{100,200,0},{0,200,0}}, {0,0,1}},
    };
    PortalTriangle gapWall[4];
    unsigned i;

    assert(portal_aperture_covered_by_triangles(&frame, splitWall, 4, 78.0f, 180.0f,
                                               8.0f, 0.99f, 8.0f));
    for (i = 0; i < 4; ++i) gapWall[i] = splitWall[i];
    /* A real narrow geometry gap is rejected, while the triangle seams above
     * are accepted because their shared edges are inclusive. */
    gapWall[2].vertex[0][0] = 12.0f;
    gapWall[2].vertex[1][0] = 100.0f;
    gapWall[2].vertex[2][0] = 100.0f;
    gapWall[3].vertex[0][0] = 12.0f;
    gapWall[3].vertex[2][0] = 12.0f;
    assert(!portal_aperture_covered_by_triangles(&frame, gapWall, 4, 78.0f, 180.0f,
                                                8.0f, 0.99f, 8.0f));

    /* A mildly tilted neighboring patch is acceptable within the plane
     * tolerance; a different plane cannot silently fill the opening. */
    splitWall[0].vertex[0][2] = 3.0f;
    assert(portal_triangle_matches_frame(&frame, &splitWall[0], 0.99f, 8.0f));
    splitWall[0].vertex[0][2] = 20.0f;
    assert(!portal_triangle_matches_frame(&frame, &splitWall[0], 0.99f, 8.0f));
}

static void test_floor_ceiling_and_slope_coverage(void) {
    const float center[3] = {0.0f, 0.0f, 0.0f};
    const float worldUp[3] = {0.0f, 1.0f, 0.0f};
    const float horizontalAim[3] = {0.0f, 0.0f, -1.0f};
    const float floorNormal[3] = {0.0f, 1.0f, 0.0f};
    const float ceilingNormal[3] = {0.0f, -1.0f, 0.0f};
    const float slopeNormal[3] = {0.35f, 0.8f, -0.48f};
    PortalFrame frames[3];
    PortalTriangle triangles[2];
    int i;
    frames[0] = frame_at(center, floorNormal, horizontalAim);
    frames[1] = frame_at(center, ceilingNormal, horizontalAim);
    frames[2] = frame_at(center, slopeNormal, worldUp);
    for (i = 0; i < 3; ++i) {
        float corners[4][3];
        int c;
        const int rightSign[4] = {-1, 1, 1, -1};
        const int upSign[4] = {-1, -1, 1, 1};
        for (c = 0; c < 4; ++c) {
            corners[c][0] = frames[i].position[0] + rightSign[c] * 100.0f * frames[i].right[0] + upSign[c] * 200.0f * frames[i].up[0];
            corners[c][1] = frames[i].position[1] + rightSign[c] * 100.0f * frames[i].right[1] + upSign[c] * 200.0f * frames[i].up[1];
            corners[c][2] = frames[i].position[2] + rightSign[c] * 100.0f * frames[i].right[2] + upSign[c] * 200.0f * frames[i].up[2];
        }
        for (c = 0; c < 3; ++c) {
            triangles[0].vertex[0][c] = corners[0][c];
            triangles[0].vertex[1][c] = corners[1][c];
            triangles[0].vertex[2][c] = corners[2][c];
            triangles[1].vertex[0][c] = corners[0][c];
            triangles[1].vertex[1][c] = corners[2][c];
            triangles[1].vertex[2][c] = corners[3][c];
            triangles[0].normal[c] = frames[i].normal[c];
            triangles[1].normal[c] = -frames[i].normal[c];
        }
        assert(portal_aperture_covered_by_triangles(&frames[i], triangles, 2,
                                                    78.0f, 180.0f, 8.0f,
                                                    0.99f, 8.0f));
    }
}

static void test_exit_safety(void) {
    const float requested[3] = {10.0f, 20.0f, 30.0f};
    const float smallNudge[3] = {45.0f, 20.0f, 30.0f};
    const float blockedExit[3] = {91.0f, 20.0f, 30.0f};
    float groundedFeet = 100.0f;
    float airborneFeet = 100.0f;

    assert(portal_exit_correction_is_safe(requested, smallNudge, 70.0f));
    assert(!portal_exit_correction_is_safe(requested, blockedExit, 70.0f));
    assert(!portal_exit_resolve_floor(&groundedFeet, 160.0f, 0, 0, 32.0f));
    assert(portal_exit_resolve_floor(&airborneFeet, 0.0f, 0, 1, 32.0f));
    assert(portal_exit_resolve_floor(&groundedFeet, 120.0f, 1, 0, 32.0f));
    assert(fabsf(groundedFeet - 120.0f) < 0.001f);
    assert(!portal_exit_resolve_floor(&groundedFeet, 200.0f, 1, 0, 32.0f));
    assert(portal_exit_resolve_floor(&airborneFeet, 20.0f, 1, 1, 32.0f));
    assert(!portal_exit_resolve_floor(&airborneFeet, 120.0f, 1, 1, 32.0f));
}

int main(void) {
    test_wall_to_wall_momentum();
    test_wall_and_floor_crossing();
    test_sloped_frame_basis_and_momentum();
    test_aperture_capsule_edges();
    test_coplanar_triangle_union();
    test_floor_ceiling_and_slope_coverage();
    test_exit_safety();
    puts("Portal wall/floor/ceiling/slope transforms, multi-triangle coverage, aperture, and exit-safety tests passed");
    return 0;
}
