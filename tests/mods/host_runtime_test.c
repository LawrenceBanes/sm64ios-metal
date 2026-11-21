#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#ifdef EXTERNAL_DATA
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#endif

#ifdef EXTERNAL_DATA
#define TEST_DYNOS_TEX_SIZE 3
#else
#define TEST_DYNOS_TEX_SIZE 2
#endif
#define TEST_STRINGIFY_INNER(value) #value
#define TEST_STRINGIFY(value) TEST_STRINGIFY_INNER(value)
#include <sys/stat.h>
#include <unistd.h>

#include "sm64.h"
#include "audio_defines.h"
#include "level_commands.h"
#include "macro_preset_types.h"
#include "game/area.h"
#include "game/interaction.h"
#include "game/camera.h"
#include "game/mario.h"
#include "pc/mods/mod_audio.h"
#include "game/save_file.h"
#include "game/sound_init.h"
#include "game/level_update.h"
#include "game/object_helpers.h"
#include "game/mario_misc.h"
#include "game/behavior_actions.h"
#include "game/object_list_processor.h"
#include "engine/surface_collision.h"
#include "engine/math_util.h"
#include "game/rendering_graph_node.h"
#include "game/game_init.h"
#include "game/memory.h"
#include "pc/gfx/gfx_pc.h"
#include "actors/common1.h"
#include "game/print.h"
#include "game/spawn_object.h"
#include "pc/fs/fs.h"
#include "pc/mods/mod_runtime.h"

char fs_gamedir[SYS_MAX_PATH] = "";
char fs_writepath[SYS_MAX_PATH] = "";
struct HudDisplay gHudDisplay;
static s16 sHudCameraStatus;
s16 get_hud_camera_status(void) { return sHudCameraStatus; }
void set_hud_camera_status(s16 status) { sHudCameraStatus = status; }
struct MarioState gMarioStates[1];
struct MarioState *gMarioState = &gMarioStates[0];
struct PlayerCameraState gPlayerCameraState[2];
struct LakituState gLakituState;
struct Object *gCurrentObject;
struct Camera *gCamera;
const Collision warp_pipe_seg3_collision_03009AC8[] = { 0 };
f32 get_sound_pan(f32 x, f32 z) { (void)x; (void)z; return 0.5f; }
struct ObjectNode gObjectListArray[NUM_OBJ_LISTS];
struct ObjectNode *gObjectLists = gObjectListArray;
static struct GraphNode *sLoadedGraphNodeStorage[0x100];
struct GraphNode **gLoadedGraphNodes = sLoadedGraphNodeStorage;
Gfx *gDisplayListHead;
u8 *gGfxPoolEnd;
struct GfxDimensions gfx_current_dimensions = { 640, 480, 4.0f / 3.0f };
void *alloc_only_pool_alloc(struct AllocOnlyPool *pool, s32 size) {
    if (!pool || size <= 0) return NULL;
    size = (size + 7) & ~7;
    if (size > pool->totalSpace - pool->usedSpace) return NULL;
    void *result = pool->freePtr;
    pool->freePtr += size;
    pool->usedSpace += size;
    return result;
}
void *vec3s_copy(Vec3s dest, Vec3s src) {
    memcpy(dest, src, sizeof(Vec3s));
    return dest;
}
void *vec3s_set(Vec3s dest, s16 x, s16 y, s16 z) {
    dest[0] = x; dest[1] = y; dest[2] = z;
    return dest;
}
Gfx *geo_switch_anim_state(s32 callContext, struct GraphNode *node, void *context) {
    (void)callContext; (void)node; (void)context;
    return NULL;
}
Gfx *geo_update_layer_transparency(s32 callContext, struct GraphNode *node, void *context) {
    (void)callContext; (void)node; (void)context;
    return NULL;
}
#define TEST_GEO_CALLBACK(name) \
    Gfx *name(s32 callContext, struct GraphNode *node, Mat4 *matrix) { \
        (void)callContext; (void)node; (void)matrix; return NULL; \
    }
TEST_GEO_CALLBACK(geo_mirror_mario_set_alpha)
TEST_GEO_CALLBACK(geo_switch_mario_eyes)
TEST_GEO_CALLBACK(geo_mario_head_rotation)
TEST_GEO_CALLBACK(geo_switch_mario_hand)
TEST_GEO_CALLBACK(geo_switch_mario_cap_effect)
TEST_GEO_CALLBACK(geo_switch_mario_cap_on_off)
TEST_GEO_CALLBACK(geo_mario_rotate_wing_cap_wings)
TEST_GEO_CALLBACK(geo_mirror_mario_backface_culling)
#undef TEST_GEO_CALLBACK
Gfx *geo_switch_mario_hand_grab_pos(s32 callContext, struct GraphNode *node, Mat4 *matrix) {
    (void)callContext; (void)node; (void)matrix; return NULL;
}
Gfx *geo_move_mario_part_from_parent(s32 callContext, struct GraphNode *node, Mat4 matrix) {
    (void)callContext; (void)node; (void)matrix; return NULL;
}
f32 gGlobalSoundSource[3];
f32 gDefaultSoundArgs[3];
s16 gCurrSaveFileNum = 1;
s16 gCurrLevelNum;
s16 gCurrCourseNum;
s16 gCurrAreaIndex;
u8 gSpecialTripleJump;
f32 gSineTable[0x1400];
static f32 sLastRayStart[3];
static f32 sLastRayDirection[3];
static f32 sLastRayPrecision;
static struct Surface sRaycastSurface;
s16 atan2s(f32 y, f32 x) {
    const f32 scale = 32768.0f / acosf(-1.0f);
    return (s16)lrintf(atan2f(y, x) * scale);
}
s32 approach_s32(s32 current, s32 target, s32 inc, s32 dec) {
    if (current < target) {
        current += inc;
        if (current > target) current = target;
    } else {
        current -= dec;
        if (current < target) current = target;
    }
    return current;
}
void *vec3f_set(Vec3f dest, f32 x, f32 y, f32 z) {
    dest[0] = x; dest[1] = y; dest[2] = z;
    return NULL;
}
/* Minimal host support for the bounded production GeoLayout integration test. */
void mtxf_identity(Mat4 matrix) {
    memset(matrix, 0, sizeof(Mat4));
    for (int i = 0; i < 4; ++i) matrix[i][i] = 1.0f;
}
void mtxf_copy(Mat4 dest, Mat4 src) { memcpy(dest, src, sizeof(Mat4)); }
void mtxf_translate(Mat4 dest, Vec3f pos) {
    mtxf_identity(dest); dest[3][0] = pos[0]; dest[3][1] = pos[1]; dest[3][2] = pos[2];
}
void mtxf_mul(Mat4 out, Mat4 a, Mat4 b) {
    Mat4 result;
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        result[r][c] = 0.0f;
        for (int k = 0; k < 4; ++k) result[r][c] += a[r][k] * b[k][c];
    }
    memcpy(out, result, sizeof(result));
}
void mtxf_rotate_zxy_and_translate(Mat4 out, Vec3f pos, Vec3s angles) {
    (void)angles; mtxf_identity(out); out[3][0] = pos[0]; out[3][1] = pos[1]; out[3][2] = pos[2];
}
void mtxf_rotate_xyz_and_translate(Mat4 out, Vec3f pos, Vec3s angles) {
    mtxf_rotate_zxy_and_translate(out, pos, angles);
}
void *vec3s_to_vec3f(Vec3f dest, Vec3s src) {
    dest[0] = src[0]; dest[1] = src[1]; dest[2] = src[2];
    return NULL;
}
void mtxf_scale_vec3f(Mat4 out, Mat4 in, Vec3f scale) {
    memcpy(out, in, sizeof(Mat4));
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out[r][c] *= scale[c];
}
void mtxf_to_mtx(Mtx *out, Mat4 in) { memset(out, 0, sizeof(*out)); (void)in; }
void mtxf_billboard(Mat4 out, Mat4 in, Vec3f pos, s16 angle) {
    (void)in; (void)angle; mtxf_rotate_zxy_and_translate(out, pos, gVec3sZero);
}
void mtxf_cylboard(Mat4 out, Mat4 in, Vec3f pos, s16 angle) {
    mtxf_billboard(out, in, pos, angle);
}
void *alloc_display_list(u32 size) {
    size = (size + 7u) & ~7u;
    if (!gDisplayListHead || !gGfxPoolEnd || gGfxPoolEnd < (u8 *)gDisplayListHead + size) return NULL;
    gGfxPoolEnd -= size;
    return gGfxPoolEnd;
}
void *vec3f_copy(Vec3f dest, Vec3f src) {
    dest[0] = src[0]; dest[1] = src[1]; dest[2] = src[2];
    return NULL;
}
void *vec3f_normalize(Vec3f dest) {
    f32 inverse_length = 1.0f / sqrtf(dest[0] * dest[0] + dest[1] * dest[1] + dest[2] * dest[2]);
    dest[0] *= inverse_length; dest[1] *= inverse_length; dest[2] *= inverse_length;
    return NULL;
}
static s32 sLastSoundBits;
static s16 sLastSetAnimToFrame;
static s32 sLastAnimWithAccelTarget;
static s32 sLastAnimWithAccelValue;
static s16 sLastCameraModeFrames;
static unsigned sCameraShakeCalls;
static s16 sLastCameraShake;
static unsigned sCameraPointShakeCalls;
static s16 sLastCameraPointShake;
static f32 sLastCameraPointShakePosition[3];
static unsigned sTransitionCalls;
static s16 sLastTransitionType;
static s16 sLastTransitionTime;
static u8 sLastTransitionRgb[3];
static unsigned sCapMusicCalls;
static u16 sLastCapMusicSequence;
static unsigned sShellMusicCalls;
static unsigned sStopShellMusicCalls;
static unsigned sWarpTriggerCalls;
static struct MarioState *sLastWarpMario;
static s32 sLastWarpOperation;
static unsigned sMarioBonkReflectionCalls;
static struct MarioState *sLastBonkMario;
static u8 sLastBonkNegateSpeed;
static unsigned sMarioStopRidingCalls;
static unsigned sMarioStopRidingAndHoldingCalls;
static unsigned sHeavyLandingSoundCalls;
static struct MarioState *sLastHeavyLandingMario;
static u32 sLastHeavyLandingSoundBits;
static unsigned sMarioActionSoundCalls;
static struct MarioState *sLastMarioActionSoundMario;
static u32 sLastMarioActionSoundBits;
static u32 sLastMarioActionWaveParticle;
static unsigned sMarioFlagSoundCalls;
static u32 sLastMarioFlagSoundBits;
static unsigned sCourseClearCalls;
static unsigned sObjScaleCalls;
static struct Object *sLastObjScaleObject;
static f32 sLastObjScale;
static unsigned sFlipSoundCalls;
static struct MarioState *sLastFlipSoundMario;
static s16 sLastFlipSoundFrames[3];
static unsigned sWaterLevelQueries;
static f32 sLastWaterLevelX;
static f32 sLastWaterLevelZ;
static unsigned sMarioCharacterSoundCalls;
static s32 sLastMarioCharacterSoundBits;
static f32 sLastSoundPosition[3];
static s32 sLastCompatSoundBits;
static f32 sLastCompatSoundPosition[3];
static s32 sLastGraphSoundBits;
static f32 sLastGraphSoundPosition[3];
int gModHudTestRectCount;
int gModHudTestTextCount;
int gModHudTestTextureCount;
int gModHudTestTextureWidth;
int gModHudTestTextureHeight;
int gModHudTestTextureFormat;
int gModHudTestTextureSize;
bool gModHudTextureSawRotation;
float gModHudTestTextureScale[2];
int gModHudTestResolution;
int gModHudTestColor[4] = { 255, 255, 255, 255 };
u16 gModHudTestRotation;
float gModHudTestRotationPivot[2];
bool gModHudSawRotation;
s16 gMenuMode = -1;

static const u8 sTestBobStarName[] = {
    0x0B, 0x12, 0x10, 0x9E,
    0x0B, 0x18, 0x0B, 0x9F, 0x18, 0x16, 0x0B, 0x9E,
    0x18, 0x17, 0x9E, 0x1D, 0x11, 0x0E, 0x9E,
    0x1C, 0x1E, 0x16, 0x16, 0x12, 0x1D, 0xFF
};
static const u8 sTestBobCourseName[] = {
    0, 0, 0, /* native course menu metadata */
    0x0B, 0x32, 0x25, 0x9F, 0x32, 0x30, 0x25, 0x9E,
    0x0B, 0x24, 0x37, 0x37, 0x2F, 0x28, 0x29, 0x2C, 0x28, 0x2F, 0x27, 0xFF
};
/* The engine treats this segmented asset as an array of encoded pointers. */
u8 *seg2_act_name_table[90] = { [0] = (u8 *)sTestBobStarName };
u8 *seg2_course_name_table[25] = { [0] = (u8 *)sTestBobCourseName };

