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
    f32 steeringAngleRadians;
    s16 driftChargeFrames;
    s16 boostFrames;
    s8 driftDirection;
    u8 drifting;
    u8 boostReady;
} IOSKartState;

typedef struct IOSKartInput {
    f32 throttle;          /* [0, 1] */
    f32 brake;             /* service brake [0, 1], never requests reverse */
    f32 reverse;           /* reverse request [0, 1], first brakes forward motion */
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
/* N64 axes: +X is right and +Y is up. Heading 0 is +Z; +yaw turns toward +X. */
void ios_kart_map_controller_input(IOSKartInput *input, f32 stickX, f32 stickY,
                                   s32 accelerate, s32 brake, s32 drift, s32 hopPressed);
f32 ios_kart_slope_acceleration(f32 normalX, f32 normalY, f32 normalZ, f32 headingRadians);
/* Small pure lifecycle predicates shared with the SM64 gameplay adapter. */
s32 ios_kart_can_mount(f32 distance, f32 maximumDistance, s32 grounded,
                       s32 alreadyRiding, s32 kartOccupied);
s32 ios_kart_can_dismount(s32 grounded, s32 exitSpaceClear);
void ios_kart_update(IOSKartState *state, const IOSKartInput *input, IOSKartEvents *events);
void ios_kart_velocity(const IOSKartState *state, f32 *velocityX, f32 *velocityZ);
void ios_kart_wall_collision(IOSKartState *state, f32 normalX, f32 normalZ);

#endif
