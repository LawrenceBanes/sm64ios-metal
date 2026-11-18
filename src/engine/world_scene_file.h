#ifndef WORLD_SCENE_FILE_H
#define WORLD_SCENE_FILE_H

#ifdef IOS_O2R_SCENE_RUNTIME
#include <PR/gbi.h>
#include "world_scene.h"

#define WORLD_SCENE_FILE_MAX_BYTES (4u * 1024u * 1024u)
#define WORLD_SCENE_FILE_MAX_TRIANGLES 8192
#define WORLD_SCENE_GFX_TAG 0xd6730001u

/* Read scenes/<id>.wsc from the writable directory. The provider owns the
 * decoded Gfx and collision data until the last active room is removed. */
extern const struct WorldSceneProvider gWorldSceneFileProvider;

/* Called before freeing a package-owned external texture name. */
void gfx_forget_scene_texture(const char *name);
#endif

#endif
