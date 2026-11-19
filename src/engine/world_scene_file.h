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

#define WORLD_SCENE_MATERIAL_OPCODE (((u32)G_NOOP << 24) | 0x673003u)

enum WorldSceneMaterialFlags {
    WORLD_SCENE_DEPTH_TEST = 1, WORLD_SCENE_DEPTH_WRITE = 2,
    WORLD_SCENE_CUTOUT = 4, WORLD_SCENE_BACK_CULL = 8, WORLD_SCENE_BLEND = 16,
};

struct WorldSceneTexture {
    char name[96];
    u16 width, height;
    s16 scale[2], origin[2], scroll[2];
    u8 address[2], filter;
};

struct WorldSceneMaterial {
    struct WorldSceneTexture texture[2];
    u8 flags, primitive[4], environment[4], alphaPeak;
    u16 alphaPeriod;
};

bool gfx_supports_scene_materials(void);

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
