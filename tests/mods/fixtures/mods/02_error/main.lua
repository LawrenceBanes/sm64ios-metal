hook_event(HOOK_UPDATE, function()
    gMarioState.numLives = gMarioState.numLives + 1
    error("intentional fixture error")
end)
