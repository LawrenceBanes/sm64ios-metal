#ifndef _CHEATS_H
#define _CHEATS_H

#include <stdbool.h>

struct CheatList {
    bool         EnableCheats;
    bool         MoonJump;
    bool         GodMode;
    bool         InfiniteLives;
    bool         SuperSpeed;
    bool         Responsive;
    bool         ExitAnywhere;
    bool         HugeMario;
    bool         TinyMario;
    unsigned int Playground;
    unsigned int SuperSlideSpeed;
};

extern struct CheatList Cheats;

enum {
    CHEAT_HAT_ON = 1, CHEAT_HAT_OFF = 2,
    CHEAT_FLIGHT = 4, CHEAT_BOWSER = 8, CHEAT_SUPERSLIDE = 16,
};
unsigned int cheats_superslide_speed(void);
void cheats_set_superslide_speed(unsigned int speed);
unsigned int cheats_playground_get(void);
void cheats_playground_set(unsigned int mask, unsigned int value);
#endif // _CHEATS_H
