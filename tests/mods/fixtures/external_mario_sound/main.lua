-- Host integration fixture mirroring an upstream CoopDX character-sound call.
function mission82_sound_action(m)
    if m.action == ACT_JUMP then
        play_mario_sound(m, CHAR_SOUND_TWIRL_BOUNCE, CHAR_SOUND_TWIRL_BOUNCE)
    end
end

hook_event(HOOK_ON_SET_MARIO_ACTION, mission82_sound_action)
