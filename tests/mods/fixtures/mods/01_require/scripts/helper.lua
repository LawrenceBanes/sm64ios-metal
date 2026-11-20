gMarioState.numLives = gMarioState.numLives + 10
local nested = require("sub/value")
local ran = false
hook_event(HOOK_UPDATE, function()
    if ran then return end
    ran = true
    local runtime = require("sub/runtime")
    gMarioState.numLives = gMarioState.numLives + runtime.lifeBonus
end)
return { coinBonus = 2 + nested.coinBonus }
