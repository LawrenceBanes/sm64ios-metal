#ifndef GFX_POSTPROCESS_H
#define GFX_POSTPROCESS_H

#include <stdbool.h>

/* Matrices follow the RSP row-vector convention: clip = view * projection. */
bool gfx_postprocess_invert_matrix(const float matrix[4][4], float inverse[4][4]);

/* Reconstruct view-space position from Metal depth (0..1) and normalized
 * top-left-origin pixel coordinates. Returns false for clear/invalid depth. */
bool gfx_postprocess_unproject_depth(const float inverse_projection[4][4],
                                     float u, float v, float depth,
                                     float position[3]);
/* Projects a world-space direction (w=0) through row-vector view/projection matrices. */
bool gfx_postprocess_project_direction(const float view[4][4], const float projection[4][4],
                                       const float world_direction[3], float screen_uv[2]);

#endif
