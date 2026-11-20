#ifndef GFX_SHADER64_PACK_H
#define GFX_SHADER64_PACK_H

#include <stddef.h>

#define GFX_SHADER64_MAX_ARCHIVE_BYTES (8u * 1024u * 1024u)
#define GFX_SHADER64_MAX_SOURCE_BYTES (256u * 1024u)
#define GFX_SHADER64_MAX_LIBRARY_BYTES (4u * 1024u * 1024u)
#define GFX_SHADER64_MAX_PACK_FILES 16u
#define GFX_SHADER64_MAX_PACK_TEXTURES 4u
#define GFX_SHADER64_MAX_PACK_TEXTURE_BYTES (4u * 1024u * 1024u)
#define GFX_SHADER64_MAX_GRAPH_PASSES 4u
#define GFX_SHADER64_MAX_GRAPH_TARGETS 3u
#define GFX_SHADER64_MAX_GRAPH_TARGET_BYTES (32u * 1024u * 1024u)
#define GFX_SHADER64_PARAMETER_ABS_MAX 16.0f
#define GFX_SHADER64_RESOURCE_SCENE_DEPTH 1u
#define GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT 2u
#define GFX_SHADER64_RESOURCE_SHADOW_MAP 4u
#define GFX_SHADER64_RESOURCE_PACK_TEXTURES 8u
#define GFX_SHADER64_RESOURCE_SCENE_NORMALS 16u
#define GFX_SHADER64_RESOURCE_FRAME_INFO 32u
#define GFX_SHADER64_NODE_FULLSCREEN 1u
#define GFX_SHADER64_NODE_COMPUTE 2u

/* API v2 exposes bounded fullscreen raster nodes and user compute nodes.
 * Compute kernels can opt into pack textures, scene depth, depth-derived
 * view-space normals, the configured directional light, and the native
 * directional shadow map using fixed non-overlapping bindings. Lighting
 * requires depth and normals; compute shadows additionally require lighting. */
struct GfxShader64GraphPass {
    const char *id;
    const char *input;
    const char *output;
    float resolution_scale;
    unsigned int node_type; /* GFX_SHADER64_NODE_FULLSCREEN or _COMPUTE */
    unsigned int resource_flags;
};
struct GfxShader64GraphSource {
    const char *id;
    const char *input;
    const char *output;
    const char *source;
    size_t source_size;
    float resolution_scale;
    unsigned int node_type;
    unsigned int resource_flags;
};

int gfx_shader64_validate_graph(const struct GfxShader64GraphPass *passes,
                                size_t pass_count, unsigned int width,
                                unsigned int height, unsigned char order[ GFX_SHADER64_MAX_GRAPH_PASSES ],
                                char *error, size_t error_size);
/* Uses the exact floor-to-pixel rule used by the Metal runtime. */
int gfx_shader64_graph_target_size(unsigned int width, unsigned int height,
                                   float resolution_scale, unsigned int *target_width,
                                   unsigned int *target_height);
int gfx_shader64_validate_pass_source(const char *source, size_t size,
                                      char *error, size_t error_size);
int gfx_shader64_validate_frame_pass_source(const char *source, size_t size,
                                            char *error, size_t error_size);
int gfx_shader64_validate_depth_pass_source(const char *source, size_t size,
                                            char *error, size_t error_size);
int gfx_shader64_validate_lit_pass_source(const char *source, size_t size,
                                          char *error, size_t error_size);
int gfx_shader64_validate_shadow_pass_source(const char *source, size_t size,
                                             char *error, size_t error_size);
int gfx_shader64_validate_textured_pass_source(const char *source, size_t size,
                                               char *error, size_t error_size);
int gfx_shader64_validate_material_source(const char *source, size_t size,
                                         char *error, size_t error_size);
int gfx_shader64_validate_material_texture_source(const char *source, size_t size,
                                                 char *error, size_t error_size);
int gfx_shader64_validate_normals_pass_source(const char *source, size_t size,
                                             char *error, size_t error_size);
int gfx_shader64_validate_normals_textured_pass_source(const char *source, size_t size,
                                                      char *error, size_t error_size);
int gfx_shader64_validate_compute_pass_source(const char *source, size_t size,
                                             char *error, size_t error_size);
int gfx_shader64_validate_compute_textured_pass_source(const char *source, size_t size,
                                                       char *error, size_t error_size);
int gfx_shader64_validate_compute_depth_pass_source(const char *source, size_t size,
                                                    char *error, size_t error_size);
int gfx_shader64_validate_compute_depth_textured_pass_source(const char *source, size_t size,
                                                             char *error, size_t error_size);
int gfx_shader64_validate_compute_resource_pass_source(const char *source, size_t size,
                                                       int pack_textures, int scene_depth,
                                                       int scene_normals, int scene_lighting,
                                                       int scene_shadows,
                                                       char *error, size_t error_size);
int gfx_shader64_build_pass_wrapper(const char *source, size_t source_size,
                                    char *output, size_t output_size);
int gfx_shader64_build_frame_pass_wrapper(const char *source, size_t source_size,
                                          char *output, size_t output_size);
int gfx_shader64_build_depth_pass_wrapper(const char *source, size_t source_size,
                                          char *output, size_t output_size);
int gfx_shader64_build_lit_pass_wrapper(const char *source, size_t source_size,
                                        char *output, size_t output_size);
int gfx_shader64_build_shadow_pass_wrapper(const char *source, size_t source_size,
                                           char *output, size_t output_size);
int gfx_shader64_build_textured_pass_wrapper(const char *source, size_t source_size,
                                             char *output, size_t output_size);
int gfx_shader64_build_normals_pass_wrapper(const char *source, size_t source_size,
                                            char *output, size_t output_size);
int gfx_shader64_build_normals_textured_pass_wrapper(const char *source, size_t source_size,
                                                     char *output, size_t output_size);

/* Installs manifest.json, direct-child .metal/.metallib files under shaders/,
 * and bounded direct-child .png files under textures/.
 * The destination must be a new private directory. Partial recognized files
 * are removed if extraction fails. */
int gfx_shader64_install_zip(const void *bytes, size_t size,
                             const char *destination,
                             char *error, size_t error_size);

/* Validates the stable API v1 post-composite source contract. */
int gfx_shader64_validate_post_source(const char *source, size_t size,
                                      char *error, size_t error_size);
int gfx_shader64_build_wrapper(const char *source, size_t source_size,
                               char *output, size_t output_size);

#endif
