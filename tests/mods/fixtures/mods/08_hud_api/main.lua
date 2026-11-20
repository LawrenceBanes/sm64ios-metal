-- Original synthetic sine tone, generated with FFmpeg's sine source.
local sample = assert(audio_sample_load("sound/test-tone.ogg"))
assert(not pcall(audio_sample_load, "../outside.ogg"))
assert(audio_sample_load("sound/missing.ogg") == nil)
local native_coin = get_texture_info("coin_seg3_texture_03005780")
if native_coin then
    assert(native_coin.width == 32 and native_coin.height == 32)
    assert(native_coin.format == 3 and native_coin.size == 2) -- G_IM_FMT_IA / G_IM_SIZ_16b
end
local function sample_position() return { x = 0, y = 0, z = 0 } end
local popupCreated = false
hook_event(HOOK_ON_HUD_RENDER, function()
    hud_hide()
    assert(hud_is_hidden())
    hud_show()
    assert(not hud_is_hidden())
    local flags = hud_get_value(HUD_DISPLAY_FLAGS)
    hud_set_value(HUD_DISPLAY_FLAGS, flags)
    assert(hud_get_value(HUD_DISPLAY_FLAGS) == flags)
    djui_hud_set_resolution(RESOLUTION_N64)
    djui_hud_set_font(FONT_NORMAL)
    assert(not pcall(djui_hud_set_font, 7))
    local screen_width = djui_hud_get_screen_width()
    local screen_height = djui_hud_get_screen_height()
    assert(screen_width > 0 and screen_height == 240)

    local text_width, text_height = djui_hud_measure_text("Mission 69!")
    assert(text_width > 0 and text_height > 0)
    assert(not pcall(djui_hud_set_rotation, 0, math.huge, 0))
    assert(not pcall(djui_hud_set_rotation, 0, 65, 0))
    djui_hud_set_rotation(0x2000, 0.5, 0.25)
    djui_hud_set_color(20, 40, 60, 128)
    assert(pcall(djui_hud_set_color, 127.5, 40, 60, 128.5))
    djui_hud_render_rect(4, 8, text_width + 8, text_height + 4)
    djui_hud_reset_color()
    djui_hud_print_text("Mission 69!", 8, 10, 0.75)

    djui_hud_set_resolution(RESOLUTION_DJUI)
    assert(djui_hud_get_screen_height() == 720)
    if native_coin then djui_hud_render_texture(native_coin, 12, 12, 1, 1) end

    if not popupCreated then
        popupCreated = true
        audio_sample_play(sample, sample_position(), 1)
        audio_sample_play(sample, gGlobalSoundSource, 0.5)
        djui_popup_create("Possession ready\nPress A to capture", 2)
        djui_chat_message_create("Possession system ready")
        assert(not pcall(djui_chat_message_create, string.rep("x", 256)))
    end
end)

hook_mod_menu_button("Destroy sample", function()
    audio_sample_stop(sample)
    audio_sample_destroy(sample)
    assert(not pcall(audio_sample_play, sample, sample_position(), 1))
end)
