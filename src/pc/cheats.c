#include "cheats.h"

struct CheatList Cheats;

unsigned int cheats_playground_get(void) {
    return __atomic_load_n(&Cheats.Playground, __ATOMIC_RELAXED);
}

void cheats_playground_set(unsigned int mask, unsigned int value) {
    unsigned int old = cheats_playground_get();
    while (!__atomic_compare_exchange_n(&Cheats.Playground, &old, (old & ~mask) | (value & mask),
                                        false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

unsigned int cheats_superslide_speed(void) {
    unsigned int speed = __atomic_load_n(&Cheats.SuperSlideSpeed, __ATOMIC_RELAXED);
    return speed >= 30 && speed <= 300 ? speed : 90;
}

void cheats_set_superslide_speed(unsigned int speed) {
    if (speed < 30) speed = 30;
    if (speed > 300) speed = 300;
    __atomic_store_n(&Cheats.SuperSlideSpeed, speed, __ATOMIC_RELAXED);
}
