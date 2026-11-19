local behavior = hook_behavior(nil, OBJ_LIST_DEFAULT, true,
    function(obj)
        obj.oPosX = 4
        obj.oFlags = OBJ_FLAG_UPDATE_GFX_POS_AND_ANGLE
        cur_obj_init_animation(1)
    end,
    function(obj)
        obj.oPosY = obj.oPosY + 2
        load_object_collision_model()
    end,
    "compatibility_fixture_behavior")

assert(max(1, 7, 3) == 7)
assert(math.min(1, 7, 3) == 1)
assert(gMarioState.area.index == 1)
assert(gMarioState.area.camera.mode == gCamera.mode)
assert(type(camera_config_is_free_cam_enabled()) == "boolean")
assert(type(camera_config_is_x_inverted()) == "boolean")
assert(type(camera_config_is_y_inverted()) == "boolean")
local previousLakituYaw = gLakituState.yaw
gLakituState.yaw = previousLakituYaw + 1
assert(gLakituState.yaw == previousLakituYaw + 1)
gLakituState.yaw = previousLakituYaw
local previousLakituPosX = gLakituState.pos.x
gLakituState.pos.x = previousLakituPosX + 1
assert(gLakituState.pos.x == previousLakituPosX + 1)
gLakituState.pos.x = previousLakituPosX

local goombaHookId = hook_behavior(id_bhvGoomba, OBJ_LIST_GENACTOR, false, nil,
    function(obj) obj.oSubAction = obj.oSubAction + 1 end, "compatibility_goomba_observer")
assert(goombaHookId == id_bhvGoomba)
local woodenPostHookId = hook_behavior(id_bhvWoodenPost, OBJ_LIST_SURFACE, false,
    function(obj) obj.oAction = 100 end,
    function(obj) obj.oAction = obj.oAction + 1 end, "compatibility_wooden_post_hook")
assert(woodenPostHookId == id_bhvWoodenPost)
local woodenPostReplacementId = hook_behavior(id_bhvWoodenPost, OBJ_LIST_SURFACE, true, nil,
    function(obj)
        obj.oAction = obj.oAction + 10
        obj.oSubAction = obj.oSubAction + 1
        local pipeCollision = smlua_collision_util_get("warp_pipe_seg3_collision_03009AC8")
        assert(pipeCollision ~= nil)
        obj.collisionData = pipeCollision
        assert(obj.collisionData == pipeCollision)
        assert(not pcall(get_object_list_from_behavior, pipeCollision))
    end, "compatibility_wooden_post_replacement")
assert(woodenPostReplacementId == id_bhvWoodenPost)
assert(smlua_collision_util_get("mission69_missing_collision") == nil)

hook_on_sync_table_change(gGlobalSyncTable, "fixtureField", "global-tag", function(tag, oldValue, newValue)
    assert(tag == "global-tag")
    gMarioState.hurtCounter = (oldValue or 0) * 100 + newValue
end)
gGlobalSyncTable.fixtureField = 1
gGlobalSyncTable.fixtureField = 1 -- Same-value writes do not dispatch a change.
gGlobalSyncTable.fixtureField = 2

hook_on_sync_table_change(gPlayerSyncTable[0], "fixtureField", "player-tag", function(tag, oldValue, newValue)
    assert(tag == "player-tag")
    gMarioState.health = 1000 + newValue
end)
gPlayerSyncTable[0].fixtureField = 3
assert(gMarioState.hurtCounter == 102 and gMarioState.health == 1003)

assert(id_bhvGoomba == 199)
assert(id_bhv_max_count == 541)
assert(get_id_from_behavior(nil) == id_bhv_max_count)
assert(get_behavior_from_id(id_bhvAmbientLight) == nil)
local goombaBehaviorPointer = get_behavior_from_id(id_bhvGoomba)
assert(type(goombaBehaviorPointer) == "userdata")
assert(goombaBehaviorPointer._pointer > 0)
assert(goombaBehaviorPointer == get_behavior_from_id(id_bhvGoomba))
assert(get_id_from_behavior(goombaBehaviorPointer) == id_bhvGoomba)
assert(get_behavior_name_from_id(id_bhvGoomba) == "bhvGoomba")
assert(get_behavior_name_from_id(-1) == nil)
assert(get_object_list_from_behavior(goombaBehaviorPointer) == OBJ_LIST_GENACTOR)
assert(not pcall(get_object_list_from_behavior, nil))
local secondBehavior = hook_behavior(nil, OBJ_LIST_GENACTOR, false,
    function() end, function() end, "second_compatibility_fixture_behavior")
