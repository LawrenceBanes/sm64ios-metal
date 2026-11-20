local helper = require("scripts/helper")
local bytecodeBonus = require("scripts/precompiled")
assert(require("scripts/precompiled") == bytecodeBonus)

hook_event(HOOK_UPDATE, function()
    gMarioState.numCoins = gMarioState.numCoins + helper.coinBonus + bytecodeBonus
end)

hook_event(HOOK_ON_OBJECT_RENDER, function(object)
    if object == gMarioState.marioObj then
        gMarioState.numStars = gMarioState.numStars + 1
    end
end)

hook_event(HOOK_OBJECT_SET_MODEL, function(object, nativeModel, extendedModel)
    if object.oAction == 909 then
        gMarioState.actionArg = nativeModel * 1000 + extendedModel
    end
end)
