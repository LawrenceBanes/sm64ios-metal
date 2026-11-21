local texture = get_texture_info("fixture_1024")
assert(texture ~= nil and texture.width == 1024 and texture.height == 512)

hook_event(HOOK_BEFORE_MARIO_UPDATE, function(m)
    m.actionArg = m.actionArg + 1
end)

hook_event(HOOK_ON_HUD_RENDER, function()
    djui_hud_render_rect(0, 0, 128, 64)
    djui_hud_render_texture(texture, 0, 0, 0.125, 0.125)
end)
