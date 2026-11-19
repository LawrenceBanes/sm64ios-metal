local behavior = hook_behavior(nil, OBJ_LIST_DEFAULT, true,
    function(obj)
        obj.oPosX = 4
        obj.oFlags = OBJ_FLAG_UPDATE_GFX_POS_AND_ANGLE
    end,
    function(obj)
        obj.oPosY = obj.oPosY + 2
    end,
    "compatibility_fixture_behavior")

assert(id_bhvGoomba == 199)
assert(id_bhv_max_count == 541)
assert(get_id_from_behavior(nil) == id_bhv_max_count)
local missingBehaviorOk, missingBehaviorError = pcall(get_behavior_from_id, id_bhvAmbientLight)
assert(not missingBehaviorOk and missingBehaviorError ~= nil)
assert(get_id_from_behavior(get_behavior_from_id(id_bhvGoomba)) == id_bhvGoomba)
local secondBehavior = hook_behavior(nil, OBJ_LIST_GENACTOR, false,
    function() end, function() end, "second_compatibility_fixture_behavior")
assert(secondBehavior ~= behavior)
assert(get_id_from_behavior(get_behavior_from_id(secondBehavior)) == secondBehavior)
local spawned = spawn_object(nil, MODEL_NONE, behavior)
assert(get_id_from_behavior(spawned.behavior) == behavior)
assert(get_id_from_behavior(get_behavior_from_id(behavior)) == behavior)
local firstGoomba = obj_get_first_with_behavior_id(id_bhvGoomba)
assert(firstGoomba ~= nil and firstGoomba.oAction == 101)
assert(get_id_from_behavior(firstGoomba.behavior) == id_bhvGoomba)
local nextGoomba = obj_get_next_with_same_behavior_id(firstGoomba)
assert(nextGoomba ~= nil and nextGoomba.oAction == 202)
assert(obj_get_first(OBJ_LIST_GENACTOR).oAction == 101)
assert(obj_get_next(firstGoomba).oAction == 202)
obj_mark_for_deletion(nextGoomba)
assert(obj_get_next_with_same_behavior_id(firstGoomba) == nil)
hook_event(HOOK_UPDATE, function()
    gMarioState.numStars = spawned.oPosY
end)

assert(E_MODEL_GOOMBA == 58)
assert(E_MODEL_MAX == 381)
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

    local noSetupObject = spawn_non_sync_object(id_bhvGoomba, E_MODEL_NONE,
        100, 200, 300, nil)
    assert(noSetupObject ~= nil and noSetupObject.parentObj.oPosZ == 300)
end)
