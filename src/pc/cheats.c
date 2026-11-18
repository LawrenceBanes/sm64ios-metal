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
