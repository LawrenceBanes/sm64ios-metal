-- Minimal SM64CoopDX-shaped demonstration for the iOS Metal compatibility core.
-- This uses native engine adapters and keeps the original Lua script format.
local ACT_IOS_COMPATIBILITY_HOP = allocate_mario_action(ACT_FLAG_AIR)

hook_mario_action(ACT_IOS_COMPATIBILITY_HOP, {
    every_frame = function(m)
        if m.controller.buttonPressed ~= 0 then
            m.vel.y = 48
            set_mario_action(m, ACT_FREEFALL, 0)
            return true
        end
        m.vel.y = m.vel.y - 1.5
        return false
    end,
})

hook_event(HOOK_UPDATE, function()
    if gMarioState.action == ACT_IDLE and gMarioState.controller.buttonPressed ~= 0 then
        set_mario_action(gMarioState, ACT_IOS_COMPATIBILITY_HOP, 0)
    end
end)
