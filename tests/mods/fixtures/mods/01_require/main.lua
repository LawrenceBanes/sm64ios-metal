local helper = require("scripts/helper")
local bytecodeBonus = require("scripts/precompiled")
assert(require("scripts/precompiled") == bytecodeBonus)

hook_event(HOOK_UPDATE, function()
    gMarioState.numCoins = gMarioState.numCoins + helper.coinBonus + bytecodeBonus
end)
