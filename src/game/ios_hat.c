#include "ios_hat.h"

#ifdef IOS_HAT_MOVEMENT

#include <math.h>
#include <string.h>

#define OUTBOUND_SPEED 36.0f
#define BASE_DECEL_TICKS 15
#define MAX_RUN_SPEED 32.0f
#define THROWN_TICKS 40
#define ARM_TICKS 4
#define RETURN_SPEED 72.0f
#define RETURN_TICKS 30
#define HAT_TOP 20.0f

void ios_hat_reset(struct IOSHat *hat) {
    *hat = (struct IOSHat) { 0 };
}

bool ios_hat_launch(struct IOSHat *hat, const float pos[3], float x, float z,
                    float forwardSpeed) {
    const float directionLength = sqrtf(x * x + z * z);
    if (hat->phase != IOS_HAT_ATTACHED || !isfinite(directionLength) || directionLength < 0.01f
        || !isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2])) {
        return false;
    }

    memcpy(hat->pos, pos, sizeof(hat->pos));
    hat->directionX = x / directionLength;
    hat->directionZ = z / directionLength;
    // A running jump covers more ground before the feet come back down.
    const float lead = fminf(fmaxf(forwardSpeed, 0.0f), MAX_RUN_SPEED);
    hat->launchSpeed = OUTBOUND_SPEED + 0.85f * lead;
    hat->decelTicks = BASE_DECEL_TICKS + (unsigned)(0.25f * lead);
    hat->ticks = 0;
    hat->phase = IOS_HAT_THROWN;
    return true;
}

static void return_hat(struct IOSHat *hat) {
    hat->phase = IOS_HAT_RETURNING;
    hat->ticks = 0;
}

static void recover_hat(struct IOSHat *hat) {
    hat->phase = IOS_HAT_ATTACHED;
    hat->ticks = 0;
}

void ios_hat_step(struct IOSHat *hat, const float target[3], IOSHatBlocked blocked) {
    if (hat->phase == IOS_HAT_THROWN) {
        if (hat->ticks < hat->decelTicks) {
            const float speed = hat->launchSpeed * (1.0f - hat->ticks / (float)hat->decelTicks);
            const float next[3] = { hat->pos[0] + speed * hat->directionX, hat->pos[1],
                                    hat->pos[2] + speed * hat->directionZ };
            if (blocked != NULL && blocked(hat->pos, next)) {
                return_hat(hat);
                return;
            }
            memcpy(hat->pos, next, sizeof(hat->pos));
        }
        if (++hat->ticks >= THROWN_TICKS) {
            return_hat(hat);
        }
    } else if (hat->phase == IOS_HAT_RETURNING) {
        const float dx = target[0] - hat->pos[0];
        const float dy = target[1] - hat->pos[1];
        const float dz = target[2] - hat->pos[2];
        const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (++hat->ticks >= RETURN_TICKS || !isfinite(distance) || distance <= RETURN_SPEED) {
            recover_hat(hat);
            return;
        }

        const float fraction = RETURN_SPEED / distance;
        const float next[3] = { hat->pos[0] + dx * fraction, hat->pos[1] + dy * fraction,
                                hat->pos[2] + dz * fraction };
        if (blocked != NULL && blocked(hat->pos, next)) {
            recover_hat(hat);
            return;
        }
        memcpy(hat->pos, next, sizeof(hat->pos));
    }
}

bool ios_hat_bounce(struct IOSHat *hat, const float from[3], const float to[3],
                    float *velocityY, IOSHatBlocked blocked) {
    const float top = hat->pos[1] + HAT_TOP;
    if (hat->phase != IOS_HAT_THROWN || hat->ticks < ARM_TICKS || hat->bouncedThisAirTime
        || !(*velocityY < 0.0f) || !(from[1] >= top && to[1] <= top && from[1] > to[1])) {
        return false;
    }

    // Test the horizontal position at the instant the feet cross the cap.
    const float fraction = (from[1] - top) / (from[1] - to[1]);
    const float contact[3] = { from[0] + (to[0] - from[0]) * fraction, top,
                               from[2] + (to[2] - from[2]) * fraction };
    const float x = contact[0] - hat->pos[0];
    const float z = contact[2] - hat->pos[2];
    if (!(x * x + z * z <= IOS_HAT_BOUNCE_RADIUS * IOS_HAT_BOUNCE_RADIUS)
        || (blocked != NULL && blocked(contact, hat->pos))) {
        return false;
    }

    *velocityY = IOS_HAT_BOUNCE_IMPULSE;
    hat->bouncedThisAirTime = true;
    return_hat(hat);
    return true;
}

#endif
