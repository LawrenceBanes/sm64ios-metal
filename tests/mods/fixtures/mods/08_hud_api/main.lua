local popupCreated = false
hook_event(HOOK_ON_HUD_RENDER, function()
    djui_hud_set_resolution(RESOLUTION_N64)
    djui_hud_set_font(FONT_NORMAL)
    local screen_width = djui_hud_get_screen_width()
    local screen_height = djui_hud_get_screen_height()
    assert(screen_width > 0 and screen_height == 240)

    local text_width, text_height = djui_hud_measure_text("Mission 69!")
    assert(text_width > 0 and text_height > 0)
    djui_hud_set_color(20, 40, 60, 128)
    djui_hud_render_rect(4, 8, text_width + 8, text_height + 4)
    djui_hud_reset_color()
    djui_hud_print_text("Mission 69!", 8, 10, 0.75)

    djui_hud_set_resolution(RESOLUTION_DJUI)
    assert(djui_hud_get_screen_height() == 720)

    if not popupCreated then
        popupCreated = true
        djui_popup_create("Possession ready\nPress A to capture", 2)
    end
end)
