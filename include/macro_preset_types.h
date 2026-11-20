#ifndef MACRO_PRESET_TYPES_H
#define MACRO_PRESET_TYPES_H

#include "types.h"

struct MacroPreset {
    const BehaviorScript *behavior;
    s16 model;
    s16 param;
};

extern struct MacroPreset MacroObjectPresets[];
extern const unsigned int gMacroObjectPresetCount;

#endif
