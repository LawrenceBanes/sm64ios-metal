local behavior = hook_behavior(nil, OBJ_LIST_DEFAULT, true,
    function(obj)
        obj.oPosX = 4
        obj.oFlags = OBJ_FLAG_UPDATE_GFX_POS_AND_ANGLE
    end,
    function(obj)
        obj.oPosY = obj.oPosY + 2
    end,
    "compatibility_fixture_behavior")

local spawned = spawn_object(nil, MODEL_NONE, behavior)
hook_event(HOOK_UPDATE, function()
    gMarioState.numStars = spawned.oPosY
end)
