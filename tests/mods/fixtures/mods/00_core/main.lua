TEST_ACTION = allocate_mario_action(ACT_FLAG_AIR | ACT_GROUP_AIRBORNE)
assert((TEST_ACTION & ACT_GROUP_MASK) == ACT_GROUP_AIRBORNE)
assert((TEST_ACTION & ACT_FLAG_CUSTOM_ACTION) ~= 0)

hook_mario_action(TEST_ACTION, {
    every_frame = function(m)
        m.actionTimer = m.actionTimer + 1
        return false
    end,
    gravity = function(m)
        m.vel.y = m.vel.y - 2
    end,
})

hook_event(HOOK_UPDATE, function()
    gMarioState.numCoins = gMarioState.numCoins + 1
    gMarioState.action = TEST_ACTION
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