void mod_hud_reset_frame(void) {
    gModHudTestRectCount = 0;
    gModHudTestTextCount = 0;
    gModHudTestTextureCount = 0;
    gModHudTextureSawRotation = false;
    gModHudTestTextureWidth = 0;
    gModHudTestTextureHeight = 0;
    gModHudTestResolution = 0;
    gModHudTestColor[0] = gModHudTestColor[1] = gModHudTestColor[2] = gModHudTestColor[3] = 255;
    gModHudTestRotation = 0;
    gModHudTestRotationPivot[0] = gModHudTestRotationPivot[1] = 0.0f;
}
void mod_hud_reset_state(void) {
    gModHudTestResolution = 0;
    gModHudTestColor[0] = gModHudTestColor[1] = gModHudTestColor[2] = gModHudTestColor[3] = 255;
    gModHudTestRotation = 0;
    gModHudTestRotationPivot[0] = gModHudTestRotationPivot[1] = 0.0f;
}
bool mod_hud_set_resolution(int resolution) {
    if (resolution < 0 || resolution > 1) return false;
    gModHudTestResolution = resolution;
    return true;
}
bool mod_hud_set_font(int font) { return font >= -1 && font <= 6; }
void mod_hud_set_color(u8 r, u8 g, u8 b, u8 a) {
    gModHudTestColor[0] = r; gModHudTestColor[1] = g; gModHudTestColor[2] = b; gModHudTestColor[3] = a;
}
void mod_hud_reset_color(void) { mod_hud_set_color(255, 255, 255, 255); }
void mod_hud_set_rotation(u16 rotation, float pivot_x, float pivot_y) {
    gModHudTestRotation = rotation;
    gModHudTestRotationPivot[0] = pivot_x;
    gModHudTestRotationPivot[1] = pivot_y;
}
int mod_hud_get_screen_width(void) { return gModHudTestResolution == 0 ? 960 : 320; }
int mod_hud_get_screen_height(void) { return gModHudTestResolution == 0 ? 720 : 240; }
bool mod_hud_measure_text(const char *text, float *width, float *height) {
    if (!text || !width || !height) return false;
    *width = (float)strlen(text) * 8.0f;
    *height = 16.0f;
    return true;
}
bool mod_hud_render_rect(float x, float y, float width, float height) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) return false;
    gModHudSawRotation |= gModHudTestRotation != 0;
    ++gModHudTestRectCount;
    return true;
}
bool mod_hud_render_texture(const void *texture, int texture_width, int texture_height,
                            int format, int size, float x, float y, float scale_x, float scale_y) {
    if (!texture || texture_width <= 0 || texture_height <= 0 || !isfinite(x) || !isfinite(y) ||
        !isfinite(scale_x) || !isfinite(scale_y) || scale_x <= 0.0f || scale_y <= 0.0f ||
        (format != G_IM_FMT_RGBA && format != G_IM_FMT_IA) ||
        (size != G_IM_SIZ_16b && !(format == G_IM_FMT_RGBA && size == G_IM_SIZ_32b))) return false;
    ++gModHudTestTextureCount;
    gModHudTestTextureWidth = texture_width;
    gModHudTestTextureHeight = texture_height;
    gModHudTestTextureFormat = format;
    gModHudTestTextureSize = size;
    gModHudTextureSawRotation |= gModHudTestRotation != 0;
    gModHudTestTextureScale[0] = scale_x;
    gModHudTestTextureScale[1] = scale_y;
    return true;
}
bool mod_hud_print_text(const char *text, float x, float y, float scale_x, float scale_y) {
    if (!text || !isfinite(x) || !isfinite(y) || scale_x <= 0 || scale_y <= 0) return false;
    gModHudSawRotation |= gModHudTestRotation != 0;
    ++gModHudTestTextCount;
    return true;
}
u32 save_file_get_flags(void) { return 0; }
u32 save_file_get_star_flags(s32 file_index, s32 course_index) {
    (void)file_index; (void)course_index; return 0;
}

char *sys_strdup(const char *source) { return source ? strdup(source) : NULL; }
void *segmented_to_virtual(const void *address) { return (void *)address; }

