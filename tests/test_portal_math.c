#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "game/portal_math.h"

static void test_wall_to_wall_momentum(void) {
    PortalFrame source = {{0.0f, 100.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    PortalFrame destination = {{500.0f, 200.0f, 100.0f}, {1.0f, 0.0f, 0.0f}};
    float entering[3] = {0.0f, 0.0f, -40.0f};
    float exiting[3];
    float point[3] = {10.0f, 110.0f, -20.0f};
    float mappedPoint[3];

    portal_transform_vector(&source, &destination, entering, exiting);
    assert(fabsf(exiting[0] - 40.0f) < 0.001f);
    assert(fabsf(exiting[1]) < 0.001f);
    assert(fabsf(exiting[2]) < 0.001f);
    assert(fabsf(sqrtf(portal_dot3(entering, entering))
                 - sqrtf(portal_dot3(exiting, exiting))) < 0.001f);

    portal_transform_point(&source, &destination, point, 64.0f, mappedPoint);
    assert(fabsf(mappedPoint[0] - 584.0f) < 0.001f);
    assert(fabsf(mappedPoint[1] - 210.0f) < 0.001f);
    assert(fabsf(mappedPoint[2] - 110.0f) < 0.001f);
}

static void test_aperture_crossing_and_edge_rejection(void) {
    PortalFrame portal = {{0.0f, 120.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    float previous[3] = {0.0f, 0.0f, 12.0f};
    float current[3] = {0.0f, 0.0f, -8.0f};
    float t = -1.0f;

    assert(portal_segment_crosses(&portal, previous, current, 90.0f, -120.0f, 120.0f,
                                  35.0f, 160.0f, &t));
    assert(fabsf(t - 0.6f) < 0.001f);

    previous[0] = 60.0f;
    current[0] = 60.0f;
    assert(!portal_segment_crosses(&portal, previous, current, 90.0f, -120.0f, 120.0f,
                                   35.0f, 160.0f, NULL));
    previous[0] = current[0] = 0.0f;
    previous[2] = -12.0f;
    current[2] = 8.0f;
    assert(!portal_segment_crosses(&portal, previous, current, 90.0f, -120.0f, 120.0f,
                                   35.0f, 160.0f, NULL));
}

static void test_aperture_must_fit_host_triangle(void) {
    PortalFrame portal = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    float a[3] = {-100.0f, -200.0f, 0.0f};
    float b[3] = {100.0f, -200.0f, 0.0f};
    float c[3] = {0.0f, 200.0f, 0.0f};

    assert(portal_aperture_fits_triangle(&portal, a, b, c, 35.0f, 60.0f));
    assert(!portal_aperture_fits_triangle(&portal, a, b, c, 90.0f, 150.0f));
}

static void test_exit_collision_correction(void) {
    const float requested[3] = {10.0f, 20.0f, 30.0f};
    const float smallNudge[3] = {45.0f, 20.0f, 30.0f};
    const float blockedExit[3] = {91.0f, 20.0f, 30.0f};

    assert(portal_exit_correction_is_safe(requested, smallNudge, 70.0f));
    assert(!portal_exit_correction_is_safe(requested, blockedExit, 70.0f));
}

int main(void) {
    test_wall_to_wall_momentum();
    test_aperture_crossing_and_edge_rejection();
    test_aperture_must_fit_host_triangle();
    test_exit_collision_correction();
    puts("Portal frame, momentum, crossing, aperture, and exit-clearance tests passed");
    return 0;
}
