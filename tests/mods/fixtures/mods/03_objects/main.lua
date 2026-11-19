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
