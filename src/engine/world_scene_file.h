#ifndef WORLD_SCENE_FILE_H
#define WORLD_SCENE_FILE_H

#ifdef IOS_O2R_SCENE_RUNTIME
#include <stdbool.h>
#include <PR/gbi.h>
#include "world_scene.h"

#define WORLD_SCENE_FILE_MAX_BYTES (4u * 1024u * 1024u)
#define WORLD_SCENE_FILE_MAX_TRIANGLES 8192
#define WORLD_SCENE_GFX_TAG 0xd6730001u
#define WORLD_SCENE_STATE_OPCODE (((u32)G_NOOP << 24) | 0x673002u)

struct WorldSceneDrawState {
    s16 scrollS, scrollT; /* 1/32 texel per rendered frame */
    u8 tint[4];           /* multiplier for vertex RGBA */
};

/* Read scenes/<id>.wsc from the writable directory. The provider owns the
 * decoded Gfx and collision data until the last active room is removed. */
extern const struct WorldSceneProvider gWorldSceneFileProvider;

/* Called before freeing a package-owned external texture name. */
void gfx_forget_scene_texture(const char *name);
bool gfx_validate_scene_texture(const char *path, int width, int height);
#endif

#endif
