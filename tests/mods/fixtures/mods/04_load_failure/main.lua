hook_event(HOOK_UPDATE, function()
    gMarioState.numLives = gMarioState.numLives + 100
end)
error("intentional initialization failure")
