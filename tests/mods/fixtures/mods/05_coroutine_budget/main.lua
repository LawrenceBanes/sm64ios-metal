local exercised = false

hook_event(HOOK_UPDATE, function()
    if exercised then return end
    exercised = true

    local co = coroutine.create(function()
        while true do end
    end)
    local ok, message = coroutine.resume(co)
    assert(not ok and string.find(message, "exceeded 50000 Lua instructions"))
    gMarioState.numCoins = gMarioState.numCoins + 100
end)