assert(secondBehavior ~= behavior)
assert(get_behavior_name_from_id(secondBehavior) == "second_compatibility_fixture_behavior")
assert(get_id_from_behavior(get_behavior_from_id(secondBehavior)) == secondBehavior)
local spawned = spawn_object(nil, MODEL_NONE, behavior)
assert(spawned.globalPlayerIndex == 0)
spawned.globalPlayerIndex = 0
assert(not pcall(function() spawned.globalPlayerIndex = 1 end))
assert(get_id_from_behavior(spawned.behavior) == behavior)
assert(spawned.behavior._pointer == get_behavior_from_id(behavior)._pointer)
assert(get_id_from_behavior(get_behavior_from_id(behavior)) == behavior)
assert(get_object_list_from_behavior(get_behavior_from_id(behavior)) == OBJ_LIST_DEFAULT)
gMarioState.marioObj.hookRender = 23
assert(gMarioState.marioObj.hookRender == 23)
assert(not pcall(function() gMarioState.marioObj.hookRender = 256 end))
local firstGoomba = obj_get_first_with_behavior_id(id_bhvGoomba)
assert(firstGoomba ~= nil and firstGoomba.oAction == 101)
firstGoomba.header.gfx.shadowPos.x = 41
firstGoomba.header.gfx.shadowPos.y = 42
firstGoomba.header.gfx.shadowPos.z = 43
firstGoomba.header.gfx.disableAutomaticShadowPos = true
assert(firstGoomba.header.gfx.disableAutomaticShadowPos)
set_object_respawn_info_bits(firstGoomba, 0x12)
set_object_respawn_info_bits(nil, 0xFF)
assert(not pcall(set_object_respawn_info_bits, firstGoomba, 256))
obj_spawn_loot_yellow_coins(firstGoomba, 3, 20)
assert(not pcall(obj_spawn_loot_yellow_coins, firstGoomba, 129, 20))
assert(not pcall(obj_spawn_loot_yellow_coins, firstGoomba, 1, math.huge))
assert(obj_has_model_extended(nil, E_MODEL_GOOMBA) == false)
obj_set_model_extended(firstGoomba, E_MODEL_GOOMBA)
assert(obj_has_model_extended(firstGoomba, E_MODEL_GOOMBA) == true)
assert(obj_has_model_extended(firstGoomba, E_MODEL_EXCLAMATION_BOX) == false)
assert(obj_has_model_extended(firstGoomba, E_MODEL_WHITE_PARTICLE_SMALL) == false)
obj_set_model_extended(firstGoomba, E_MODEL_NONE)
assert(obj_has_model_extended(firstGoomba, E_MODEL_NONE) == true)
obj_set_model_extended(firstGoomba, E_MODEL_GOOMBA)
assert(obj_get_field_u32(firstGoomba, 0x31) == 101)
obj_set_field_u32(firstGoomba, 0x31, 404)
assert(firstGoomba.oAction == 404 and obj_get_field_u32(firstGoomba, 0x31) == 404)
obj_set_field_u32(firstGoomba, 0x31, 101)
assert(obj_get_field_u32(nil, 0x31) == 0)
assert(obj_get_field_u32(firstGoomba, -1) == 0)
assert(obj_get_field_u32(firstGoomba, 0x50) == 0)
obj_set_field_u32(firstGoomba, -1, 900)
obj_set_field_u32(firstGoomba, 0x50, 900)
assert(firstGoomba.oAction == 101)
assert(not pcall(obj_set_field_u32, firstGoomba, 0x31, -1))
assert(not pcall(obj_set_field_u32, firstGoomba, 0x31, 0x100000000))
obj_scale(firstGoomba, 1.5)
assert(obj_is_grabbable(nil) == false)
assert(obj_is_attackable(nil) == false)
assert(obj_is_attackable(firstGoomba) == false)
assert(obj_is_bully(nil) == false)
assert(obj_is_bully(firstGoomba) == false)
firstGoomba.oInteractType = INTERACT_BULLY
assert(obj_is_bully(firstGoomba) == true)
firstGoomba.oInteractType = 0
assert(obj_is_coin(nil) == false)
assert(obj_is_coin(firstGoomba) == false)
firstGoomba.oInteractType = INTERACT_COIN
assert(obj_is_coin(firstGoomba) == true)
firstGoomba.oInteractType = 0
assert(obj_is_exclamation_box(nil) == false)
assert(obj_is_exclamation_box(firstGoomba) == false)
local exclamationBox = obj_get_first_with_behavior_id(id_bhvExclamationBox)
assert(exclamationBox ~= nil and obj_is_exclamation_box(exclamationBox) == false)
exclamationBox.oAction = 2
assert(obj_is_exclamation_box(exclamationBox) == true)
exclamationBox.oAction = 1
assert(obj_is_exclamation_box(exclamationBox) == false)
firstGoomba.oInteractType = INTERACT_KOOPA
assert(obj_is_attackable(firstGoomba) == true)
firstGoomba.oInteractType = 0
assert(obj_is_valid_for_interaction(nil) == false)
assert(obj_is_valid_for_interaction(firstGoomba) == true)
firstGoomba.oIntangibleTimer = 1
assert(obj_is_valid_for_interaction(firstGoomba) == false)
firstGoomba.oIntangibleTimer = 0
firstGoomba.oInteractStatus = 0x8000
assert(obj_is_valid_for_interaction(firstGoomba) == false)
firstGoomba.oInteractStatus = 0
assert(obj_is_grabbable(firstGoomba) == false)
assert(obj_is_mushroom_1up(nil) == false)
assert(obj_is_mushroom_1up(firstGoomba) == false)
local oneUp = obj_get_first_with_behavior_id(id_bhv1Up)
assert(oneUp ~= nil and obj_is_mushroom_1up(oneUp) == true)
oneUp.header.gfx.node.flags = oneUp.header.gfx.node.flags | GRAPH_RENDER_INVISIBLE
assert(obj_is_mushroom_1up(oneUp) == false)
oneUp.header.gfx.node.flags = oneUp.header.gfx.node.flags & ~GRAPH_RENDER_INVISIBLE
assert(obj_is_secret(nil) == false)
assert(obj_is_secret(firstGoomba) == false)
local secretTrigger = obj_get_first_with_behavior_id(id_bhvHiddenStarTrigger)
assert(secretTrigger ~= nil and obj_is_secret(secretTrigger) == true)
local coinsBeforeInteraction = gMarioState.numCoins
firstGoomba.oDamageOrCoinValue = 3
assert(interact_coin(gMarioState, 0, firstGoomba) == 0)
assert(gMarioState.numCoins == coinsBeforeInteraction + 3)
assert(firstGoomba.oInteractStatus == 0x8000)
gMarioState.numCoins = coinsBeforeInteraction
firstGoomba.oInteractStatus = 0
mario_stop_riding_object(gMarioState)
mario_stop_riding_and_holding(gMarioState)
local bubbledOk, bubbledError = pcall(mario_set_bubbled, gMarioState)
assert(not bubbledOk and string.find(bubbledError, "no ACT_BUBBLED action", 1, true))
assert(mario_can_bubble(gMarioState) == false and mario_can_bubble(nil) == false)
local networkOk, networkError = pcall(network_send, {})
assert(not networkOk and string.find(networkError, "no packet transport", 1, true))
firstGoomba.oInteractType = INTERACT_GRABBABLE
assert(obj_is_grabbable(firstGoomba) == true)
firstGoomba.oInteractionSubtype = INT_SUBTYPE_NOT_GRABBABLE
assert(obj_is_grabbable(firstGoomba) == false)
firstGoomba.oInteractionSubtype = 0
assert(obj_is_grabbable(firstGoomba) == true)
assert(obj_has_behavior_id(firstGoomba, id_bhvGoomba) == 1)
assert(obj_has_behavior_id(firstGoomba, id_bhvTree) == 0)
assert(obj_has_behavior_id(nil, id_bhvGoomba) == 0)
assert(obj_get_first_with_behavior_id_and_field_s32(id_bhvGoomba, 0x31, 101) == firstGoomba)
assert(obj_get_first_with_behavior_id_and_field_s32(id_bhvGoomba, 0x31, 999) == nil)
assert(obj_get_first_with_behavior_id_and_field_s32(id_bhvGoomba, -1, 101) == nil)
assert(get_id_from_behavior(firstGoomba.behavior) == id_bhvGoomba)
local nextGoomba = obj_get_next_with_same_behavior_id(firstGoomba)
assert(nextGoomba ~= nil and nextGoomba.oAction == 202)
local queryObject = gMarioState.marioObj
queryObject.oPosX, queryObject.oPosY, queryObject.oPosZ = 0, 0, 0
firstGoomba.oPosX, firstGoomba.oPosY, firstGoomba.oPosZ = 80, 0, 0
nextGoomba.oPosX, nextGoomba.oPosY, nextGoomba.oPosZ = 140, 0, 0
assert(obj_get_nearest_object_with_behavior_id(queryObject, id_bhvGoomba) == firstGoomba)
queryObject.oPosX = 130
assert(obj_get_nearest_object_with_behavior_id(queryObject, id_bhvGoomba) == nextGoomba)
nextGoomba.oPosX = 80
queryObject.oPosX = 80
assert(obj_get_nearest_object_with_behavior_id(queryObject, id_bhvGoomba) == firstGoomba)
queryObject.oPosX = 200000
assert(obj_get_nearest_object_with_behavior_id(queryObject, id_bhvGoomba) == nil)
firstGoomba.oPosX, firstGoomba.oPosZ = 0, 0
nextGoomba.oPosX, nextGoomba.oPosZ = 0, 100
obj_set_pos(firstGoomba, 10, -20, 30)
assert(firstGoomba.oPosX == 10 and firstGoomba.oPosY == -20 and firstGoomba.oPosZ == 30)
assert(not pcall(obj_set_pos, firstGoomba, 32768, 0, 0))
obj_set_angle(firstGoomba, -100, 16384, 200)
assert(firstGoomba.oFaceAnglePitch == -100 and firstGoomba.oFaceAngleYaw == 16384 and firstGoomba.oFaceAngleRoll == 200)
assert(firstGoomba.oMoveAnglePitch == -100 and firstGoomba.oMoveAngleYaw == 16384 and firstGoomba.oMoveAngleRoll == 200)
assert(not pcall(obj_set_angle, firstGoomba, 0, 32768, 0))
obj_set_face_angle(firstGoomba, -101, 1000, -200)
assert(firstGoomba.oFaceAnglePitch == -101 and firstGoomba.oFaceAngleYaw == 1000 and firstGoomba.oFaceAngleRoll == -200)
assert(firstGoomba.oMoveAnglePitch == -100 and firstGoomba.oMoveAngleYaw == 16384 and firstGoomba.oMoveAngleRoll == 200)
assert(not pcall(obj_set_face_angle, firstGoomba, 0, 32768, 0))
obj_set_vel(firstGoomba, 1.5, -2, 3.25)
assert(firstGoomba.oVelX == 1.5 and firstGoomba.oVelY == -2 and firstGoomba.oVelZ == 3.25)
assert(not pcall(obj_set_vel, firstGoomba, 0 / 0, 0, 0))
obj_set_vel(nil, 1, 2, 3)
obj_set_pos(firstGoomba, 10, 20, 30)
obj_move_xyz(firstGoomba, -1.5, 2, 0.25)
assert(firstGoomba.oPosX == 8.5 and firstGoomba.oPosY == 22 and firstGoomba.oPosZ == 30.25)
assert(not pcall(obj_move_xyz, firstGoomba, 0, 0 / 0, 0))
obj_move_xyz(nil, 1, 2, 3)
assert(find_ceil_height(1, 2, 3) == 123)
assert(not pcall(find_ceil_height, 0 / 0, 2, 3))
assert(find_floor_height(1, 2, 3) == -123)
assert(not pcall(find_floor_height, 1, 0 / 0, 3))
assert(find_water_level(10, -4) == 6)
assert(not pcall(find_water_level, 0 / 0, 1))
set_anim_to_frame(gMarioState, 12)
set_anim_to_frame(nil, 4)
assert(not pcall(set_anim_to_frame, gMarioState, 32768))
assert(set_mario_anim_with_accel(gMarioState, 42, 65536) == 42)
assert(not pcall(set_mario_anim_with_accel, gMarioState, 42, 0 / 0))
assert(set_character_anim_with_accel(gMarioState, 43, 32768) == 43)
assert(not pcall(set_character_anim_with_accel, gMarioState, 43, 0 / 0))
mario_bonk_reflection(gMarioState, 1)
mario_bonk_reflection(gMarioState, 0)
play_mario_heavy_landing_sound(gMarioState, SOUND_ACTION_TERRAIN_HEAVY_LANDING)
play_mario_action_sound(gMarioState, SOUND_ACTION_TERRAIN_JUMP, 0)
play_course_clear()
play_flip_sounds(gMarioState, 2, 8, 20)
assert(mario_get_floor_class(gMarioState) == SURFACE_CLASS_DEFAULT)
assert(CHAR_SOUND_YAHOO == 2 and CHAR_SOUND_MAX == 44)
assert(network_get_player_text_color_string(0) == "\\#ffffff\\")
assert(not pcall(network_get_player_text_color_string, 1))
assert(get_level_name_ascii(1, 9, 1, 0) == "Bob-omb Battlefield", get_level_name_ascii(1, 9, 1, 0))
assert(get_level_name_ascii(1, 9, 1, -1) == "Bob-omb Battlefield", get_level_name_ascii(1, 9, 1, -1))
assert(get_level_name_ascii(0, 6, 2, 0) == "Castle Upper Floor", get_level_name_ascii(0, 6, 2, 0))
assert(get_level_name_ascii(0, 6, 2, -1) == "Castle Upper Floor", get_level_name_ascii(0, 6, 2, -1))
assert(not pcall(get_level_name_ascii, 65536, 9, 1, 0))
play_character_sound_offset(gMarioState, CHAR_SOUND_YAHOO, 2)
play_character_sound_offset(nil, CHAR_SOUND_YAHOO, 0)
play_character_sound_offset(gMarioState, CHAR_SOUND_MAX, 0)
assert(not pcall(play_character_sound_offset, gMarioState, CHAR_SOUND_YAHOO, -1))
play_character_sound(gMarioState, CHAR_SOUND_YAHOO)
play_character_sound(nil, CHAR_SOUND_YAHOO)
play_character_sound(gMarioState, CHAR_SOUND_MAX)
local marioFlagsBeforeSoundGate = gMarioState.flags
gMarioState.flags = marioFlagsBeforeSoundGate & ~MARIO_MARIO_SOUND_PLAYED
play_character_sound_if_no_flag(gMarioState, CHAR_SOUND_YAHOO, MARIO_MARIO_SOUND_PLAYED)
local marioFlagsAfterFirstSoundGate = gMarioState.flags
play_character_sound_if_no_flag(gMarioState, CHAR_SOUND_YAHOO, MARIO_MARIO_SOUND_PLAYED)
assert(gMarioState.flags == marioFlagsAfterFirstSoundGate)
gMarioState.flags = marioFlagsBeforeSoundGate
assert(CAMERA_MODE_BEHIND_MARIO == 3)
gCamera.pos[1] = 12
assert(gCamera.pos[1] == 12)
set_camera_mode(gCamera, CAMERA_MODE_BEHIND_MARIO, 3)
assert(gCamera.mode == CAMERA_MODE_BEHIND_MARIO)
assert(not pcall(set_camera_mode, gCamera, CAMERA_MODE_BEHIND_MARIO, 32768))
hook_event(HOOK_ON_PACKET_RECEIVE, function() end)
assert(HOOK_ALLOW_PVP_ATTACK == 5 and HOOK_ON_PVP_ATTACK == 6)
assert(HOOK_ON_PLAYER_CONNECTED == 7 and HOOK_ON_PLAYER_DISCONNECTED == 8)
assert(HOOK_USE_ACT_SELECT == 23)
local framesSinceA = gMarioState.framesSinceA
assert(type(framesSinceA) == "number")
gMarioState.framesSinceA = framesSinceA
hook_event(HOOK_ON_PLAYER_CONNECTED, function() gMarioState.actionArg = 999 end)
hook_event(HOOK_ON_PLAYER_DISCONNECTED, function() gMarioState.actionArg = 998 end)
hook_event(HOOK_USE_ACT_SELECT, function(levelNum)
    gMarioState.actionArg = levelNum + 9000
    return false
end)
hook_event(HOOK_ON_DEATH, function(m)
    m.health = 801
    return false
end)
hook_event(HOOK_ALLOW_HAZARD_SURFACE, function(m, hazardType)
    m.actionArg = hazardType
    return hazardType ~= HAZARD_TYPE_LAVA_FLOOR
end)
hook_event(HOOK_ON_PAUSE_EXIT, function(usedExitToCastle)
    gMarioState.actionArg = usedExitToCastle and 1 or 0
    return false
end)
hook_event(HOOK_ON_HUD_RENDER_BEHIND, function()
    djui_hud_render_rect(4, 5, 20, 10)
    gMarioState.actionArg = 3600
end)
hook_event(HOOK_ON_WARP, function(warpType, levelNum, areaIdx, nodeId, warpArg)
    gMarioState.actionArg = warpType * 100000 + levelNum * 1000 + areaIdx * 100 + nodeId * 10 + warpArg
end)
set_camera_shake_from_hit(SHAKE_HIT_FROM_BELOW)
assert(not pcall(set_camera_shake_from_hit, 32768))
set_camera_shake_from_point(SHAKE_POS_MEDIUM, 120, 340, -560)
assert(not pcall(set_camera_shake_from_point, 99, 120, 340, -560))
assert(not pcall(set_camera_shake_from_point, SHAKE_POS_SMALL, math.huge, 0, 0))
play_transition(WARP_TRANSITION_FADE_INTO_COLOR, 14, 255, 128, 0)
assert(not pcall(play_transition, WARP_TRANSITION_FADE_INTO_COLOR, 32768, 255, 255, 255))
play_cap_music(1234)
play_shell_music()
stop_shell_music()
firstGoomba.oPosX, firstGoomba.oPosY, firstGoomba.oPosZ = 0, 0, 0
assert(obj_angle_to_object(firstGoomba, nextGoomba) == 0x4000)
assert(obj_check_hitbox_overlap(nil, nextGoomba) == false)
firstGoomba.hitboxRadius, nextGoomba.hitboxRadius = 40, 40
firstGoomba.hitboxHeight, nextGoomba.hitboxHeight = 100, 100
assert(obj_check_hitbox_overlap(firstGoomba, nextGoomba) == false)
nextGoomba.hitboxRadius = 60
assert(obj_check_hitbox_overlap(firstGoomba, nextGoomba) == true)
nextGoomba.oPosY = 250
assert(obj_check_hitbox_overlap(firstGoomba, nextGoomba) == false)
nextGoomba.oPosY = 100
assert(obj_check_hitbox_overlap(firstGoomba, nextGoomba) == true)
local previousUsed, previousHeld = gMarioState.usedObj, gMarioState.heldObj
gMarioState.usedObj, gMarioState.heldObj = firstGoomba, nil
mario_grab_used_object(gMarioState)
assert(gMarioState.heldObj == firstGoomba)
gMarioState.usedObj, gMarioState.heldObj = previousUsed, previousHeld
assert(obj_get_first_with_behavior_id_and_field_s32(id_bhvGoomba, 0x31, 202) == nextGoomba)
assert(obj_get_first(OBJ_LIST_GENACTOR).oAction == 101)
assert(obj_get_next(firstGoomba).oAction == 202)
obj_mark_for_deletion(nextGoomba)
assert(obj_get_next_with_same_behavior_id(firstGoomba) == nil)
hook_event(HOOK_UPDATE, function()
    gMarioState.numStars = spawned.oPosY

    local marioGfx = gMarioState.marioObj.header.gfx
    assert(marioGfx.node.flags == 0)
    marioGfx.node.flags = marioGfx.node.flags | GRAPH_RENDER_ACTIVE
    assert((marioGfx.node.flags & GRAPH_RENDER_ACTIVE) ~= 0)
    marioGfx.node.flags = marioGfx.node.flags & ~GRAPH_RENDER_ACTIVE
    assert((marioGfx.node.flags & GRAPH_RENDER_ACTIVE) == 0)

    marioGfx.cameraToObject.x = 12
    marioGfx.cameraToObject.y = 34
    marioGfx.cameraToObject.z = 56
    play_sound(0x2468, marioGfx.cameraToObject)
end)

