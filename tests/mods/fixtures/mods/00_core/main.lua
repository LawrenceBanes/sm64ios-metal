-- name: Core Fixture Display Name
-- category: test-suite
-- description: Validates CoopDX package metadata behavior.
-- incompatible: metadata-shared-token

TEST_ACTION = allocate_mario_action(ACT_FLAG_AIR | ACT_GROUP_AIRBORNE)
assert(MARIO_ANIM_RUNNING == 0x72)
assert(MARIO_ANIM_CROUCHING == 0x98)
assert((TEST_ACTION & ACT_GROUP_MASK) == ACT_GROUP_AIRBORNE)
assert((TEST_ACTION & ACT_FLAG_CUSTOM_ACTION) ~= 0)
assert(gMarioState.character.type == CT_MARIO)
assert(gMarioState.character.name == "Mario")
assert(gMarioState.character.modelId == MODEL_MARIO)
assert(gMarioState.riddenObj == nil)
gMarioState.riddenObj = gMarioState.marioObj
assert(gMarioState.riddenObj == gMarioState.marioObj)
gMarioState.riddenObj = nil
local bodyState = gMarioState.marioBodyState
assert(bodyState.modelState == 0 or bodyState.modelState == 0x25) -- Startup can run again after re-enable.
bodyState.modelState = 0x25
bodyState.capState = 2
bodyState.punchState = 17
assert(bodyState.modelState == 0x25 and bodyState.capState == 2 and bodyState.punchState == 17)
assert(not pcall(function() bodyState.modelState = 32768 end))
assert(not pcall(function() bodyState.headAngle.x = 1 end))
assert(not pcall(function() return bodyState.mirrorMario end))
assert(not pcall(function() return bodyState.allowPartRotation end))
assert(type(gActiveMods) == "table")
local foundMetadata = false
for _, active in ipairs(gActiveMods) do
    if active.relativePath == "00_core" then
        assert(active.name == "Core Fixture Display Name")
        assert(active.category == "test-suite")
        assert(active.description == "Validates CoopDX package metadata behavior.")
        assert(active.incompatible == "metadata-shared-token")
        foundMetadata = true
    end
end
assert(foundMetadata)
local cameraState = gMarioState.statusForCamera
assert(cameraState.cameraEvent == 0 or cameraState.cameraEvent == 31)
cameraState.cameraEvent = 31
cameraState.pos.x = 12
cameraState.faceAngle.y = 0x4000
assert(cameraState.cameraEvent == 31 and cameraState.pos.x == 12 and cameraState.faceAngle.y == 0x4000)
assert(atan2s(0, 1) == 0)
assert(atan2s(1, 0) == 0x4000)
assert(atan2s(-1, 0) == -0x4000)
assert(atan2s(0, -1) == -0x8000)
assert(not pcall(atan2s, 0 / 0, 1))
assert(approach_s32(2, 6, 3, 3) == 5)
assert(approach_s32(8, 5, 2, 4) == 5)
assert(approach_s32(2147483646, 2147483647, 2147483647, 0) == 2147483647)
assert(not pcall(approach_s32, 0, 1, -1, 1))
assert(math.abs(sins(0)) < 0.0001 and math.abs(coss(0) - 1) < 0.0001)
assert(math.abs(sins(0x4000) - 1) < 0.0001 and math.abs(coss(0x4000)) < 0.0001)
assert(math.abs(sins(0x14000) - 1) < 0.0001) -- Binary angles wrap through the s16 domain.
assert(vec3f_dist({ x = 1, y = 2, z = 3 }, { 4, 6, 3 }) == 5)
assert(vec3f_dist(gMarioState.pos, gMarioState.pos) == 0)
assert(not pcall(vec3f_dist, { x = 0, y = 0 }, { x = 0, y = 0, z = 0 }))
local normalizedTable = { x = 3, y = 0, z = 4 }
vec3f_normalize(normalizedTable)
assert(math.abs(normalizedTable.x - 0.6) < 0.0001 and math.abs(normalizedTable.z - 0.8) < 0.0001)
vec3f_set(normalizedTable, 0, 2, 0)
assert(normalizedTable.x == 0 and normalizedTable.y == 2 and normalizedTable.z == 0)
local copiedVector = { x = 0, y = 0, z = 0 }
vec3f_copy(copiedVector, normalizedTable)
assert(copiedVector.x == 0 and copiedVector.y == 2 and copiedVector.z == 0)
local normalizedArray = { 0, 3, 4 }
vec3f_normalize(normalizedArray)
assert(math.abs(normalizedArray[2] - 0.6) < 0.0001 and math.abs(normalizedArray[3] - 0.8) < 0.0001)
vec3f_set(gGlobalSoundSource, 3, 0, 4)
vec3f_normalize(gGlobalSoundSource)
assert(math.abs(gGlobalSoundSource.x - 0.6) < 0.0001 and math.abs(gGlobalSoundSource.z - 0.8) < 0.0001)
vec3f_set(gDefaultSoundArgs, 0, 2, 0)
vec3f_copy(gGlobalSoundSource, gDefaultSoundArgs)
assert(gGlobalSoundSource.x == 0 and gGlobalSoundSource.y == 2 and gGlobalSoundSource.z == 0)
vec3f_set(gGlobalSoundSource, 0, 0, 0)
vec3f_set(gDefaultSoundArgs, 0, 0, 0)
local rayInfo = collision_find_surface_on_ray(10, 20, 30, 4, 8, 12, 2.5)
assert(rayInfo.surface.type == 0x1234)
assert(rayInfo.hitPos.x == 11 and rayInfo.hitPos.y == 22 and rayInfo.hitPos.z == 33)
assert(not pcall(function() rayInfo.hitPos.x = 0 end))
assert(not pcall(collision_find_surface_on_ray, 0, 0, 0, 1, 1, 1, 9))
gMarioState.specialTripleJump = 1
assert(gMarioState.specialTripleJump == 1)

