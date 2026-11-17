#ifndef IOS_HAT_H
#define IOS_HAT_H

#include <stdbool.h>

#ifndef IOS_HAT_BOUNCE_RADIUS
#define IOS_HAT_BOUNCE_RADIUS 104.0f
#endif
#ifndef IOS_HAT_BOUNCE_IMPULSE
#define IOS_HAT_BOUNCE_IMPULSE 54.0f
#endif

enum IOSHatPhase {
    IOS_HAT_ATTACHED,
    IOS_HAT_THROWN,
    IOS_HAT_RETURNING,
};

struct IOSHat {
    enum IOSHatPhase phase;
    float pos[3];
    float previousPos[3];
    float directionX, directionZ;
    float launchSpeed;
    unsigned ticks;
    unsigned decelTicks;
    bool bouncedThisAirTime;
};

typedef bool (*IOSHatBlocked)(const float from[3], const float to[3]);

void ios_hat_reset(struct IOSHat *hat);
bool ios_hat_launch(struct IOSHat *hat, const float pos[3], float x, float z,
                    float forwardSpeed);
void ios_hat_step(struct IOSHat *hat, const float target[3], IOSHatBlocked blocked);
// startTime and endTime are fractions of the hat's current simulation tick.
bool ios_hat_bounce(struct IOSHat *hat, const float from[3], const float to[3],
                    float startTime, float endTime, float *velocityY, IOSHatBlocked blocked);

#endif
