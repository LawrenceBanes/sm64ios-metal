#ifndef GFX_SHADER64_PACK_H
#define GFX_SHADER64_PACK_H

#include <stddef.h>

#define GFX_SHADER64_MAX_ARCHIVE_BYTES (8u * 1024u * 1024u)
#define GFX_SHADER64_MAX_SOURCE_BYTES (256u * 1024u)
#define GFX_SHADER64_MAX_PACK_FILES 10u

/* Installs only manifest.json and direct-child .metal files under shaders/.
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