assert(E_MODEL_GOOMBA == 58)
assert(E_MODEL_MAX == 381)
assert(A_BUTTON == 0x8000 and D_JPAD == 0x0400)
assert(GRAPH_RENDER_ACTIVE == 1)
assert((gMarioState.controller.buttonPressed & (A_BUTTON | D_JPAD)) == (A_BUTTON | D_JPAD))
assert((gMarioState.controller.buttonDown & D_JPAD) ~= 0)
local didSpawnNative = false
hook_event(HOOK_UPDATE, function()
    if didSpawnNative then return end
    didSpawnNative = true

    local badBehaviorOk = pcall(spawn_non_sync_object, E_MODEL_MAX + 20, E_MODEL_GOOMBA,
        1, 2, 3, function() end)
    assert(not badBehaviorOk)
    local customModelOk, customModelError = pcall(spawn_non_sync_object, id_bhvGoomba, E_MODEL_MAX,
        1, 2, 3, function() end)
    assert(not customModelOk and string.find(customModelError, "DynOS"), tostring(customModelError))
    local unloadedModelOk, unloadedModelError = pcall(spawn_non_sync_object, id_bhvGoomba, E_MODEL_STAR,
        1, 2, 3, function() end)
    assert(not unloadedModelOk and string.find(unloadedModelError, "not loaded"))
    local syncOk, syncError = pcall(spawn_sync_object, id_bhvGoomba, E_MODEL_GOOMBA,
        1, 2, 3, function() end)
    assert(not syncOk and string.find(syncError, "multiplayer"))

    local failedSetup = spawn_non_sync_object(id_bhvGoomba, E_MODEL_GOOMBA,
        1, 2, 3, function() error("intentional setup failure") end)
    assert(failedSetup == nil)

    local nativeObject = spawn_non_sync_object(id_bhvGoomba, E_MODEL_GOOMBA,
        12.5, 34.5, 56.5, function(obj)
            assert(obj.parentObj.oPosX == 12.5)
            assert(obj.oHomeY == 34.5)
            obj.oAction = 909
        end)
    assert(nativeObject ~= nil)
    assert(get_id_from_behavior(nativeObject.behavior) == id_bhvGoomba)
    obj_set_model_extended(nativeObject, E_MODEL_GOOMBA)
    obj_set_model_extended(nil, E_MODEL_GOOMBA)
    obj_scale_xyz(nativeObject, 1.5, 2.0, 0.75)
    assert(nativeObject.header.gfx.scale.x == 1.5)
    assert(nativeObject.header.gfx.scale.y == 2.0)
    assert(nativeObject.header.gfx.scale.z == 0.75)
    assert(not pcall(obj_scale_xyz, nativeObject, 1, 2, 0/0))
    local missingModelOk, missingModelError = pcall(obj_set_model_extended, nativeObject, E_MODEL_ERROR_MODEL)
    assert(not missingModelOk and string.find(missingModelError, "DynOS"), tostring(missingModelError))
    obj_scale_random(nativeObject, 4, 1)
    smlua_anim_util_set_animation(nativeObject, "missing_fixture_animation")

    local noSetupObject = spawn_non_sync_object(id_bhvGoomba, E_MODEL_NONE,
        100, 200, 300, nil)
    assert(noSetupObject ~= nil and noSetupObject.parentObj.oPosZ == 300)
end)