static struct Object sMarioObject;
static struct Camera sCamera;
static struct Area sArea;
static struct Object sSpawnedObject;
static struct Animation sTestAnimationData[2];
static struct Animation *sTestAnimationPointers[2];
static s32 sLastCurObjAnimationIndex = -1;
static s16 sTestCollisionData[4];
static unsigned sCollisionModelCalls;
static unsigned sYellowLootCalls;
static struct Object *sLastYellowLootObject;
static s32 sLastYellowLootCount;
static f32 sLastYellowLootSpeed;
static u32 sGoombaRespawnInfo;
static struct Object sSpawnedNativeObjects[3];
static s32 sSpawnedNativeModels[3];
static unsigned sSpawnedNativeObjectCount;
static s32 sLastSpawnedModel;
static struct Object sGoombas[3];
static struct Object sOneUpObject;
static struct Object sSecretObject;
static struct Object sExclamationBoxObject;
static struct Object sWoodenPost;
const BehaviorScript bhvGoomba[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvBowser[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvTree[] = { ((uintptr_t)OBJ_LIST_POLELIKE << 16) };
const BehaviorScript bhv1Up[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvHiddenStarTrigger[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvHiddenRedCoinStar[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvBowserCourseRedCoinStar[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvHiddenStar[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvRedCoin[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvExclamationBox[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvWoodenPost[] = { ((uintptr_t)OBJ_LIST_SURFACE << 16) };
const BehaviorScript bhvMantaRayWaterRing[] = { ((uintptr_t)OBJ_LIST_LEVEL << 16) };
const BehaviorScript bhvMips[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvBowsersSub[] = { ((uintptr_t)OBJ_LIST_SURFACE << 16) };
const BehaviorScript bhvMovingYellowCoin[] = { ((uintptr_t)OBJ_LIST_LEVEL << 16) };
const BehaviorScript bhvYellowCoin[] = { ((uintptr_t)OBJ_LIST_LEVEL << 16) };
const BehaviorScript bhvKoopaShellUnderwater[] = { ((uintptr_t)OBJ_LIST_GENACTOR << 16) };
const BehaviorScript bhvPyramidPillarTouchDetector[] = { ((uintptr_t)OBJ_LIST_LEVEL << 16) };
const BehaviorScript bhvSpawnedStarNoLevelExit[] = { ((uintptr_t)OBJ_LIST_LEVEL << 16) };
/* Pointer-only engine symbols for source-probe startup. The native game links
 * the complete scripts from data/behavior_data.c; these host test identities
 * are never spawned or ticked. */
const BehaviorScript bhvMetalCap[] = { ((uintptr_t)OBJ_LIST_LEVEL << 16) };
const BehaviorScript bhvVanishCap[] = { ((uintptr_t)OBJ_LIST_LEVEL << 16) };
static const MacroObject sTestMacroObjects[] = { 31, 100, 200, 300, 0x1200, -1 };
struct MacroPreset MacroObjectPresets[] = { { bhvGoomba, MODEL_GOOMBA, 1 } };
const unsigned int gMacroObjectPresetCount = sizeof(MacroObjectPresets) / sizeof(MacroObjectPresets[0]);
static const LevelScript sTestLevelScript[] = {
    AREA(2, NULL),
    OBJECT(MODEL_GOOMBA, 10, 20, 30, 10, 90, 20, 0x12345678, bhvGoomba),
    MACRO_OBJECTS(sTestMacroObjects),
    END_AREA(),
    RETURN(),
};

const LevelScript *level_script_for_mod_api(s16 level_num) {
    return level_num >= LEVEL_MIN && level_num < LEVEL_COUNT ? sTestLevelScript : NULL;
}
static struct Controller sController;
static struct MarioBodyState sMarioBodyState;
static unsigned sPrintedText;

u32 get_object_list_from_behavior(const BehaviorScript *behavior) {
    return (u32)((behavior[0] >> 16) & 0xFFu);
}

f32 find_ceil(f32 x, f32 y, f32 z, struct Surface **ceiling) {
    if (ceiling) *ceiling = NULL;
    return x * 100.0f + y * 10.0f + z;
}

f32 find_floor(f32 x, f32 y, f32 z, struct Surface **floor) {
    if (floor) *floor = NULL;
    return -(x * 100.0f + y * 10.0f + z);
}

f32 find_water_level(f32 x, f32 z) {
    ++sWaterLevelQueries;
    sLastWaterLevelX = x;
    sLastWaterLevelZ = z;
    return x + z;
}

void obj_mark_for_deletion(struct Object *object) {
    object->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}

void obj_scale_random(struct Object *object, f32 range_length, f32 minimum_scale) {
    /* Deterministic substitute for the engine RNG in this host adapter test. */
    f32 scale = 0.25f * range_length + minimum_scale;
    object->header.gfx.scale[0] = scale;
    object->header.gfx.scale[1] = scale;
    object->header.gfx.scale[2] = scale;
}

void obj_scale_xyz(struct Object *object, f32 x_scale, f32 y_scale, f32 z_scale) {
    if (!object) return;
    object->header.gfx.scale[0] = x_scale;
    object->header.gfx.scale[1] = y_scale;
    object->header.gfx.scale[2] = z_scale;
}

void obj_set_pos(struct Object *object, s16 x, s16 y, s16 z) {
    object->oPosX = x;
    object->oPosY = y;
    object->oPosZ = z;
}

void obj_set_angle(struct Object *object, s16 pitch, s16 yaw, s16 roll) {
    object->oFaceAnglePitch = pitch;
    object->oFaceAngleYaw = yaw;
    object->oFaceAngleRoll = roll;
    object->oMoveAnglePitch = pitch;
    object->oMoveAngleYaw = yaw;
    object->oMoveAngleRoll = roll;
}

s16 obj_angle_to_object(struct Object *from, struct Object *to) {
    double angle = atan2((double)to->oPosZ - from->oPosZ, (double)to->oPosX - from->oPosX);
    return (s16)lrint(angle * (32768.0 / 3.14159265358979323846));
}

void mario_grab_used_object(struct MarioState *m) {
    if (m->heldObj == NULL) m->heldObj = m->usedObj;
}

void set_anim_to_frame(struct MarioState *m, s16 frame) {
    (void)m;
    sLastSetAnimToFrame = frame;
}

s16 set_mario_anim_with_accel(struct MarioState *m, s32 target_anim, s32 accel) {
    (void)m;
    sLastAnimWithAccelTarget = target_anim;
    sLastAnimWithAccelValue = accel;
    return (s16)target_anim;
}

void set_camera_mode(struct Camera *camera, s16 mode, s16 frames) {
    if (!mod_runtime_on_set_camera_mode(camera, mode, frames)) return;
    camera->mode = (u8)mode;
    sLastCameraModeFrames = frames;
}

void set_camera_shake_from_hit(s16 shake) {
    ++sCameraShakeCalls;
    sLastCameraShake = shake;
}

void set_camera_shake_from_point(s16 shake, f32 x, f32 y, f32 z) {
    ++sCameraPointShakeCalls;
    sLastCameraPointShake = shake;
    sLastCameraPointShakePosition[0] = x;
    sLastCameraPointShakePosition[1] = y;
    sLastCameraPointShakePosition[2] = z;
}

void play_transition(s16 type, s16 time, u8 red, u8 green, u8 blue) {
    ++sTransitionCalls;
    sLastTransitionType = type;
    sLastTransitionTime = time;
    sLastTransitionRgb[0] = red;
    sLastTransitionRgb[1] = green;
    sLastTransitionRgb[2] = blue;
}

s16 level_trigger_warp(struct MarioState *m, s32 operation) {
    ++sWarpTriggerCalls;
    sLastWarpMario = m;
    sLastWarpOperation = operation;
    return 17;
}

void play_cap_music(u16 sequence_args) {
    ++sCapMusicCalls;
    sLastCapMusicSequence = sequence_args;
}

void play_shell_music(void) {
    ++sShellMusicCalls;
}

void stop_shell_music(void) {
    ++sStopShellMusicCalls;
}

u32 interact_coin(struct MarioState *m, u32 interact_type, struct Object *object) {
    (void)interact_type;
    m->numCoins += object->oDamageOrCoinValue;
    m->healCounter += 4 * object->oDamageOrCoinValue;
    object->oInteractStatus = INT_STATUS_INTERACTED;
    return FALSE;
}

u32 process_interaction(struct MarioState *m, u32 interact_type, struct Object *object,
                        u32 (*interact_function)(struct MarioState *, u32, struct Object *)) {
    if (!m || !object || !interact_function || !mod_runtime_allow_interact(m, object, interact_type))
        return FALSE;
    bool interacted = interact_function(m, interact_type, object) != 0;
    mod_runtime_on_interact(m, object, interact_type, interacted);
    return interacted;
}

static void append_test_object(int list, struct Object *object, const BehaviorScript *behavior,
                               s16 active_flags, s32 action) {
    struct ObjectNode *head = &gObjectLists[list];
    memset(object, 0, sizeof(*object));
    object->behavior = behavior;
    object->activeFlags = active_flags;
    object->oAction = action;
    object->header.prev = head->prev;
    object->header.next = head;
    head->prev->next = &object->header;
    head->prev = &object->header;
}

f32 dist_between_objects(struct Object *left, struct Object *right) {
    f32 dx = left->oPosX - right->oPosX;
    f32 dy = left->oPosY - right->oPosY;
    f32 dz = left->oPosZ - right->oPosZ;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static bool real_mod_storage_contains(const char *root, const char *needle) {
    static const char hex[] = "0123456789abcdef";
    const char *name = "sm64coop-achievements-2.1.0";
    char path[SYS_MAX_PATH];
    int n = snprintf(path, sizeof(path), "%s/mods_storage/", root);
    if (n <= 0 || (size_t)n >= sizeof(path)) return false;
    size_t prefix = (size_t)n;
    for (size_t i = 0; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (prefix + i * 2 + 8 >= sizeof(path)) return false;
        path[prefix + i * 2] = hex[c >> 4];
        path[prefix + i * 2 + 1] = hex[c & 15];
    }
    snprintf(path + prefix + strlen(name) * 2, sizeof(path) - prefix - strlen(name) * 2, ".sav");
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char data[16384];
    size_t length = fread(data, 1, sizeof(data) - 1, file);
    bool ok = !ferror(file) && feof(file);
    fclose(file);
    if (!ok) return false;
    data[length] = '\0';
    bool found = strstr(data, needle) != NULL;
    if (!found) fprintf(stderr, "real mod storage missing '%s' in %s: %s\n", needle, path, data);
    return found;
}

static bool list_add(fs_pathlist_t *list, const char *path) {
    if (list->numpaths == list->listcap) {
        int next_cap = list->listcap ? list->listcap * 2 : 16;
        char **next = realloc(list->paths, sizeof(char *) * (size_t)next_cap);
        if (!next) return false;
        list->paths = next; list->listcap = next_cap;
    }
    list->paths[list->numpaths] = strdup(path);
    if (!list->paths[list->numpaths]) return false;
    list->numpaths++;
    return true;
}

static bool walk_dir(const char *base, walk_fn_t callback, void *user, bool recur) {
    DIR *dir = opendir(base);
    if (!dir) return false;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char full[SYS_MAX_PATH];
        int n = snprintf(full, sizeof(full), "%s/%s", base, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) { closedir(dir); return false; }
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (recur && !walk_dir(full, callback, user, recur)) { closedir(dir); return false; }
        } else if (S_ISREG(st.st_mode) && !callback(user, full)) { closedir(dir); return false; }
    }
    closedir(dir);
    return true;
}

bool fs_sys_walk(const char *base, walk_fn_t walk, void *user, const bool recur) {
    return walk_dir(base, walk, user, recur);
}

static bool enum_cb(void *user, const char *path) { return list_add(user, path); }

fs_pathlist_t fs_sys_enumerate(const char *base, const bool recur) {
    fs_pathlist_t list = {0};
    if (!walk_dir(base, enum_cb, &list, recur)) fs_pathlist_free(&list);
    return list;
}

void fs_pathlist_free(fs_pathlist_t *list) {
    if (!list) return;
    for (int i = 0; i < list->numpaths; ++i) free(list->paths[i]);
    free(list->paths); *list = (fs_pathlist_t){0};
}

bool fs_sys_dir_exists(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISDIR(st.st_mode); }
bool fs_sys_file_exists(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode); }
bool fs_sys_mkdir(const char *path) { return mkdir(path, 0777) == 0 || errno == EEXIST; }
bool fs_sys_copy_file(const char *src, const char *dst) { (void)src; (void)dst; return false; }

const char *fs_get_write_path(const char *path) {
    static char output[SYS_MAX_PATH];
    snprintf(output, sizeof(output), "%s/%s", fs_writepath, path);
    return output;
}

#ifndef MOD_RUNTIME_REAL_ZIP
bool fs_zip_extract_package(const char *zip_path, const char *mods_dir, const char *dynos_dir,
                            char *installed, size_t installed_size, bool *contains_lua,
                            bool *contains_dynos, char *error, size_t error_size) {
    (void)zip_path; (void)mods_dir; (void)dynos_dir; (void)installed; (void)installed_size;
    if (contains_lua) *contains_lua = false;
    if (contains_dynos) *contains_dynos = false;
    if (error && error_size) snprintf(error, error_size, "not part of runtime unit test");
    return false;
}
#endif

u32 set_mario_action(struct MarioState *m, u32 action, u32 arg) {
    m->prevAction = m->action; m->action = action; m->actionArg = arg;
    m->actionState = m->actionTimer = 0;
    return TRUE;
}

s32 perform_ground_step(struct MarioState *m) {
    assert(m == gMarioState);
    return GROUND_STEP_HIT_WALL;
}

s32 perform_air_step(struct MarioState *m, u32 step_arg) {
    assert(m == gMarioState);
    return (s32)(100 + step_arg);
}

void mario_bonk_reflection(struct MarioState *m, u32 negate_speed) {
    ++sMarioBonkReflectionCalls;
    sLastBonkMario = m;
    sLastBonkNegateSpeed = (u8)negate_speed;
}

void mario_stop_riding_object(struct MarioState *m) {
    ++sMarioStopRidingCalls;
    if (m->riddenObj) {
        m->riddenObj->oInteractStatus = INT_STATUS_STOP_RIDING;
        m->riddenObj = NULL;
    }
}

void mario_stop_riding_and_holding(struct MarioState *m) {
    assert(m == gMarioState);
    ++sMarioStopRidingAndHoldingCalls;
}

void play_mario_heavy_landing_sound(struct MarioState *m, u32 sound_bits) {
    ++sHeavyLandingSoundCalls;
    sLastHeavyLandingMario = m;
    sLastHeavyLandingSoundBits = sound_bits;
}

void play_mario_action_sound(struct MarioState *m, u32 sound_bits, u32 wave_particle_type) {
    ++sMarioActionSoundCalls;
    sLastMarioActionSoundMario = m;
    sLastMarioActionSoundBits = sound_bits;
    sLastMarioActionWaveParticle = wave_particle_type;
}

void play_course_clear(void) { ++sCourseClearCalls; }

void obj_scale(struct Object *object, f32 scale) {
    ++sObjScaleCalls;
    sLastObjScaleObject = object;
    sLastObjScale = scale;
}

void play_flip_sounds(struct MarioState *m, s16 frame1, s16 frame2, s16 frame3) {
    ++sFlipSoundCalls;
    sLastFlipSoundMario = m;
    sLastFlipSoundFrames[0] = frame1;
    sLastFlipSoundFrames[1] = frame2;
    sLastFlipSoundFrames[2] = frame3;
}

u32 perform_water_step(struct MarioState *m) {
    assert(m == gMarioState);
    return WATER_STEP_CANCELLED;
}

s32 mario_get_floor_class(struct MarioState *m) {
    assert(m == gMarioState);
    return SURFACE_CLASS_DEFAULT;
}

s16 set_mario_animation(struct MarioState *m, s32 id) { (void)m; return (s16)id; }

void cur_obj_init_animation(s32 animation_index) {
    assert(gCurrentObject && gCurrentObject->oAnimations);
    sLastCurObjAnimationIndex = animation_index;
    gCurrentObject->header.gfx.unk38.curAnim = gCurrentObject->oAnimations[animation_index];
}

void set_object_respawn_info_bits(struct Object *object, u8 bits) {
    if (!object || !object->respawnInfo) return;
    if (object->respawnInfoType == RESPAWN_INFO_TYPE_32)
        *(u32 *)object->respawnInfo |= (u32)bits << 8;
    else if (object->respawnInfoType == RESPAWN_INFO_TYPE_16)
        *(u16 *)object->respawnInfo |= (u16)bits << 8;
}

void load_object_collision_model(void) {
    assert(gCurrentObject && gCurrentObject->collisionData);
    ++sCollisionModelCalls;
}

void obj_spawn_loot_yellow_coins(struct Object *object, s32 count, f32 launch_speed) {
    ++sYellowLootCalls;
    sLastYellowLootObject = object;
    sLastYellowLootCount = count;
    sLastYellowLootSpeed = launch_speed;
    if (object && object->oNumLootCoins > 0) {
        s32 spawned = count < object->oNumLootCoins ? count : object->oNumLootCoins;
        object->oNumLootCoins -= spawned;
    }
}

struct Object *spawn_object(struct Object *parent, s32 model, const BehaviorScript *behavior) {
    if (behavior == bhvGoomba) {
        assert(sSpawnedNativeObjectCount < sizeof(sSpawnedNativeObjects) / sizeof(sSpawnedNativeObjects[0]));
        unsigned slot = sSpawnedNativeObjectCount++;
        struct Object *native_object = &sSpawnedNativeObjects[slot];
        sSpawnedNativeModels[slot] = model;
        append_test_object(OBJ_LIST_GENACTOR, native_object, behavior, ACTIVE_FLAG_ACTIVE, 0);
        native_object->parentObj = parent;
        sLastSpawnedModel = model;
        return native_object;
    }
    memset(&sSpawnedObject, 0, sizeof(sSpawnedObject));
    sSpawnedObject.parentObj = parent; sSpawnedObject.behavior = behavior;
    sSpawnedObject.activeFlags = ACTIVE_FLAG_ACTIVE;
    sLastSpawnedModel = model;
    return &sSpawnedObject;
}

struct Object *spawn_object_abs_with_rot(struct Object *parent, s16 spawn_type, u32 model,
    const BehaviorScript *behavior, s16 x, s16 y, s16 z, s16 pitch, s16 yaw, s16 roll) {
    (void)spawn_type;
    struct Object *obj = spawn_object(parent, (s32)model, behavior);
    obj->oPosX = x; obj->oPosY = y; obj->oPosZ = z;
    obj->oFaceAnglePitch = pitch; obj->oFaceAngleYaw = yaw; obj->oFaceAngleRoll = roll;
    return obj;
}

void print_text(s32 x, s32 y, const char *text) { (void)x; (void)y; (void)text; ++sPrintedText; }

void play_sound(s32 sound_bits, f32 *position) {
    sLastSoundBits = sound_bits;
    memcpy(sLastSoundPosition, position, sizeof(sLastSoundPosition));
    if (sound_bits == SOUND_MARIO_YAHOO || sound_bits == SOUND_MARIO_YAHOO + 2) {
        ++sMarioCharacterSoundCalls;
        sLastMarioCharacterSoundBits = sound_bits;
    }
    if (sound_bits == 0x1357) {
        sLastCompatSoundBits = sound_bits;
        memcpy(sLastCompatSoundPosition, position, sizeof(sLastCompatSoundPosition));
    } else if (sound_bits == 0x2468) {
        sLastGraphSoundBits = sound_bits;
        memcpy(sLastGraphSoundPosition, position, sizeof(sLastGraphSoundPosition));
    }
}

void play_sound_if_no_flag(struct MarioState *m, u32 sound_bits, u32 flags) {
    if (m && !(m->flags & flags)) {
        ++sMarioFlagSoundCalls;
        sLastMarioFlagSoundBits = sound_bits;
        play_sound(sound_bits, m->marioObj->header.gfx.cameraToObject);
        m->flags |= flags;
    }
}
void play_mario_jump_sound(struct MarioState *m) {
    if (m && !(m->flags & MARIO_MARIO_SOUND_PLAYED)) {
        play_sound(SOUND_MARIO_YAH_WAH_HOO, m->marioObj->header.gfx.cameraToObject);
        m->flags |= MARIO_MARIO_SOUND_PLAYED;
    }
}

static void reset_game_state(void) {
    for (size_t i = 0; i < sizeof(gSineTable) / sizeof(gSineTable[0]); ++i)
        gSineTable[i] = sinf((f32)i * (acosf(-1.0f) / 2048.0f));
    memset(gMarioStates, 0, sizeof(gMarioStates));
    memset(gPlayerCameraState, 0, sizeof(gPlayerCameraState));
    memset(&sMarioBodyState, 0, sizeof(sMarioBodyState));
    gSpecialTripleJump = 0;
    sLastSetAnimToFrame = INT16_MIN;
    sLastAnimWithAccelTarget = INT32_MIN;
    sLastAnimWithAccelValue = INT32_MIN;
    sLastCameraModeFrames = INT16_MIN;
    sCameraShakeCalls = 0;
    sLastCameraShake = INT16_MIN;
    sCameraPointShakeCalls = 0;
    sLastCameraPointShake = INT16_MIN;
    memset(sLastCameraPointShakePosition, 0, sizeof(sLastCameraPointShakePosition));
    sTransitionCalls = 0;
    sLastTransitionType = INT16_MIN;
    sLastTransitionTime = INT16_MIN;
    memset(sLastTransitionRgb, 0, sizeof(sLastTransitionRgb));
    sCapMusicCalls = 0;
    sLastCapMusicSequence = 0;
    sShellMusicCalls = 0;
    sStopShellMusicCalls = 0;
    sWarpTriggerCalls = 0;
    sLastWarpMario = NULL;
    sLastWarpOperation = -1;
    sMarioBonkReflectionCalls = 0;
    sLastBonkMario = NULL;
    sLastBonkNegateSpeed = 0;
    sMarioStopRidingCalls = 0;
    sMarioStopRidingAndHoldingCalls = 0;
    sHeavyLandingSoundCalls = 0;
    sLastHeavyLandingMario = NULL;
    sLastHeavyLandingSoundBits = 0;
    sWaterLevelQueries = 0;
    sLastWaterLevelX = 0;
    sLastWaterLevelZ = 0;
    sMarioCharacterSoundCalls = 0;
    sLastMarioCharacterSoundBits = 0;
    sMarioFlagSoundCalls = 0;
    sLastMarioFlagSoundBits = 0;
    memset(&sMarioObject, 0, sizeof(sMarioObject));
    memset(&sCamera, 0, sizeof(sCamera));
    memset(&sArea, 0, sizeof(sArea));
    memset(&gLakituState, 0, sizeof(gLakituState));
    gCamera = &sCamera;
    sArea.index = 1;
    sArea.camera = &sCamera;
    memset(&sSpawnedObject, 0, sizeof(sSpawnedObject));
    memset(sSpawnedNativeObjects, 0, sizeof(sSpawnedNativeObjects));
    memset(sSpawnedNativeModels, 0, sizeof(sSpawnedNativeModels));
    sSpawnedNativeObjectCount = 0;
    sLastSpawnedModel = -1;
    memset(sGoombas, 0, sizeof(sGoombas));
    memset(&sOneUpObject, 0, sizeof(sOneUpObject));
    memset(&sSecretObject, 0, sizeof(sSecretObject));
    memset(&sExclamationBoxObject, 0, sizeof(sExclamationBoxObject));
    memset(&sWoodenPost, 0, sizeof(sWoodenPost));
    memset(sLoadedGraphNodeStorage, 0, sizeof(sLoadedGraphNodeStorage));
    sLoadedGraphNodeStorage[MODEL_GOOMBA] = (struct GraphNode *)1;
    memset(gObjectListArray, 0, sizeof(gObjectListArray));
    for (int i = 0; i < NUM_OBJ_LISTS; ++i) {
        gObjectListArray[i].next = &gObjectListArray[i];
        gObjectListArray[i].prev = &gObjectListArray[i];
    }
    append_test_object(OBJ_LIST_GENACTOR, &sGoombas[0], bhvGoomba, ACTIVE_FLAG_ACTIVE, 101);
    append_test_object(OBJ_LIST_GENACTOR, &sGoombas[1], bhvGoomba, ACTIVE_FLAG_DEACTIVATED, 999);
    append_test_object(OBJ_LIST_GENACTOR, &sGoombas[2], bhvGoomba, ACTIVE_FLAG_ACTIVE, 202);
    append_test_object(OBJ_LIST_SURFACE, &sWoodenPost, bhvWoodenPost, ACTIVE_FLAG_ACTIVE, 0);
    append_test_object(OBJ_LIST_GENACTOR, &sOneUpObject, bhv1Up, ACTIVE_FLAG_ACTIVE, 303);
    append_test_object(OBJ_LIST_GENACTOR, &sSecretObject, bhvHiddenStarTrigger, ACTIVE_FLAG_ACTIVE, 404);
    append_test_object(OBJ_LIST_GENACTOR, &sExclamationBoxObject, bhvExclamationBox, ACTIVE_FLAG_ACTIVE, 1);
    sGoombaRespawnInfo = 0x01000001;
    sGoombas[0].oNumLootCoins = 5;
    sYellowLootCalls = 0;
    sLastYellowLootObject = NULL;
    sLastYellowLootCount = 0;
    sLastYellowLootSpeed = 0.0f;
    sGoombas[0].respawnInfoType = RESPAWN_INFO_TYPE_32;
    sGoombas[0].respawnInfo = &sGoombaRespawnInfo;
    memset(&sController, 0, sizeof(sController));
    sController.buttonDown = D_JPAD;
    memset(gGlobalSoundSource, 0, sizeof(gGlobalSoundSource));
    memset(gDefaultSoundArgs, 0, sizeof(gDefaultSoundArgs));
    sLastSoundBits = 0;
    memset(sLastSoundPosition, 0, sizeof(sLastSoundPosition));
    sLastCompatSoundBits = sLastGraphSoundBits = 0;
    memset(sLastCompatSoundPosition, 0, sizeof(sLastCompatSoundPosition));
    memset(sLastGraphSoundPosition, 0, sizeof(sLastGraphSoundPosition));
    gMarioState = &gMarioStates[0];
    gMarioState->area = &sArea;
    gMarioState->marioObj = &sMarioObject;
    sMarioObject.activeFlags = ACTIVE_FLAG_ACTIVE;
    gMarioState->controller = &sController;
    gMarioState->marioBodyState = &sMarioBodyState;
    gMarioState->statusForCamera = &gPlayerCameraState[0];
    gMarioState->action = ACT_IDLE;
    sController.buttonPressed = A_BUTTON | D_JPAD;
}

void find_surface_on_ray_with_precision(Vec3f origin, Vec3f direction,
                                        struct Surface **hit_surface, Vec3f hit_position,
                                        f32 precision) {
    memcpy(sLastRayStart, origin, sizeof(sLastRayStart));
    memcpy(sLastRayDirection, direction, sizeof(sLastRayDirection));
    sLastRayPrecision = precision;
    memset(&sRaycastSurface, 0, sizeof(sRaycastSurface));
    sRaycastSurface.type = 0x1234;
    sRaycastSurface.normal.x = 1.0f;
    *hit_surface = &sRaycastSurface;
    for (int axis = 0; axis < 3; ++axis)
        hit_position[axis] = origin[axis] + direction[axis] * 0.25f;
}

static int bytecode_writer(lua_State *L, const void *data, size_t size, void *userdata) {
    (void)L;
    return fwrite(data, 1, size, (FILE *)userdata) != size;
}

static void create_bytecode_fixture(const char *fixture_root) {
    char path[SYS_MAX_PATH];
    snprintf(path, sizeof(path), "%s/mods/01_require/scripts/precompiled.luac", fixture_root);
    lua_State *compiler = luaL_newstate();
    assert(compiler);
    assert(luaL_loadstring(compiler, "return 3") == LUA_OK);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(lua_dump(compiler, bytecode_writer, file, 0) == 0);
    assert(fclose(file) == 0);
    lua_close(compiler);
}

static void write_bytecode_script(const char *path, const char *source, unsigned other_size_t_width) {
    lua_State *compiler = luaL_newstate();
    assert(compiler);
    assert(luaL_loadstring(compiler, source) == LUA_OK);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(lua_dump(compiler, bytecode_writer, file, 0) == 0);
    assert(fclose(file) == 0);
    lua_close(compiler);
    if (other_size_t_width) {
        file = fopen(path, "r+b");
        assert(file);
        assert(fseek(file, 13, SEEK_SET) == 0);
        assert(fputc((int)other_size_t_width, file) != EOF);
        assert(fclose(file) == 0);
    }
}

static void create_architecture_variant_fixture(const char *fixture_root) {
    char directory[SYS_MAX_PATH], path[SYS_MAX_PATH];
    snprintf(directory, sizeof(directory), "%s/mods/07_architecture_variants", fixture_root);
    assert(fs_sys_mkdir(directory));
    snprintf(path, sizeof(path), "%s/main.lua", directory);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs("-- paired Lua 5.3 bytecode architecture fixture\n", file) >= 0);
    assert(fclose(file) == 0);

    bool is_64_bit = sizeof(size_t) == 8;
    snprintf(path, sizeof(path), "%s/%s", directory, is_64_bit ? "00-64.lua" : "00-32.lua");
    write_bytecode_script(path, "gMarioState.numCoins = gMarioState.numCoins + 64", 0);
    snprintf(path, sizeof(path), "%s/%s", directory, is_64_bit ? "01-32.lua" : "01-64.lua");
    write_bytecode_script(path, "gMarioState.numCoins = gMarioState.numCoins + 1000", is_64_bit ? 4 : 8);
}

static void write_u32_le(FILE *file, uint32_t value) {
    assert(fputc((int)(value & 0xff), file) != EOF);
    assert(fputc((int)((value >> 8) & 0xff), file) != EOF);
    assert(fputc((int)((value >> 16) & 0xff), file) != EOF);
    assert(fputc((int)((value >> 24) & 0xff), file) != EOF);
}

static void write_dynos_behavior(const char *path, const char *behavior_name,
                                 const char *callback_name, bool unsupported_command) {
    FILE *file = fopen(path, "wb");
    assert(file);
    size_t name_length = strlen(behavior_name);
    assert(name_length > 0 && name_length <= 240);
    assert(fputc(0x16, file) != EOF); /* DATA_TYPE_BEHAVIOR_SCRIPT */
    assert(fputc((int)name_length, file) != EOF);
    assert(fwrite(behavior_name, 1, name_length, file) == name_length);
    assert(fputc(1, file) != EOF && fputc(0, file) != EOF && fputc(1, file) != EOF);
    const uint32_t valid_script[] = {
        UINT32_C(0x000b0000), /* BEGIN(OBJ_LIST_SPAWNER) */
        UINT32_C(0x39000001), /* ID(1) */
        UINT32_C(0x11010001), /* OR_INT(0x01, 1) */
        UINT32_C(0x08000000), /* BEGIN_LOOP */
        UINT32_C(0x3c000000), /* CALL_NATIVE_EXT(token) */
        UINT32_C(0x5641554c), /* LUA_VAR_CODE("fixture_bhv_tick") */
        UINT32_C(0x09000000)  /* END_LOOP */
    };
    const uint32_t rejected_script[] = {
        UINT32_C(0x000b0000), UINT32_C(0x3a000001), UINT32_C(0x09000000)
    };
    const uint32_t *script = unsupported_command ? rejected_script : valid_script;
    size_t script_words = unsupported_command
        ? sizeof(rejected_script) / sizeof(rejected_script[0])
        : sizeof(valid_script) / sizeof(valid_script[0]);
    write_u32_le(file, (uint32_t)script_words);
    for (size_t i = 0; i < script_words; ++i) {
        write_u32_le(file, script[i]);
        if (script[i] == UINT32_C(0x5641554c)) {
            size_t callback_length = strlen(callback_name);
            assert(callback_length > 0 && callback_length <= 240);
            assert(fputc((int)callback_length, file) != EOF);
            assert(fwrite(callback_name, 1, callback_length, file) == callback_length);
        }
    }
    assert(fclose(file) == 0);
}

static void write_dynos_texture(const char *path, const char *name) {
    uint8_t pixels[2 * 2 * 4] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 0,
    };
    int png_size = 0;
    unsigned char *png = stbi_write_png_to_mem(pixels, 0, 2, 2, 4, &png_size);
    assert(png && png_size > 8 && (uint64_t)png_size <= UINT32_MAX);
    size_t name_length = strlen(name);
    assert(name_length > 0 && name_length <= UINT8_MAX);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputc(0x02, file) != EOF); /* DATA_TYPE_TEXTURE */
    assert(fputc((int)name_length, file) != EOF);
    assert(fwrite(name, 1, name_length, file) == name_length);
    write_u32_le(file, (uint32_t)png_size);
    assert(fwrite(png, 1, (size_t)png_size, file) == (size_t)png_size);
    assert(fclose(file) == 0);
    free(png);
}

static void create_dynos_behavior_fixture(const char *fixture_root) {
    char isolated[SYS_MAX_PATH], mods[SYS_MAX_PATH], mod[SYS_MAX_PATH], data[SYS_MAX_PATH];
    char textures[SYS_MAX_PATH], path[SYS_MAX_PATH];
    snprintf(isolated, sizeof(isolated), "%s/dynos-isolated", fixture_root);
    assert(fs_sys_mkdir(isolated));
    snprintf(mods, sizeof(mods), "%s/mods", isolated);
    assert(fs_sys_mkdir(mods));
    snprintf(mod, sizeof(mod), "%s/00_dynos_behavior", mods);
    assert(fs_sys_mkdir(mod));
    snprintf(data, sizeof(data), "%s/data", mod);
    assert(fs_sys_mkdir(data));
    snprintf(textures, sizeof(textures), "%s/textures", mod);
    assert(fs_sys_mkdir(textures));

    snprintf(path, sizeof(path), "%s/main.lua", mod);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs("assert(type(bhvFixture) == 'number')\n"
                 "assert(bhvRejected == nil)\n"
                 "assert(get_behavior_name_from_id(bhvFixture) == 'bhvFixture')\n"
                 "local ptr = get_behavior_from_id(bhvFixture)\n"
                 "assert(get_id_from_behavior(ptr) == bhvFixture)\n"
                 "assert(get_object_list_from_behavior(ptr) == OBJ_LIST_SPAWNER)\n"
                 "local texture = get_texture_info('fixture_tex')\n"
                 "assert(texture and texture.width == 2 and texture.height == 2)\n"
                 "assert(texture.format == 0 and texture.size == " TEST_STRINGIFY(TEST_DYNOS_TEX_SIZE) ")\n"
                 "assert(texture.name == 'fixture_tex' and type(texture.texture) == 'userdata')\n"
                 "hook_event(HOOK_ON_HUD_RENDER, function()\n"
                 "  djui_hud_set_rotation(0x1000, 0.5, 0.5)\n"
                 "  djui_hud_render_texture(texture, 12, 18, 1.5, 2.0)\n"
                 "  djui_hud_set_rotation(0, 0, 0)\n"
                 "end)\n"
                 "function fixture_bhv_tick() gMarioState.hurtCounter = gMarioState.hurtCounter + 1 end\n"
                 "gMarioState.actionArg = bhvFixture\n"
                 "local spawned = spawn_object(nil, MODEL_NONE, bhvFixture)\n"
                 "assert(spawned and get_id_from_behavior(spawned.behavior) == bhvFixture)\n", file) >= 0);
    assert(fclose(file) == 0);
    snprintf(path, sizeof(path), "%s/bhvFixture.bhv", data);
    write_dynos_behavior(path, "bhvFixture", "fixture_bhv_tick", false);
    snprintf(path, sizeof(path), "%s/bhvRejected.bhv", data);
    write_dynos_behavior(path, "bhvRejected", "unused", true);
    snprintf(path, sizeof(path), "%s/fixture_tex.tex", textures);
    write_dynos_texture(path, "fixture_tex");
}

int main(int argc, char **argv) {
#ifdef EXTERNAL_DATA
    ModRuntimeNativeTexture native_texture;
    assert(mod_runtime_native_texture_lookup("coin_seg3_texture_03005780", &native_texture));
    assert(strcmp(native_texture.asset_path, "actors/coin/coin_front.ia16") == 0 &&
           native_texture.width == 32 && native_texture.height == 32 &&
           native_texture.format == G_IM_FMT_IA && native_texture.size == G_IM_SIZ_16b);
    assert(mod_runtime_native_texture_lookup("texture_hud_char_star", &native_texture));
    assert(strcmp(native_texture.asset_path, "textures/segment2/segment2.05C00.rgba16") == 0 &&
           native_texture.width == 16 && native_texture.height == 16);
    assert(mod_runtime_native_texture_lookup("gd_texture_red_star_0", &native_texture));
    assert(strcmp(native_texture.asset_path, "textures/intro_raw/red_star_0.rgba16") == 0 &&
           native_texture.width == 32 && native_texture.height == 32 &&
           native_texture.format == G_IM_FMT_RGBA && native_texture.size == G_IM_SIZ_16b);
    assert(!mod_runtime_native_texture_lookup("native_texture_missing", &native_texture));
#endif
    if (argc == 3 && strcmp(argv[1], "--external-root") == 0) {
        snprintf(fs_writepath, sizeof(fs_writepath), "%s", argv[2]);
        assert(fs_sys_mkdir(fs_writepath));
        reset_game_state();
        assert(mod_runtime_init());
        printf("external mod inventory: %d\n", mod_runtime_mod_count());
        mod_runtime_update();
        for (int i = 0; i < mod_runtime_mod_count(); ++i) {
            printf("external mod %s: %s\n", mod_runtime_mod_name(i),
                   mod_runtime_mod_enabled(i) ? "enabled" : "disabled");
        }
        mod_runtime_shutdown();
        return 0;
    }
    if (argc == 4 && (!strcmp(argv[1], "--external-zip") || !strcmp(argv[1], "--possession-zip"))) {
        bool possession = !strcmp(argv[1], "--possession-zip");
        snprintf(fs_writepath, sizeof(fs_writepath), "%s", argv[2]);
        reset_game_state();
        char installed[SYS_MAX_PATH], error[512] = {0};
        if (!mod_runtime_init() ||
            !mod_runtime_import_zip(argv[3], installed, sizeof(installed), error, sizeof(error))) {
            fprintf(stderr, "ZIP import failed: %s\n", error);
            return 1;
        }
        mod_runtime_update();
        printf("Imported %s, startup %s\n", mod_runtime_mod_name(0), mod_runtime_mod_enabled(0) ? "passed" : "failed");
        if (!mod_runtime_mod_enabled(0)) {
            mod_runtime_shutdown();
            return 1;
        }
        if (getenv("MOD_TEST_DISPATCH_MP3_STREAM")) {
            int16_t pcm[4096] = {0};
            mod_audio_mix(pcm, 2048, 0, 1);
            bool heard = false;
            for (size_t i = 0; i < 4096; ++i) heard |= pcm[i] != 0;
            assert(heard); /* Stream voices use the native music-volume channel. */
            assert(mod_runtime_set_enabled(0, false));
            memset(pcm, 0, sizeof(pcm));
            mod_audio_mix(pcm, 2048, 1, 1);
            for (size_t i = 0; i < 4096; ++i) assert(pcm[i] == 0);
            assert(mod_runtime_set_enabled(0, true));
            memset(pcm, 0, sizeof(pcm));
            mod_audio_mix(pcm, 2048, 0, 1);
            heard = false;
            for (size_t i = 0; i < 4096; ++i) heard |= pcm[i] != 0;
            assert(heard);
            assert(mod_runtime_set_enabled(0, false));
            printf("Lua MP3 stream: playback, music gain, stop, disable cleanup, and re-enable passed\n");
        }
        if (!possession && getenv("MOD_TEST_DISPATCH_SET_ACTION")) {
            gMarioState->action = ACT_JUMP;
            mod_runtime_after_set_action(gMarioState);
            assert(sMarioFlagSoundCalls == 1);
            assert(sLastMarioFlagSoundBits == 38u); /* CoopDX CHAR_SOUND_TWIRL_BOUNCE */
            assert(sLastSoundBits == SOUND_MARIO_TWIRL_BOUNCE);
            assert(gMarioState->flags & MARIO_ACTION_SOUND_PLAYED);
            assert(gMarioState->flags & MARIO_MARIO_SOUND_PLAYED);
            printf("original mod HOOK_ON_SET_MARIO_ACTION: native action sound and mapped CHAR_SOUND playback passed\n");
        }
        if (getenv("MOD_TEST_DISPATCH_DVD_TEXTURE")) {
            gMarioState->controller->buttonPressed = 0;
            gMarioState->controller->buttonDown = 0;
            gMarioState->controller->stickMag = 0;
            /* Exercise the real idle timeout and HUD callback path used by the
             * unmodified DVD Screensaver package, rather than calling Lua
             * functions directly from the test. */
            for (int frame = 0; frame < 9002; ++frame)
                mod_runtime_before_mario_update(gMarioState);
            mod_runtime_hud_render();
            assert(gModHudTestRectCount == 1);
            assert(gModHudTestTextureCount == 1 && gModHudTestTextureWidth == 1024 &&
                   gModHudTestTextureHeight == 512);
            if (getenv("MOD_TEST_ASSERT_UPDATE_COUNT"))
                assert(gMarioState->actionArg == 9002);
            printf("production importer + idle/action and HUD hooks: 1024x512 DynOS texture submitted\n");
        }
        if (getenv("MOD_TEST_EXPECT_OMM_MODELS")) {
            for (int slot = 0xE6; slot <= 0xEB; ++slot) {
                assert(gLoadedGraphNodes[slot] != NULL);
                assert(mod_runtime_extended_model_id(slot) >= 0x10000);
            }
            char model_error[400] = {0};
            int peach_id = 0;
            assert(mod_runtime_host_load_dynos_model(0, "omm_peachy_geo", &peach_id,
                                                     model_error, sizeof(model_error)));
            assert(peach_id >= 0x10000);
            int peach_native_model = -1;
            for (int slot = 0; slot < 0x100; ++slot)
                if (gLoadedGraphNodes[slot] && mod_runtime_extended_model_id(slot) == peach_id)
                    peach_native_model = slot;
            assert(peach_native_model >= 0);
            if (getenv("MOD_TEST_OMM_EXTRA_MODELS")) {
                static const char *extra_models[] = {
                    "omm_tiara_geo", "omm_tiara_wing_geo", "omm_tiara_metal_geo",
                    "omm_tiara_winged_metal_geo", "omm_geo_star_opaque",
                    "omm_geo_star_transparent",
                };
                for (size_t i = 0; i < sizeof(extra_models) / sizeof(extra_models[0]); ++i) {
                    int extra_id = 0;
                    char extra_error[400] = {0};
                    bool loaded = mod_runtime_host_load_dynos_model(0, extra_models[i],
                        &extra_id, extra_error, sizeof(extra_error));
                    printf("OMM extra model %s: %s%s%s\n", extra_models[i],
                        loaded ? "registered" : "unsupported", loaded ? "" : " — ",
                        loaded ? "" : extra_error);
                    if (loaded) {
                        assert(extra_id >= 0x10000);
                        int extra_slot = -1;
                        for (int slot = 0; slot < 0x100; ++slot)
                            if (gLoadedGraphNodes[slot] &&
                                mod_runtime_extended_model_id(slot) == extra_id)
                                extra_slot = slot;
                        assert(extra_slot >= 0);
                        gMarioState->marioObj = &sMarioObject;
                        assert(mod_runtime_request_player_dynos_model(extra_models[i]));
                        mod_runtime_host_process_requests();
                        assert(sMarioObject.header.gfx.sharedChild == gLoadedGraphNodes[extra_slot]);

                        struct GraphNodeRoot extra_root = {0};
                        struct GraphNodePerspective extra_frustum = {0};
                        struct GraphNodeCamera extra_camera = {0};
                        struct GraphNodeMasterList extra_master = {0};
                        struct AllocOnlyPool extra_pool = {0};
                        uint8_t extra_pool_storage[65536];
                        uint8_t extra_gfx_scratch[4096];
                        extra_root.areaIndex = 1;
                        extra_frustum.fov = 45.0f;
                        extra_frustum.near = 1;
                        extra_frustum.far = 20000;
                        extra_pool.totalSpace = sizeof(extra_pool_storage);
                        extra_pool.startPtr = extra_pool_storage;
                        extra_pool.freePtr = extra_pool_storage;
                        sMarioObject.header.gfx.node.flags = GRAPH_RENDER_ACTIVE;
                        sMarioObject.header.gfx.unk18 = 1;
                        sMarioObject.header.gfx.pos[2] = -1000.0f;
                        ++gAreaUpdateCounter;
                        assert(rendering_graph_host_process_object(&sMarioObject, &extra_root,
                            &extra_frustum, &extra_camera, &extra_master, &extra_pool,
                            extra_gfx_scratch, sizeof(extra_gfx_scratch)));
                        assert(rendering_graph_host_display_list_submissions() > 0);
                        printf("OMM extra model %s: production GeoLayout traversal submitted display-list draws\n",
                               extra_models[i]);
                    }
                }
            }
            gMarioState->marioObj = &sMarioObject;
            sMarioObject.header.gfx.sharedChild = gLoadedGraphNodes[0xE6];
            assert(mod_runtime_request_player_dynos_model("omm_peachy_geo"));
            mod_runtime_host_process_requests();
            assert(sMarioObject.header.gfx.sharedChild == gLoadedGraphNodes[peach_native_model]);
            assert(mod_runtime_set_enabled(0, false));
            assert(sMarioObject.header.gfx.sharedChild == NULL);
            for (int slot = 0xE6; slot <= 0xFF; ++slot) assert(gLoadedGraphNodes[slot] == NULL);
            assert(mod_runtime_set_enabled(0, true));
            for (int slot = 0xE6; slot <= 0xEB; ++slot)
                assert(gLoadedGraphNodes[slot] != NULL);
            memset(model_error, 0, sizeof(model_error));
            assert(mod_runtime_host_load_dynos_model(0, "omm_peachy_geo", &peach_id,
                                                     model_error, sizeof(model_error)));
            if (getenv("MOD_TEST_OMM_ANIMATION")) {
                int peach_slot = -1;
                for (int slot = 0; slot < 0x100; ++slot)
                    if (gLoadedGraphNodes[slot] && mod_runtime_extended_model_id(slot) == peach_id)
                        peach_slot = slot;
                assert(peach_slot >= 0);
                gMarioState->marioObj = &sMarioObject;
                assert(mod_runtime_request_player_dynos_model("omm_peachy_geo"));
                mod_runtime_host_process_requests();
                sMarioObject.header.gfx.node.flags = GRAPH_RENDER_ACTIVE | GRAPH_RENDER_HAS_ANIMATION;
                sMarioObject.header.gfx.unk18 = 1;
                sMarioObject.header.gfx.pos[2] = -1000.0f;
                vec3f_set(sMarioObject.header.gfx.scale, 1.0f, 1.0f, 1.0f);
                assert(mod_runtime_host_set_animation(&sMarioObject, "anim_mario_omm_cappy_throw"));
                assert(sMarioObject.header.gfx.unk38.curAnim->values != NULL);
                assert(sMarioObject.header.gfx.unk38.curAnim->index != NULL);

                struct GraphNodeRoot anim_root = {0};
                struct GraphNodePerspective anim_frustum = {0};
                struct GraphNodeCamera anim_camera = {0};
                struct GraphNodeMasterList anim_master = {0};
                struct AllocOnlyPool anim_pool = {0};
                uint8_t anim_pool_storage[65536];
                uint8_t anim_gfx_scratch[4096];
                anim_root.areaIndex = 1;
                anim_frustum.fov = 45.0f;
                anim_frustum.near = 1;
                anim_frustum.far = 20000;
                anim_pool.totalSpace = sizeof(anim_pool_storage);
                anim_pool.startPtr = anim_pool_storage;
                anim_pool.freePtr = anim_pool_storage;
                unsigned long long first_transform = 0;
                bool animation_changed_transform = false;
                for (int frame = 0; frame < 63; ++frame) {
                    ++gAreaUpdateCounter;
                    anim_pool.freePtr = anim_pool_storage;
                    anim_pool.usedSpace = 0;
                    assert(rendering_graph_host_process_object(&sMarioObject, &anim_root,
                        &anim_frustum, &anim_camera, &anim_master, &anim_pool,
                        anim_gfx_scratch, sizeof(anim_gfx_scratch)));
                    assert(rendering_graph_host_display_list_submissions() > 0);
                    assert(rendering_graph_host_animated_part_visits() > 0);
                    unsigned long long transform = rendering_graph_host_animation_transform_hash();
                    if (frame == 0) first_transform = transform;
                    else if (transform != first_transform) animation_changed_transform = true;
                }
                assert(animation_changed_transform);
                printf("OMM animation: original Cappy throw data advanced through Peach GeoLayout animated parts and changed native transforms\n");
            }
            if (getenv("MOD_TEST_OMM_ACTIONS")) {
                int64_t cappy_throw_action = 0;
                bool got_cappy_throw_action = mod_runtime_host_get_lua_integer(
                    "ACT_OMM_CAPPY_THROW_GROUND", &cappy_throw_action);
                fprintf(stderr, "OMM action probe: action constant %s (value=%lld)\n",
                        got_cappy_throw_action ? "available" : "missing",
                        (long long)cappy_throw_action);
                assert(got_cappy_throw_action);
                gMarioState->health = 0x880;
                gMarioState->marioObj = &sMarioObject;
                gMarioState->action = (uint32_t)cappy_throw_action;
                gMarioState->actionArg = 0;
                gMarioState->actionState = 0;
                gMarioState->actionTimer = 0;
                sController.buttonPressed = UINT16_MAX;
                sController.buttonDown = 0;
                int in_loop = 0;
                bool handled = mod_runtime_mario_action(gMarioState, &in_loop);
                printf("OMM Cappy ground-throw action callback dispatch: %s (inLoop=%d, action=0x%08x)\n",
                       handled ? "handled" : "not handled", in_loop, gMarioState->action);
                assert(handled && sController.buttonPressed != UINT16_MAX);
            }
            assert(peach_id >= 0x10000);
            printf("original OMM: six startup graphs and Peach model registered across disable/re-enable\n");
        }
        if (getenv("MOD_TEST_MIRROR_RENDER")) {
            struct GraphNodeObject mirror = {0};
            sMarioObject.header.gfx.pos[0] = 11.0f;
            sMarioObject.header.gfx.angle[1] = 23;
            sMarioObject.header.gfx.scale[0] = 2.0f;
            mirror.pos[0] = 100.0f;
            mirror.pos[1] = 200.0f;
            mirror.pos[2] = 300.0f;
            mirror.node.flags = GRAPH_RENDER_ACTIVE;
            mod_runtime_mirror_mario_render(&mirror, 0);
            float shadow[3] = {0};
            bool has_shadow_override = mod_runtime_graph_node_shadow_position(&mirror, shadow);
            printf("mirror-render callback completed; mirror.x=%.2f shadowOverride=%s shadow.x=%.2f\n",
                   mirror.pos[0], has_shadow_override ? "yes" : "no", shadow[0]);
        }
        if (possession) {
            append_test_object(OBJ_LIST_PLAYER, &sMarioObject, NULL, ACTIVE_FLAG_ACTIVE, 0);
            sMarioObject.header.gfx.node.flags = GRAPH_RENDER_ACTIVE;
            sWoodenPost.oPosX = 1000;
            sOneUpObject.oPosX = 1000;
            sSecretObject.oPosX = 1000;
            sExclamationBoxObject.oPosX = 1000;
            sGoombas[0].oPosX = 80;
            sGoombas[2].oPosX = 140;
            gMarioState->health = 100;
            sController.buttonPressed = D_JPAD;
            mod_runtime_after_mario_update(gMarioState);
            assert(!(sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE));
            sController.buttonPressed = 0;
            gMarioState->forwardVel = 12;
            mod_runtime_after_mario_update(gMarioState);
            assert(gMarioState->pos[0] == 80 && gMarioState->forwardVel == 24);
            mod_runtime_hud_render();
            assert(gModHudTestTextCount == 1);
            sController.buttonPressed = D_JPAD;
            mod_runtime_after_mario_update(gMarioState);
            assert(sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE);
            sController.buttonPressed = D_JPAD;
            mod_runtime_after_mario_update(gMarioState);
            assert(!(sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE));
            gMarioState->health = 99;
            sController.buttonPressed = 0;
            mod_runtime_after_mario_update(gMarioState);
            assert(sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE); /* Damage releases possession. */
            gMarioState->health = 100;
            mod_runtime_after_mario_update(gMarioState); /* Refresh the script's saved health before re-possessing. */
            sController.buttonPressed = D_JPAD;
            mod_runtime_after_mario_update(gMarioState);
            assert(!(sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE));
            assert(mod_runtime_set_enabled(0, false));
            assert(sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE);
            assert(mod_runtime_set_enabled(0, true));
            sController.buttonPressed = D_JPAD;
            mod_runtime_after_mario_update(gMarioState);
            assert(!(sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE));
            mod_runtime_object_unload(&sGoombas[0]);
            sGoombas[0].oPosX = 400;
            sController.buttonPressed = 0;
            mod_runtime_after_mario_update(gMarioState);
            assert(sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE);
            assert(gMarioState->pos[0] == 80);
            puts("Original possession callback: input, actor selection, follow, speed, manual/damage release, reload, and deletion cleanup passed");
        } else {
            /* Real game loops dispatch a later update after the initial mod
             * load, allowing cross-file initialization callbacks to publish
             * their globals before HUD hooks consume them. */
            if (getenv("MOD_TEST_OMM_HUD")) {
                gCurrLevelNum = LEVEL_BOB;
                gCurrCourseNum = COURSE_BOB;
                gCurrAreaIndex = 1;
            }
            gHudDisplay.flags = HUD_DISPLAY_DEFAULT;
            gHudDisplay.lives = 4;
            gHudDisplay.coins = 3;
            gHudDisplay.stars = 1;
            gHudDisplay.wedges = 8;
            gMarioState->numCoins = 3;
            gMarioState->numStars = 1;
            mod_runtime_update();
            mod_runtime_before_mario_update(gMarioState);
            mod_runtime_after_mario_update(gMarioState);
            mod_runtime_hud_render_behind();
            int hud_behind_texture_count = gModHudTestTextureCount;
            mod_runtime_hud_render();
            int hud_front_texture_count = gModHudTestTextureCount;
            printf("host dispatch: texture HUD calls behind=%d front=%d",
                   hud_behind_texture_count, hud_front_texture_count);
            if (hud_front_texture_count > 0)
                printf(" (%dx%d format=%d size=%d)", gModHudTestTextureWidth,
                       gModHudTestTextureHeight, gModHudTestTextureFormat, gModHudTestTextureSize);
            putchar('\n');
            if (getenv("MOD_TEST_REQUIRE_TEXTURE_HUD") &&
                hud_behind_texture_count + hud_front_texture_count == 0) {
                fprintf(stderr, "mod did not dispatch a texture HUD draw during the host frame\n");
                mod_runtime_shutdown();
                return 1;
            }
        }
        mod_runtime_shutdown();
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--real-zip") == 0) {
        snprintf(fs_writepath, sizeof(fs_writepath), "%s", argv[2]);
        assert(fs_sys_mkdir(fs_writepath));
        reset_game_state();
        char installed[SYS_MAX_PATH], error[512];
        assert(mod_runtime_import_zip(argv[3], installed, sizeof(installed), error, sizeof(error)));
        printf("real ZIP import: PASS (%s)\n", installed);
        assert(mod_runtime_init());
        assert(mod_runtime_mod_count() == 1);
        printf("real ZIP discovered mod: %s\n", mod_runtime_mod_name(0));
        mod_runtime_update();
        assert(mod_runtime_menu_item_count(0) == 11);
        ModRuntimeMenuItem menu_item;
        assert(mod_runtime_menu_get_item(0, 0, &menu_item));
        assert(menu_item.type == MOD_RUNTIME_MENU_SLIDER);
        assert(menu_item.minimum == 1 && menu_item.maximum == 8);
        assert(mod_runtime_request_menu_set_value(0, 0, 2));
        mod_runtime_update();
        assert(mod_runtime_menu_get_item(0, 0, &menu_item));
        assert(menu_item.value == 2);
        gMarioState->numStars = 1;
        for (int frame = 0; frame < 15; ++frame) mod_runtime_after_mario_update(gMarioState);
        assert(real_mod_storage_contains(argv[2], "73315f676574315374617273=31"));
        /* Unsupported sound/HUD calls should be diagnosed and isolated to their hooks. */
        mod_runtime_update();
        mod_runtime_hud_render();
        assert(mod_runtime_mod_enabled(0));
        printf("real ZIP startup: %s\n", mod_runtime_mod_enabled(0) ? "loaded" : "failed/disabled");
        assert(mod_runtime_set_enabled(0, false));
        assert(mod_runtime_menu_item_count(0) == 0);
        assert(mod_runtime_set_enabled(0, true));
        assert(mod_runtime_menu_item_count(0) == 11);
        mod_runtime_shutdown();
        assert(mod_runtime_init());
        mod_runtime_update();
        gMarioState->numStars = 1;
        mod_runtime_after_mario_update(gMarioState);
        assert(real_mod_storage_contains(argv[2], "73315f676574315374617273=31"));
        mod_runtime_shutdown();
        return 0;
    }
    assert(argc == 2);
    snprintf(fs_writepath, sizeof(fs_writepath), "%s", argv[1]);
    create_bytecode_fixture(argv[1]);
    create_architecture_variant_fixture(argv[1]);
    reset_game_state();
    assert(mod_runtime_init());
    assert(mod_runtime_mod_count() == 13);
    assert(mod_runtime_dynos_pack_count() == 1);
    assert(strcmp(mod_runtime_dynos_pack_name(0), "FixturePack") == 0);
    assert(strstr(mod_runtime_dynos_pack_summary(0), "1 model bins · 2 texture assets") != NULL);
    assert(strstr(mod_runtime_dynos_pack_summary(0), "support varies") != NULL);
    assert(strcmp(mod_runtime_mod_name(0), "Core Fixture Display Name") == 0);
    assert(strcmp(mod_runtime_mod_package_id(0), "00_core") == 0);
    assert(strcmp(mod_runtime_mod_category(0), "test-suite") == 0);
    assert(strcmp(mod_runtime_mod_description(0), "Validates CoopDX package metadata behavior.") == 0);
    assert(strcmp(mod_runtime_mod_incompatibilities(0), "metadata-shared-token") == 0);
    assert(mod_runtime_mod_enabled(0));
    assert(mod_runtime_mod_enabled(8));
    assert(mod_runtime_mod_enabled(9));
    mod_runtime_update();
    assert(!mod_runtime_mod_enabled(12));
    assert(strcmp(mod_runtime_mod_name(12), "Metadata Conflict Fixture") == 0);
    assert(strcmp(mod_runtime_mod_enabled_conflict(12), "Core Fixture Display Name") == 0);
    assert(!mod_runtime_set_enabled(12, true));
    assert(!mod_runtime_mod_enabled(12));
#ifdef MOD_RUNTIME_HOST_TEST
    if (getenv("MOD_TEST_DYNOS_GRAPH_RENDER")) {
        char model_error[400] = {0};
        const char *model_name = getenv("MOD_TEST_DYNOS_ACTOR_NAME");
        if (!model_name || !*model_name) model_name = "fixture_actor";
        int extended_id = 0;
        if (!mod_runtime_host_load_dynos_model(0, model_name, &extended_id,
                                               model_error, sizeof(model_error))) {
            fprintf(stderr, "DynOS actor activation failed: %s\n", model_error);
            assert(false);
        }
        assert(extended_id >= 0x10000);
        int native_model = -1;
        for (int i = 0; i < 0x100; ++i) {
            if (gLoadedGraphNodes[i] && mod_runtime_extended_model_id(i) == extended_id) {
                native_model = i;
                break;
            }
        }
        assert(native_model >= 0);

        struct Object *actor = &sMarioObject;
        gMarioState->marioObj = actor;
        assert(mod_runtime_request_player_dynos_model(model_name));
        mod_runtime_host_process_requests();
        assert(actor->header.gfx.sharedChild == gLoadedGraphNodes[native_model]);
        assert(mod_runtime_player_dynos_model_id() == (uint32_t)extended_id);
        actor->header.gfx.sharedChild = NULL; /* Simulate a native model reset before object traversal. */
        mod_runtime_object_render(actor);
        assert(actor->header.gfx.sharedChild == gLoadedGraphNodes[native_model]);
        struct GraphNode native_mario_graph = {0};
        struct GraphNode *saved_mario_graph = gLoadedGraphNodes[MODEL_MARIO];
        if (!saved_mario_graph) gLoadedGraphNodes[MODEL_MARIO] = &native_mario_graph;
        assert(mod_runtime_request_player_native_model());
        mod_runtime_host_process_requests();
        assert(actor->header.gfx.sharedChild == gLoadedGraphNodes[MODEL_MARIO]);
        assert(mod_runtime_player_dynos_model_id() == 0);
        gLoadedGraphNodes[MODEL_MARIO] = saved_mario_graph;
        struct GraphNodeObject saved_actor_gfx = actor->header.gfx;
        actor->header.gfx.node.flags = GRAPH_RENDER_ACTIVE;
        actor->header.gfx.unk18 = 1;
        actor->header.gfx.pos[0] = 12.0f;
        actor->header.gfx.pos[1] = 34.0f;
        actor->header.gfx.pos[2] = -1000.0f;
        vec3f_set(actor->header.gfx.scale, 1.0f, 1.0f, 1.0f);
        memset(&actor->header.gfx.unk38, 0, sizeof(actor->header.gfx.unk38));
        assert(mod_runtime_host_set_object_model(actor, extended_id, model_error,
                                                 sizeof(model_error)));
        assert(actor->header.gfx.sharedChild == gLoadedGraphNodes[native_model]);

        struct GraphNodeRoot root = {0};
        struct GraphNodePerspective frustum = {0};
        struct GraphNodeCamera camera = {0};
        struct GraphNodeMasterList master = {0};
        root.areaIndex = 1;
        frustum.fov = 45.0f;
        frustum.near = 1;
        frustum.far = 20000;
        struct AllocOnlyPool display_pool = {0};
        uint8_t display_storage[8192];
        display_pool.totalSpace = sizeof(display_storage);
        display_pool.startPtr = display_storage;
        display_pool.freePtr = display_storage;
        uint8_t gfx_scratch[4096];
        assert(rendering_graph_host_process_object(actor, &root, &frustum, &camera,
                                                   &master, &display_pool, gfx_scratch,
                                                   sizeof(gfx_scratch)));
        unsigned int submissions = rendering_graph_host_display_list_submissions();
        assert(submissions > 0);
        unsigned int listed_submissions = 0;
        for (int layer = 0; layer < GFX_NUM_MASTER_LISTS; ++layer) {
            for (struct DisplayListNode *entry = master.listHeads[layer]; entry; entry = entry->next) {
                assert(entry->displayList != NULL);
                listed_submissions++;
            }
        }
        assert(listed_submissions == submissions);
        assert((model_name && strcmp(model_name, "fixture_actor") != 0) || submissions == 1);
        unsigned int callbacks = rendering_graph_host_geo_callbacks();
        unsigned int transform_nodes = rendering_graph_host_transform_nodes();
        if (strcmp(model_name, "omm_peachy_geo") == 0) {
            assert(callbacks > 0);
            assert(transform_nodes > 1);
        }
        assert(actor->header.gfx.cameraToObject[0] == 12.0f &&
               actor->header.gfx.cameraToObject[1] == 34.0f &&
               actor->header.gfx.cameraToObject[2] == -1000.0f);
        actor->header.gfx.sharedChild = NULL;
        assert(!rendering_graph_host_process_object(actor, &root, &frustum, &camera,
                                                    &master, &display_pool, gfx_scratch,
                                                    sizeof(gfx_scratch)));
        assert(rendering_graph_host_display_list_submissions() == 0);
        actor->header.gfx = saved_actor_gfx;
        printf("DynOS host integration: %s (extended %d, native slot 0x%x), geo_process_object -> %u display-list submission(s), %u callback(s), %u transform node(s)\n",
               model_name, extended_id, native_model, submissions, callbacks, transform_nodes);
    }
#endif
    assert(sMarioBodyState.modelState == 0x25 && sMarioBodyState.capState == 2 &&
           sMarioBodyState.punchState == 17); /* Lua writes native MarioBodyState storage. */
    assert(gPlayerCameraState[0].cameraEvent == 31 && gPlayerCameraState[0].pos[0] == 12.0f &&
           gPlayerCameraState[0].faceAngle[1] == 0x4000); /* Live camera state reaches the native player camera. */
    assert(sLastRayStart[0] == 10.0f && sLastRayStart[1] == 20.0f && sLastRayStart[2] == 30.0f);
    assert(sLastRayDirection[0] == 4.0f && sLastRayDirection[1] == 8.0f && sLastRayDirection[2] == 12.0f);
    assert(sLastRayPrecision == 2.5f);
    float scripted_shadow[3] = {0};
    assert(mod_runtime_graph_node_shadow_position(&sGoombas[0].header.gfx, scripted_shadow));
    assert(scripted_shadow[0] == 41.0f && scripted_shadow[1] == 42.0f && scripted_shadow[2] == 43.0f);
    assert(gSpecialTripleJump == 1); /* Lua's MarioState field writes the native Yoshi triple-jump control. */
    s32 previous_action_arg = gMarioState->actionArg;
    bool use_act_select = true;
    assert(mod_runtime_use_act_select(LEVEL_BOB, &use_act_select));
    assert(!use_act_select && gMarioState->actionArg == LEVEL_BOB + 9000);
    gMarioState->actionArg = previous_action_arg;
    assert(sGoombaRespawnInfo == 0x01001201);
    assert(sYellowLootCalls == 1 && sLastYellowLootObject == &sGoombas[0] &&
           sLastYellowLootCount == 3 && sLastYellowLootSpeed == 20.0f &&
           sGoombas[0].oNumLootCoins == 2);
    gCurrentObject = &sGoombas[0];
    assert(!mod_runtime_native_behavior_tick());
    assert(sGoombas[0].oSubAction == 1);
    assert(!mod_runtime_native_behavior_tick());
    assert(sGoombas[0].oSubAction == 2);
    gCurrentObject = &sWoodenPost;
    assert(mod_runtime_native_behavior_tick());
    assert(sWoodenPost.oAction == 101 && sWoodenPost.oSubAction == 1);
    assert(mod_runtime_native_behavior_tick());
    assert(sWoodenPost.oAction == 112 && sWoodenPost.oSubAction == 2);
    gCurrentObject = NULL;
    assert(sWarpTriggerCalls == 1 && sLastWarpMario == gMarioState);
    assert(sLastWarpOperation == WARP_OP_WARP_DOOR);
    u8 previous_camera_mode = sCamera.mode;
    s16 previous_camera_mode_frames = sLastCameraModeFrames;
    sCamera.mode = 77;
    int camera_hook_stars = gMarioState->numStars;
    set_camera_mode(&sCamera, 321, 8);
    assert(sCamera.mode == 77 && gMarioState->numStars == camera_hook_stars + 1);
    set_camera_mode(&sCamera, 222, 4);
    assert(sCamera.mode == 222 && gMarioState->numStars == camera_hook_stars + 2);
    sCamera.mode = previous_camera_mode;
    sLastCameraModeFrames = previous_camera_mode_frames;
    gMarioState->numStars = camera_hook_stars;
    assert(!mod_runtime_allow_camera_angle(CAM_ANGLE_MARIO));
    assert(mod_runtime_allow_camera_angle(CAM_ANGLE_LAKITU));
    assert(gMarioState->numStars == camera_hook_stars + 2);
    gMarioState->numStars = camera_hook_stars;
    assert(mod_runtime_mario_action_interaction_type(gMarioState) == INT_PUNCH);
    assert(sMarioObject.header.gfx.unk38.curAnim != NULL);
    assert(sMarioObject.header.gfx.unk38.curAnim->flags == ANIM_FLAG_FORWARD);
    assert(sMarioObject.header.gfx.unk38.curAnim->unk08 == 1);
    assert(sMarioObject.header.gfx.unk38.curAnim->values[1] == 10);
    assert(gMarioState->hurtCounter == 102);
    assert(gMarioState->health == 1003);
    uint32_t model_action_arg = gMarioState->actionArg;
    mod_runtime_on_warp(1, 2, 3, 4, 5);
    assert(gMarioState->actionArg == 102345);
    gMarioState->actionArg = model_action_arg;
    gMarioState->health = 0x880;
    assert(!mod_runtime_on_death(gMarioState));
    assert(gMarioState->health == 801);
    assert(!mod_runtime_allow_hazard_surface(gMarioState, 0));
    assert(gMarioState->actionArg == 0);
    assert(mod_runtime_allow_hazard_surface(gMarioState, 2));
    assert(gMarioState->actionArg == 2);
    assert(!mod_runtime_on_pause_exit(true));
    assert(gMarioState->actionArg == 1);
    gMarioState->actionArg = model_action_arg;
    assert(sLastSetAnimToFrame == 12);
    assert(sLastAnimWithAccelTarget == 43 && sLastAnimWithAccelValue == 32768);
    assert(sCamera.mode == CAMERA_MODE_BEHIND_MARIO && sLastCameraModeFrames == 3);
    assert(sCameraShakeCalls == 1 && sLastCameraShake == SHAKE_HIT_FROM_BELOW);
    assert(sCameraPointShakeCalls == 1 && sLastCameraPointShake == SHAKE_POS_MEDIUM &&
           sLastCameraPointShakePosition[0] == 120.0f &&
           sLastCameraPointShakePosition[1] == 340.0f &&
           sLastCameraPointShakePosition[2] == -560.0f);
    assert(sTransitionCalls == 1 && sLastTransitionType == WARP_TRANSITION_FADE_INTO_COLOR &&
           sLastTransitionTime == 14 && sLastTransitionRgb[0] == 255 &&
           sLastTransitionRgb[1] == 128 && sLastTransitionRgb[2] == 0);
    assert(sCapMusicCalls == 1 && sLastCapMusicSequence == 1234);
    assert(sShellMusicCalls == 1);
    assert(sStopShellMusicCalls == 1);
    assert(sWaterLevelQueries == 1 && sLastWaterLevelX == 10 && sLastWaterLevelZ == -4);
    assert(sMarioBonkReflectionCalls == 2 && sLastBonkMario == gMarioState && sLastBonkNegateSpeed == FALSE);
    assert(sMarioStopRidingCalls == 1);
    assert(sMarioStopRidingAndHoldingCalls == 1);
    assert(sHeavyLandingSoundCalls == 1 && sLastHeavyLandingMario == gMarioState &&
           sLastHeavyLandingSoundBits == SOUND_ACTION_TERRAIN_HEAVY_LANDING);
    assert(sMarioActionSoundCalls == 1 && sLastMarioActionSoundMario == gMarioState &&
           sLastMarioActionSoundBits == SOUND_ACTION_TERRAIN_JUMP && sLastMarioActionWaveParticle == 0);
    assert(sCourseClearCalls == 1);
    assert(sObjScaleCalls == 1 && sLastObjScaleObject != NULL && sLastObjScale == 1.5f);
    assert(sFlipSoundCalls == 1 && sLastFlipSoundMario == gMarioState &&
           sLastFlipSoundFrames[0] == 2 && sLastFlipSoundFrames[1] == 8 &&
           sLastFlipSoundFrames[2] == 20);
    assert(sMarioCharacterSoundCalls == 3 && sLastMarioCharacterSoundBits == SOUND_MARIO_YAHOO);
    mod_runtime_hud_render_behind();
    assert(gMarioState->actionArg == 3600 && gModHudTestRectCount == 1);
    gMarioState->actionArg = model_action_arg;
    mod_runtime_hud_render();
#ifdef EXTERNAL_DATA
    assert(gModHudTestTextureCount == 1 && gModHudTestTextureWidth == 32 &&
           gModHudTestTextureHeight == 32 && gModHudTestTextureFormat == G_IM_FMT_IA &&
           gModHudTestTextureSize == G_IM_SIZ_16b); /* Native symbol TextureInfo reaches the HUD draw adapter. */
#else
    assert(gModHudTestTextureCount == 0); /* Bundled path identities are unavailable in embedded-data builds. */
#endif
    assert(gMarioState->actionArg == MODEL_GOOMBA * 1000 + mod_runtime_extended_model_id(MODEL_GOOMBA));
    assert(mod_runtime_mod_enabled(8));
    assert(!mod_runtime_mod_enabled(10)); /* Non-stop requires the missing game-mode transition system. */
    assert(mod_runtime_stay_in_level_after_star());
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_FIX_VANISH_FLOORS));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_STAR_HEAL) == 255);
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_MUSHROOM_1UP_HEAL));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_PREVIEW_BLUE_COINS));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_RESPAWN_BLUE_COINS_SWITCH));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_VISIBLE_SECRETS));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_SHOW_STAR_NUMBER));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_EXTENDED_PAUSE_DISPLAY));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_PAUSE_EXIT_ANYWHERE));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_CELL_HEIGHT_LIMIT) == 20000);
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_COINS_REQUIRED_FOR_COIN_STAR) == 80);
    assert(mod_runtime_set_enabled(9, false));
    assert(!mod_runtime_stay_in_level_after_star()); /* Disabling releases the setting owner. */
    assert(!mod_runtime_level_value(MOD_LEVEL_VALUE_STAR_HEAL));
    assert(!mod_runtime_level_value(MOD_LEVEL_VALUE_PAUSE_EXIT_ANYWHERE));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_COINS_REQUIRED_FOR_COIN_STAR) == 100);
    assert(mod_runtime_set_enabled(9, true));
    assert(mod_runtime_stay_in_level_after_star()); /* Re-enable replays the original setting write. */
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_STAR_HEAL));
    assert(mod_runtime_level_value(MOD_LEVEL_VALUE_COINS_REQUIRED_FOR_COIN_STAR) == 80);
    assert(gModHudTestRectCount == 3 && gModHudTestTextCount == 3);
    assert(gModHudSawRotation); /* The hook queued draw commands before its state reset. */
    assert(gModHudTestResolution == 0);
    assert(gModHudTestColor[0] == 255 && gModHudTestColor[1] == 255 &&
           gModHudTestColor[2] == 255 && gModHudTestColor[3] == 255);
    assert(sLastSoundBits == SOUND_MENU_PINCH_MARIO_FACE); /* Upstream popup notification sound. */
    int16_t pcm[256] = {0};
    mod_audio_mix(pcm, 128, 1, 1);
    bool heard = false;
    for (size_t i = 0; i < 256; i += 2) {
        assert(pcm[i] == pcm[i + 1]);
        heard |= pcm[i] != 0;
    }
    assert(heard);
    assert(mod_runtime_set_enabled(8, false));
    memset(pcm, 0, sizeof(pcm));
    mod_audio_mix(pcm, 128, 1, 1);
    for (size_t i = 0; i < 256; ++i) assert(pcm[i] == 0);
    mod_runtime_hud_render();
    assert(gModHudTestRectCount == 0 && gModHudTestTextCount == 0); /* Unload clears owner popups. */
    assert(mod_runtime_set_enabled(8, true));
    mod_runtime_hud_render();
    assert(gModHudTestRectCount == 3 && gModHudTestTextCount == 3);
