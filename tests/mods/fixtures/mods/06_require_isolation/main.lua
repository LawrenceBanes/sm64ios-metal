local value = require("scripts/sub/value")

hook_event(HOOK_UPDATE, function()
    gMarioState.numCoins = gMarioState.numCoins + value.coinBonus
end)
