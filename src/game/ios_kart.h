#ifndef IOS_KART_H
#define IOS_KART_H

#include <PR/ultratypes.h>

/*
 * Renderer- and SM64-state-independent kart dynamics. One call advances one
 * simulation tick; the game adapter owns collision, camera, and actor state.
 */
typedef struct IOSKartState {
    f32 headingRadians;
    f32 yawRateRadians;
    f32 forwardSpeed;
    f32 lateralSpeed;
    s16 driftChargeFrames;
    s16 boostFrames;
    u8 drifting;
    u8 boostReady;
} IOSKartState;

typedef struct IOSKartInput {
    f32 throttle;          /* [0, 1] */
    f32 brake;             /* [0, 1] */
    f32 steer;             /* [-1, 1] */
    f32 traction;          /* [0.35, 1], derived from the current floor */
    f32 slopeAcceleration; /* projected gravity along the vehicle heading */
    u8 grounded;
    u8 driftHeld;
    u8 hopPressed;
} IOSKartInput;

typedef struct IOSKartEvents {
    u8 hopRequested;
    u8 driftStarted;
    u8 boostReady;
    u8 boostActivated;
} IOSKartEvents;

void ios_kart_reset(IOSKartState *state, f32 headingRadians);
/* Small pure lifecycle predicates shared with the SM64 gameplay adapter. */
s32 ios_kart_can_mount(f32 distance, f32 maximumDistance, s32 grounded,
                       s32 alreadyRiding, s32 kartOccupied);
s32 ios_kart_can_dismount(s32 grounded, s32 exitSpaceClear);
void ios_kart_update(IOSKartState *state, const IOSKartInput *input, IOSKartEvents *events);
void ios_kart_velocity(const IOSKartState *state, f32 *velocityX, f32 *velocityZ);
void ios_kart_wall_collision(IOSKartState *state, f32 normalX, f32 normalZ);

#endif