#ifdef EXTERNAL_DATA
    assert(gModHudTestTextureCount == 1); /* Native texture handle is restored with its owning callback. */
#endif
    assert(mod_runtime_menu_set_value(8, 0, 0));
    mod_audio_mix(pcm, 128, 1, 1);
    for (size_t i = 0; i < 256; ++i) assert(pcm[i] == 0);
    assert(sSpawnedNativeObjectCount == 3);
    assert(sSpawnedNativeModels[0] == MODEL_GOOMBA && sSpawnedNativeModels[1] == MODEL_GOOMBA);
    assert(sSpawnedNativeModels[2] == MODEL_NONE);
    assert(sSpawnedNativeObjects[0].activeFlags == ACTIVE_FLAG_DEACTIVATED); /* Failed setup was discarded. */
    assert(sSpawnedNativeObjects[1].behavior == bhvGoomba);
    assert(sSpawnedNativeObjects[1].parentObj == &sSpawnedNativeObjects[1]);
    assert(sSpawnedNativeObjects[1].oPosX == 12.5f && sSpawnedNativeObjects[1].oPosY == 34.5f &&
           sSpawnedNativeObjects[1].oPosZ == 56.5f);
    assert(sSpawnedNativeObjects[1].oHomeX == 12.5f && sSpawnedNativeObjects[1].oHomeY == 34.5f &&
           sSpawnedNativeObjects[1].oHomeZ == 56.5f);
    assert(sSpawnedNativeObjects[1].oAction == 909);
    assert(sSpawnedNativeObjects[2].parentObj == &sSpawnedNativeObjects[2]);
    assert(sSpawnedNativeObjects[2].oPosX == 100.0f && sSpawnedNativeObjects[2].oPosY == 200.0f &&
           sSpawnedNativeObjects[2].oPosZ == 300.0f);
    assert(sSpawnedNativeObjects[1].header.gfx.scale[0] == 2.0f &&
           sSpawnedNativeObjects[1].header.gfx.scale[1] == 2.0f &&
           sSpawnedNativeObjects[1].header.gfx.scale[2] == 2.0f);
    assert(sSpawnedNativeObjects[1].header.gfx.sharedChild == (struct GraphNode *)1);
    assert(sSpawnedNativeObjects[1].header.gfx.unk38.curAnim == NULL);
    assert(mod_runtime_mod_enabled(3)); /* Setup failure is isolated to its object. */
    assert(sLastCompatSoundBits == 0x1357);
    assert(sLastCompatSoundPosition[0] == 12.0f && sLastCompatSoundPosition[1] == 34.0f &&
           sLastCompatSoundPosition[2] == 56.0f);
    assert(sLastGraphSoundBits == 0x2468);
    assert(sLastGraphSoundPosition[0] == 12.0f && sLastGraphSoundPosition[1] == 34.0f &&
           sLastGraphSoundPosition[2] == 56.0f);
    assert((sMarioObject.header.gfx.node.flags & GRAPH_RENDER_ACTIVE) == 0);
    assert(sMarioObject.header.gfx.cameraToObject[0] == 12.0f &&
           sMarioObject.header.gfx.cameraToObject[1] == 34.0f &&
           sMarioObject.header.gfx.cameraToObject[2] == 56.0f);
    assert(mod_runtime_mod_enabled(7));
    assert(gMarioState->numCoins == 178); /* Native-width bytecode runs; the alternate-width twin is skipped. */
    assert(!mod_runtime_mod_enabled(4));
    assert(mod_runtime_mod_enabled(2)); /* Missing API callback failure is isolated to that hook. */
    assert(gMarioState->numCoins == 178); /* Relative modules, per-mod cache isolation, architecture skip, and coroutine budget. */
    assert(gMarioState->numLives == 111); /* Module and callback-relative require plus isolated failure. */
    assert(!mod_runtime_set_enabled(4, true)); /* Failed initialization was cleaned and remains disabled. */
    assert(gMarioState->action & ACT_FLAG_CUSTOM_ACTION);
    assert((gMarioState->action & ACT_GROUP_MASK) == ACT_GROUP_AIRBORNE);

    mod_runtime_update();
    assert(gMarioState->numCoins == 192);
    assert(gMarioState->numLives == 111);

    mod_runtime_after_mario_update(gMarioState);
    assert(gMarioState->pos[0] == 1.0f);
    assert(gMarioState->pos[2] == 5.0f);

    int in_loop = 0;
    assert(mod_runtime_mario_action(gMarioState, &in_loop));
    assert(!in_loop && gMarioState->actionTimer == 1);
    gMarioState->vel[1] = 10.0f;
    assert(mod_runtime_mario_gravity(gMarioState));
    assert(gMarioState->vel[1] == 8.0f);
    assert(!mod_runtime_allow_interact(gMarioState, &sSpawnedObject, 0x1234));
    assert(mod_runtime_allow_interact(gMarioState, &sSpawnedObject, 0x5678));
    assert(mod_runtime_allow_interact(gMarioState, &sSpawnedObject, 0x9abc));

    uint32_t incoming = 0x12345678;
    bool cancel = false;
    mod_runtime_before_set_action(gMarioState, &incoming, 7, &cancel);
    assert(!cancel && incoming == ACT_JUMP);

    sTestAnimationPointers[0] = &sTestAnimationData[0];
    sTestAnimationPointers[1] = &sTestAnimationData[1];
    sSpawnedObject.oAnimations = sTestAnimationPointers;
    sSpawnedObject.collisionData = sTestCollisionData;
    sLastCurObjAnimationIndex = -1;
    sCollisionModelCalls = 0;
    gCurrentObject = &sSpawnedObject;
    mod_runtime_behavior_tick();
    assert(sSpawnedObject.oPosX == 4.0f && sSpawnedObject.oPosY == 2.0f);
    assert(sLastCurObjAnimationIndex == 1 &&
           sSpawnedObject.header.gfx.unk38.curAnim == &sTestAnimationData[1]);
    assert(sCollisionModelCalls == 1);
    mod_runtime_behavior_tick();
    assert(sSpawnedObject.oPosY == 4.0f);
    assert(sCollisionModelCalls == 2);
    mod_runtime_update();
    assert(gMarioState->numStars == 4);
    assert(gMarioState->numCoins == 206);

    assert(mod_runtime_set_enabled(0, false));
    assert(!mod_runtime_mod_enabled(0));
    assert(sMarioObject.header.gfx.unk38.curAnim == NULL); /* Owner unload clears native animation references. */
    int coins_before = gMarioState->numCoins;
    mod_runtime_update();
    assert(gMarioState->numCoins == coins_before + 13);

    int stars_before_render = gMarioState->numStars;
    mod_runtime_object_render(&sMarioObject);
    assert(gMarioState->numStars == stars_before_render + 1); /* One mod sets the flag; another receives the native render event. */

    sMarioObject.header.gfx.pos[0] = 11.0f;
    sMarioObject.header.gfx.angle[1] = 23;
    sMarioObject.header.gfx.scale[0] = 2.0f;
    struct GraphNodeObject mirror = {0};
    mirror.pos[0] = 100.0f;
    mirror.pos[1] = 200.0f;
    mirror.pos[2] = 300.0f;
    mirror.node.flags = GRAPH_RENDER_ACTIVE;
    mod_runtime_mirror_mario_render(&mirror, 0);
    assert(mirror.pos[0] == 100.5f && mirror.node.flags == GRAPH_RENDER_ACTIVE);
    float mirror_shadow[3] = {0};
    assert(mod_runtime_graph_node_shadow_position(&mirror, mirror_shadow));
    assert(mirror_shadow[0] == 98.5f && mirror_shadow[1] == 203.0f && mirror_shadow[2] == 300.0f);
    assert(mod_runtime_set_enabled(11, false));
    mirror.pos[0] = 150.0f;
    mod_runtime_mirror_mario_render(&mirror, 0);
    assert(mirror.pos[0] == 150.0f);
    assert(mod_runtime_set_enabled(11, true));
    mod_runtime_mirror_mario_render(&mirror, 0);
    assert(mirror.pos[0] == 150.5f);

    assert(mod_runtime_set_enabled(3, false));
    int stars_after_render_unload = gMarioState->numStars;
    mod_runtime_object_render(&sMarioObject);
    assert(gMarioState->numStars == stars_after_render_unload); /* Owner cleanup disables the shared render hook. */
    float old_y = sSpawnedObject.oPosY;
    assert(sSpawnedObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
    assert(sSpawnedNativeObjects[0].activeFlags == ACTIVE_FLAG_DEACTIVATED);
    assert(sSpawnedNativeObjects[1].activeFlags == ACTIVE_FLAG_DEACTIVATED);
    assert(sSpawnedNativeObjects[2].activeFlags == ACTIVE_FLAG_DEACTIVATED);
    mod_runtime_behavior_tick();
    assert(sSpawnedObject.oPosY == old_y);

    assert(mod_runtime_set_enabled(0, true));
    assert(mod_runtime_mod_enabled(0));
    assert(sMarioObject.header.gfx.unk38.curAnim != NULL); /* Re-enable re-registers its owned animation. */
    int32_t step_result = -1;
    int coins_before_step_hook = gMarioState->numCoins;
    assert(mod_runtime_before_phys_step(gMarioState, MOD_RUNTIME_STEP_TYPE_AIR, 55, &step_result));
    assert(step_result == AIR_STEP_LANDED);
    assert(gMarioState->numCoins == coins_before_step_hook + 1); /* First output stops later physics hooks. */
    assert(mod_runtime_before_phys_step(gMarioState, MOD_RUNTIME_STEP_TYPE_GROUND, 0, &step_result));
    assert(step_result == GROUND_STEP_NONE);
    assert(mod_runtime_before_phys_step(gMarioState, MOD_RUNTIME_STEP_TYPE_WATER, 0, &step_result));
    assert(step_result == WATER_STEP_CANCELLED);
    assert(mod_runtime_before_phys_step(gMarioState, MOD_RUNTIME_STEP_TYPE_HANG, 0, &step_result));
    assert(step_result == 77);
    assert(!mod_runtime_before_phys_step(gMarioState, 99, 0, &step_result));
    assert(gMarioState->numCoins == coins_before_step_hook + 1);
    gMarioState->health = 4660;
    mod_runtime_update();
    assert(gMarioState->health == GROUND_STEP_HIT_WALL);
    gMarioState->health = 4661;
    mod_runtime_update();
    assert(gMarioState->health == 123);
    gMarioState->health = 4662;
    mod_runtime_update();
    assert(gMarioState->health == WATER_STEP_CANCELLED);
    gMarioState->action = 1; /* Fixture selects the legacy function API on the next update. */
    gMarioState->actionTimer = 0;
    mod_runtime_update();
    assert(mod_runtime_mario_action_interaction_type(gMarioState) == INT_GROUND_POUND_OR_TWIRL);
    int legacy_action_in_loop = TRUE;
    assert(mod_runtime_mario_action(gMarioState, &legacy_action_in_loop));
    assert(legacy_action_in_loop == FALSE && gMarioState->actionTimer == 7);
    mod_runtime_shutdown();
    create_dynos_behavior_fixture(argv[1]);
    snprintf(fs_writepath, sizeof(fs_writepath), "%s/dynos-isolated", argv[1]);
    reset_game_state();
    assert(mod_runtime_init());
    assert(mod_runtime_mod_count() == 1);
    mod_runtime_update();
    assert(mod_runtime_mod_enabled(0));
    assert(gMarioState->actionArg >= 0x70000000u);
    assert(sSpawnedObject.behavior != NULL);
    mod_runtime_hud_render();
    assert(gModHudTestTextureCount == 1 && gModHudTestTextureWidth == 2 &&
           gModHudTestTextureHeight == 2 && gModHudTestTextureScale[0] == 1.5f &&
           gModHudTestTextureScale[1] == 2.0f && gModHudTestTextureFormat == G_IM_FMT_RGBA &&
           gModHudTestTextureSize == TEST_DYNOS_TEX_SIZE && gModHudTextureSawRotation);
    int initial_hurt_counter = gMarioState->hurtCounter;
    gCurrentObject = &sSpawnedObject;
    assert(mod_runtime_behavior_call_lua(1));
    assert(gMarioState->hurtCounter == (uint8_t)(initial_hurt_counter + 1));
    assert(mod_runtime_behavior_call_lua(1));
    assert(gMarioState->hurtCounter == (uint8_t)(initial_hurt_counter + 2));
    assert(!mod_runtime_behavior_call_lua(2));
    assert(sSpawnedObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
    gCurrentObject = NULL;
    uint32_t original_dynamic_id = gMarioState->actionArg;
    assert(mod_runtime_set_enabled(0, false));
    assert(!mod_runtime_mod_enabled(0));
    assert(sSpawnedObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
    mod_runtime_hud_render();
    assert(gModHudTestTextureCount == 0); /* Unload removes the callback before freeing its DynOS texture. */
    assert(mod_runtime_set_enabled(0, true));
    assert(mod_runtime_mod_enabled(0));
    assert(gMarioState->actionArg != original_dynamic_id);
    mod_runtime_hud_render();
    assert(gModHudTestTextureCount == 1 && gModHudTestTextureWidth == 2 &&
           gModHudTestTextureHeight == 2); /* Reload decodes the resource and re-registers its HUD callback. */
    int reloaded_hurt_counter = gMarioState->hurtCounter;
    gCurrentObject = &sSpawnedObject;
    assert(mod_runtime_behavior_call_lua(1));
    assert(gMarioState->hurtCounter == (uint8_t)(reloaded_hurt_counter + 1));
    gCurrentObject = NULL;
    mod_runtime_shutdown();
    puts("mod runtime host regression checks passed");
    return 0;
}
