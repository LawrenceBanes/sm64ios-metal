#ifndef GFX_SHADER64_PACK_H
#define GFX_SHADER64_PACK_H

#include <stddef.h>

#define GFX_SHADER64_MAX_ARCHIVE_BYTES (8u * 1024u * 1024u)
#define GFX_SHADER64_MAX_SOURCE_BYTES (256u * 1024u)
#define GFX_SHADER64_MAX_LIBRARY_BYTES (4u * 1024u * 1024u)
#define GFX_SHADER64_MAX_PACK_FILES 10u
#define GFX_SHADER64_MAX_GRAPH_PASSES 4u
#define GFX_SHADER64_MAX_GRAPH_TARGETS 3u
#define GFX_SHADER64_MAX_GRAPH_TARGET_BYTES (32u * 1024u * 1024u)
#define GFX_SHADER64_PARAMETER_ABS_MAX 16.0f

/* API v2 currently exposes bounded fullscreen raster nodes only. Geometry,
 * compute and external resources are intentionally rejected until the engine
 * can provide stable resource contracts for them. */
struct GfxShader64GraphPass {
    const char *id;
    const char *input;
    const char *output;
    float resolution_scale;
    unsigned int node_type; /* 1 = fullscreen raster */
};
struct GfxShader64GraphSource {
    const char *id;
    const char *input;
    const char *output;
    const char *source;
    size_t source_size;
    float resolution_scale;
    unsigned int node_type;
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
int gfx_shader64_build_pass_wrapper(const char *source, size_t source_size,
                                    char *output, size_t output_size);

/* Installs only manifest.json and direct-child .metal/.metallib files under shaders/.
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
