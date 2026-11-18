TEST_ACTION = allocate_mario_action(ACT_FLAG_AIR)

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
