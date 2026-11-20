#include "pc/gfx/gfx_postprocess.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_identity(void) {
    const float identity[4][4] = {
        {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}
    };
    float inverse[4][4];
    assert(gfx_postprocess_invert_matrix(identity, inverse));
    assert(memcmp(identity, inverse, sizeof(identity)) == 0);
    float point[3];
    assert(gfx_postprocess_unproject_depth(inverse, .25f, .75f, .5f, point));
    assert(fabsf(point[0] + .5f) < 1e-6f);
    assert(fabsf(point[1] + .5f) < 1e-6f);
    assert(fabsf(point[2]) < 1e-6f);
}

static void test_projection_and_bad_inputs(void) {
    const float projection[4][4] = {
        {2,0,0,0}, {0,4,0,0}, {0,0,1,1}, {0,0,-1,0}
    };
    float inverse[4][4];
    assert(gfx_postprocess_invert_matrix(projection, inverse));
    float point[3];
    assert(gfx_postprocess_unproject_depth(inverse, .5f, .5f, .5f, point));
    assert(fabsf(point[0]) < 1e-6f && fabsf(point[1]) < 1e-6f);
    assert(isfinite(point[2]));
    const float singular[4][4] = {{0}};
    assert(!gfx_postprocess_invert_matrix(singular, inverse));
    assert(!gfx_postprocess_unproject_depth(inverse, .5f, .5f, 1.0f, point));
    assert(!gfx_postprocess_unproject_depth(inverse, NAN, .5f, .5f, point));
}

int main(void) {
    test_identity();
    test_projection_and_bad_inputs();
    puts("postprocess matrix inversion and depth reconstruction checks passed");
    return 0;
}