local fixtureAnimationValues = { 0, 10, 20, 30, 40, 50 }
local fixtureAnimationIndex = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
smlua_anim_util_register_animation("mission69_fixture_anim", ANIM_FLAG_FORWARD,
    0, 0, 0, 1, fixtureAnimationValues, fixtureAnimationIndex)
assert(not pcall(smlua_anim_util_register_animation, "mission69_invalid_anim",
    0, 0, 0, 0, 1, { 70000 }, { 0, 0, 0, 0, 0, 0 }))
smlua_anim_util_register_animation("mission69_fixture_anim", 0, 0, 0, 0, 1,
    { 999 }, { 0, 0, 0, 0, 0, 0 }) -- Duplicate names retain the first registration.
smlua_anim_util_set_animation(gMarioState.marioObj, "mission69_fixture_anim")
local fixtureAnimInfo = gMarioState.marioObj.header.gfx.animInfo
assert(fixtureAnimInfo.curAnim.flags == ANIM_FLAG_FORWARD)
assert(fixtureAnimInfo.curAnim.valuesLength == 6 and fixtureAnimInfo.curAnim.indexLength == 12)
fixtureAnimInfo.animFrame = 3
assert(fixtureAnimInfo.animFrame == 3)
assert(level_trigger_warp(gMarioState, WARP_OP_WARP_DOOR) == 17)

hook_mario_action(TEST_ACTION, {
    every_frame = function(m)
        m.actionTimer = m.actionTimer + 1
        return false
    end,
    gravity = function(m)
        m.vel.y = m.vel.y - 2
    end,
}, INT_PUNCH)

LEGACY_TEST_ACTION = allocate_mario_action(ACT_FLAG_AIR | ACT_GROUP_AIRBORNE)
hook_mario_action(LEGACY_TEST_ACTION, function(m)
    m.actionTimer = m.actionTimer + 7
    return 0 -- Legacy callback uses an integer action-loop result.
end, INT_GROUND_POUND_OR_TWIRL)

hook_event(HOOK_UPDATE, function()
    gMarioState.numCoins = gMarioState.numCoins + 1
    gMarioState.action = gMarioState.action == 1 and LEGACY_TEST_ACTION or TEST_ACTION
    gGlobalSoundSource.x = 12
    gGlobalSoundSource.y = 34
    gGlobalSoundSource.z = 56
    play_sound(0x1357, gGlobalSoundSource)
end)

hook_event(HOOK_MARIO_UPDATE, function(m)
    m.pos.x = m.pos.x + 1
    if m.controller.buttonPressed ~= 0 then
        m.pos.z = m.pos.z + 5
    end
end)

hook_event(HOOK_BEFORE_SET_MARIO_ACTION, function(m, action, actionArg)
    if action == 0x12345678 then
        return ACT_JUMP
    end
end)

hook_event(HOOK_ALLOW_INTERACT, function(m, obj, interactType)
    if interactType == 0x1234 then
        return false
    elseif interactType == 0x5678 then
        return true
    end
end)

hook_event(HOOK_BEFORE_PHYS_STEP, function(m, stepType, stepArg)
    if stepType == STEP_TYPE_AIR and stepArg == 55 then
        gMarioState.numCoins = gMarioState.numCoins + 1000
    end
end)

hook_event(HOOK_UPDATE, function()
    if gMarioState.health == 4660 then
        gMarioState.health = perform_ground_step(gMarioState)
    elseif gMarioState.health == 4661 then
        gMarioState.health = perform_air_step(gMarioState, 23)
    elseif gMarioState.health == 4662 then
        gMarioState.health = perform_water_step(gMarioState)
    end
end)

hook_event(HOOK_ON_SET_CAMERA_MODE, function(camera, mode, frames)
    if mode == 321 then
        assert(camera.mode == 77 and frames == 8)
        gMarioState.numStars = gMarioState.numStars + 1
        return false
    elseif mode == 222 then
        assert(camera.mode == 77 and frames == 4)
        gMarioState.numStars = gMarioState.numStars + 1
        return true
    end
end)

hook_event(HOOK_ON_CHANGE_CAMERA_ANGLE, function(angle)
    if angle == CAM_ANGLE_MARIO then
        gMarioState.numStars = gMarioState.numStars + 1
        return false
    elseif angle == CAM_ANGLE_LAKITU then
        gMarioState.numStars = gMarioState.numStars + 1
        return true
    end
end)

hook_event(HOOK_BEFORE_PHYS_STEP, function(m, stepType, stepArg)
    if stepType == STEP_TYPE_AIR and stepArg == 55 then
        gMarioState.numCoins = gMarioState.numCoins + 1
        return AIR_STEP_LANDED
    elseif stepType == STEP_TYPE_GROUND then
        return GROUND_STEP_NONE
    elseif stepType == STEP_TYPE_WATER then
        return WATER_STEP_CANCELLED
    elseif stepType == STEP_TYPE_HANG then
        return 77
    end
end)
