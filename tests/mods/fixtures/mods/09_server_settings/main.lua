assert(STAR_EXIT == 0)
assert(STAR_STAY_IN_LEVEL == 1)
assert(STAR_NON_STOP == 2)
assert(gServerSettings.stayInLevelAfterStar == STAR_EXIT)
assert(level_is_vanilla_level(LEVEL_BOB))
assert(not level_is_vanilla_level(-1))
assert(not level_is_vanilla_level(0))
local parsedAreas, parsedObjects, parsedMacros = 0, 0, 0
level_script_parse(LEVEL_BOB, function(area, object, macroBehaviors, macroArgs, macroModels)
    if area ~= nil then
        assert(area == 2 and object == nil and macroBehaviors == nil)
        parsedAreas = parsedAreas + 1
    elseif object ~= nil then
        assert(object.behavior == 199 and object.behaviorArg == 0x12345678)
        assert(object.model == MODEL_GOOMBA and object.posX == 10 and object.posY == 20 and object.posZ == 30)
        assert(object.pitch == 1820 and object.yaw == 16384 and object.roll == 3640)
        parsedObjects = parsedObjects + 1
    elseif macroBehaviors ~= nil then
        assert(macroBehaviors[0] == 199 and macroArgs[0] == 0x11200 and macroModels[0] == MODEL_GOOMBA)
        parsedMacros = parsedMacros + 1
    end
end)
assert(parsedAreas == 1 and parsedObjects == 1 and parsedMacros == 1)
assert(not pcall(function() level_script_parse(-1, function() end) end))
assert(SM64COOPDX_VERSION == "v1.5.1")
local bobStarName = get_star_name_ascii(COURSE_BOB, 1, 0)
assert(bobStarName == "BIG BOB-OMB ON THE SUMMIT", bobStarName)
assert(get_star_name_ascii(COURSE_BOB, 1, -1) == "Big Bob-Omb On The Summit")
assert(get_star_name_ascii(COURSE_NONE, 1, 0) == "Toad Star 1")
assert(get_star_name_ascii(COURSE_NONE, 4, 0) == "Mips Star 1")
assert(get_star_name_ascii(COURSE_BOB, 8, 0) == "A Secret Star!")
assert(get_star_name_ascii(COURSE_BITDW, 1, 0) == "Star 1")
assert(network_global_index_from_local(0) == 0)
assert(network_local_index_from_global(0) == 0)
assert(network_global_index_from_local(1) == UNKNOWN_GLOBAL_INDEX)
assert(network_is_server())
assert(not pcall(network_is_server, 0))
assert(smlua_model_util_get_id("fixture_missing_geo") == E_MODEL_ERROR_MODEL)
assert(gLevelValues.cellHeightLimit == 20000)
gServerSettings.stayInLevelAfterStar = STAR_STAY_IN_LEVEL
assert(gLevelValues.starHeal == 0)
gLevelValues.fixVanishFloors = true
gLevelValues.starHeal = 255
assert(gLevelValues.starHeal == 255)
gLevelValues.mushroom1UpHeal = true
gLevelValues.previewBlueCoins = true
gLevelValues.respawnBlueCoinsSwitch = true
gLevelValues.visibleSecrets = true
gLevelValues.showStarNumber = true
gLevelValues.extendedPauseDisplay = true
gLevelValues.pauseExitAnywhere = true
gLevelValues.coinsRequiredForCoinStar = 80
assert(get_star_name_ascii(COURSE_BOB, 7, 0) == "80 Coins Star")
assert(not pcall(function() gLevelValues.coinsRequiredForCoinStar = 32768 end))
