#include "gfx_postprocess.h"

#include <math.h>

bool gfx_postprocess_invert_matrix(const float matrix[4][4], float inverse[4][4]) {
    if (!matrix || !inverse) return false;
    double work[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!isfinite(matrix[r][c])) return false;
            work[r][c] = matrix[r][c];
            work[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    }
    for (int c = 0; c < 4; ++c) {
        int pivot_row = c;
        for (int r = c + 1; r < 4; ++r)
            if (fabs(work[r][c]) > fabs(work[pivot_row][c])) pivot_row = r;
        if (fabs(work[pivot_row][c]) < 1.0e-12) return false;
        if (pivot_row != c) {
            for (int k = 0; k < 8; ++k) {
                double temporary = work[c][k];
                work[c][k] = work[pivot_row][k];
                work[pivot_row][k] = temporary;
            }
        }
        double pivot = work[c][c];
        for (int k = 0; k < 8; ++k) work[c][k] /= pivot;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            double factor = work[r][c];
            for (int k = 0; k < 8; ++k) work[r][k] -= factor * work[c][k];
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double value = work[r][c + 4];
            if (!isfinite(value) || fabs(value) > 3.402823466e+38) return false;
            inverse[r][c] = (float)value;
        }
    }
    return true;
}

bool gfx_postprocess_unproject_depth(const float inverse_projection[4][4],
                                     float u, float v, float depth,
                                     float position[3]) {
    if (!inverse_projection || !position || !isfinite(u) || !isfinite(v) ||
        !isfinite(depth) || u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f ||
        depth < 0.0f || depth >= 1.0f) return false;
    /* N64 clip Z is remapped from [-1,1] into Metal's [0,1] by gfx_pc.c. */
    const double clip[4] = { (double)u * 2.0 - 1.0,
                             1.0 - (double)v * 2.0,
                             (double)depth * 2.0 - 1.0, 1.0 };
    double view[4] = {0};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            view[c] += clip[r] * inverse_projection[r][c];
    if (!isfinite(view[3]) || fabs(view[3]) < 1.0e-12) return false;
    for (int c = 0; c < 3; ++c) {
        double value = view[c] / view[3];
        if (!isfinite(value) || fabs(value) > 1.0e9) return false;
        position[c] = (float)value;
    }
    return true;
}
