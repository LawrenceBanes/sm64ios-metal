mirror_render_calls = 0

hook_event(HOOK_MIRROR_MARIO_RENDER, function(mirror, playerIndex)
    assert(playerIndex == 0)
    assert(type(mirror.node.flags) == "number")
    mirror.pos.x = mirror.pos.x + 0.5
    mirror.shadowPos.x = mirror.pos.x - 2
    mirror.shadowPos.y = mirror.pos.y + 3
    mirror.disableAutomaticShadowPos = true

    local mario = gMarioStates[playerIndex].marioObj
    local gfx = mario.header.gfx
    assert(gfx.pos.x == 11 and gfx.angle.y == 23 and gfx.scale.x == 2)
    local offset = gfx.gfxOffset
    assert(offset.active == false and offset.x == 0 and offset.y == 0 and offset.z == 0)
    mirror_render_calls = mirror_render_calls + 1
end)
