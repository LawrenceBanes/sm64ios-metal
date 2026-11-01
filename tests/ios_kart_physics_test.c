#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/game/ios_kart.h"
#include "../src/pc/controller/controller_touch_axis.h"

static void tick(IOSKartState *state, IOSKartInput input, IOSKartEvents *events, int count) {
    int i;
    for (i = 0; i < count; ++i) ios_kart_update(state, &input, events);
}

int main(void) {
    IOSKartState state;
    IOSKartEvents events;
    IOSKartInput input = {0};
    f32 beforeYaw, beforeSteering, vx, vz, speedBeforeBrake;
    IOSKartInput mapped = {0};

    /* This is the exact UIKit screen-delta -> signed N64 axis conversion used
     * by controller_touchscreen.m. Positive screen X is right; screen Y down
     * is inverted so pushing up produces the standard positive N64 Y axis. */
    assert(touchscreen_axis_from_delta(0, 128, FALSE) == 0);
    assert(touchscreen_axis_from_delta(64, 128, FALSE) == 127);
    assert(touchscreen_axis_from_delta(-64, 128, FALSE) == -128);
    assert(touchscreen_axis_from_delta(-64, 128, TRUE) == 127);
    assert(touchscreen_axis_from_delta(64, 128, TRUE) == -128);
    assert(touchscreen_axis_from_delta(10, 0, FALSE) == 0);

    /* Compose the real screen-axis mapping with the same controller adapter
     * used by the kart: an up/right touch means forward travel and a right turn. */
    ios_kart_reset(&state, 0.0f);
    ios_kart_map_controller_input(&input,
                                 touchscreen_axis_from_delta(64, 128, FALSE) / 64.0f,
                                 touchscreen_axis_from_delta(-64, 128, TRUE) / 64.0f,
                                 0, 0, 0, 0);
    input.grounded = 1;
    input.traction = 1.0f;
    tick(&state, input, &events, 35);
    assert(state.forwardSpeed > 20.0f && state.headingRadians > 0.0f);
    ios_kart_velocity(&state, &vx, &vz);
    assert(vx > 0.0f && vz > 0.0f);

    ios_kart_map_controller_input(&mapped, 1.0f, 1.0f, 0, 0, 0, 0);
    assert(mapped.steer == 1.0f && mapped.throttle == 1.0f && mapped.reverse == 0.0f);
    ios_kart_map_controller_input(&mapped, -1.0f, -1.0f, 0, 0, 0, 0);
    assert(mapped.steer == -1.0f && mapped.throttle == 0.0f && mapped.reverse == 1.0f);
    ios_kart_map_controller_input(&mapped, 0.0f, -1.0f, 1, 1, 1, 1);
    assert(mapped.throttle == 1.0f && mapped.reverse == 0.0f && mapped.brake == 1.0f);
    assert(mapped.driftHeld && mapped.hopPressed);

    /* SM64 convention: heading 0 faces +Z; increasing yaw turns toward +X.
     * A right touch therefore turns right and produces positive-X travel. */
    ios_kart_reset(&state, 0.0f);
    input = (IOSKartInput){ .throttle = 1.0f, .steer = 1.0f, .traction = 1.0f, .grounded = 1 };
    tick(&state, input, &events, 35);
    assert(state.forwardSpeed > 20.0f);
    assert(state.headingRadians > 0.0f);
    ios_kart_velocity(&state, &vx, &vz);
    assert(vx > 0.0f && vz > 0.0f);

    ios_kart_reset(&state, 0.0f);
    input.steer = -1.0f;
    tick(&state, input, &events, 35);
    assert(state.headingRadians < 0.0f);
    ios_kart_velocity(&state, &vx, &vz);
    assert(vx < 0.0f && vz > 0.0f);

    /* No pivot-spin at a standstill. Steering eases in and back to center. */
    ios_kart_reset(&state, 0.0f);
    input = (IOSKartInput){ .steer = 1.0f, .traction = 1.0f, .grounded = 1 };
    tick(&state, input, &events, 10);
    assert(fabsf(state.headingRadians) < 0.001f);
    input.throttle = 1.0f;
    tick(&state, input, &events, 40);
    input.steer = 0.0f;
    beforeSteering = state.steeringAngleRadians;
    tick(&state, input, &events, 30);
    assert(fabsf(state.yawRateRadians) < 0.01f);
    assert(fabsf(state.steeringAngleRadians) < beforeSteering);

    /* Braking stops forward motion without silently selecting reverse. */
    ios_kart_reset(&state, 0.0f);
    input = (IOSKartInput){ .throttle = 1.0f, .traction = 1.0f, .grounded = 1 };
    tick(&state, input, &events, 45);
    speedBeforeBrake = state.forwardSpeed;
    input.throttle = 0.0f;
    input.brake = 1.0f;
    tick(&state, input, &events, 4);
    assert(state.forwardSpeed > 0.0f && state.forwardSpeed < speedBeforeBrake);
    tick(&state, input, &events, 40);
    assert(fabsf(state.forwardSpeed) < 0.001f);
    input.brake = 0.0f;
    input.reverse = 1.0f;
    tick(&state, input, &events, 30);
    assert(state.forwardSpeed < 0.0f && state.forwardSpeed >= -12.0f);
    input.reverse = 0.0f;
    input.throttle = 1.0f;
    tick(&state, input, &events, 10);
    assert(state.forwardSpeed <= 0.0f); /* throttle first cancels reverse */

    /* A positive-Z uphill plane has a negative-Z normal component, so gravity
     * decelerates travel toward +Z and accelerates travel toward -Z. */
    assert(ios_kart_slope_acceleration(0.0f, 0.93f, -0.36f, 0.0f) < 0.0f);
    assert(ios_kart_slope_acceleration(0.0f, 0.93f, 0.36f, 0.0f) > 0.0f);
    assert(fabsf(ios_kart_slope_acceleration(0.0f, 1.0f, -0.3f, 1.5707963f)) < 0.001f);

    assert(ios_kart_can_mount(100.0f, 130.0f, 1, 0, 0));
    assert(ios_kart_can_mount(130.0f, 130.0f, 1, 0, 0));
    assert(!ios_kart_can_mount(131.0f, 130.0f, 1, 0, 0));
    assert(!ios_kart_can_mount(100.0f, 130.0f, 0, 0, 0));
    assert(!ios_kart_can_mount(100.0f, 130.0f, 1, 1, 0));
    assert(!ios_kart_can_mount(100.0f, 130.0f, 1, 0, 1));
    assert(ios_kart_can_dismount(1, 1));
    assert(!ios_kart_can_dismount(0, 1));
    assert(!ios_kart_can_dismount(1, 0));

    input.grounded = TRUE;
    input.traction = 1.0f;
    ios_kart_reset(&state, 0.0f);
    input = (IOSKartInput){ .grounded = TRUE, .traction = 1.0f };
    input.throttle = 1.0f;
    tick(&state, input, &events, 55);
    assert(state.forwardSpeed > 35.0f && state.forwardSpeed <= 44.0f);
    tick(&state, input, &events, 100);
    assert(state.forwardSpeed == 44.0f);

    input.throttle = 0.0f;
    tick(&state, input, &events, 20);
    assert(state.forwardSpeed > 0.0f && state.forwardSpeed < 44.0f);

    input.brake = 1.0f;
    tick(&state, input, &events, 35);
    assert(fabsf(state.forwardSpeed) < 0.001f);
    input.brake = 0.0f;
    input.reverse = 1.0f;
    tick(&state, input, &events, 35);
    assert(state.forwardSpeed < 0.0f && state.forwardSpeed >= -12.0f);

    ios_kart_reset(&state, 0.0f);
    input = (IOSKartInput){ .throttle = 1.0f, .traction = 1.0f, .grounded = 1 };
    tick(&state, input, &events, 30);
    input.throttle = 0.0f;
    input.steer = 1.0f;
    beforeYaw = state.headingRadians;
    tick(&state, input, &events, 30);
    assert(state.headingRadians > beforeYaw);

    ios_kart_reset(&state, 0.0f);
    input = (IOSKartInput){ .steer = 0.8f, .throttle = 1.0f, .traction = 1.0f, .grounded = 1 };
    tick(&state, input, &events, 25);
    input.throttle = 0.0f;
    input.driftHeld = 1;
    input.hopPressed = 1;
    ios_kart_update(&state, &input, &events);
    assert(events.hopRequested && events.driftStarted && state.drifting);
    input.hopPressed = 0;
    tick(&state, input, &events, 24);
    assert(state.boostReady && state.driftChargeFrames >= 24);
    input.driftHeld = 0;
    ios_kart_update(&state, &input, &events);
    assert(events.boostActivated && state.boostFrames > 0 && !state.drifting);
    tick(&state, input, &events, 100);
    assert(state.forwardSpeed <= 56.0f);

    ios_kart_reset(&state, 0.0f);
    input = (IOSKartInput){ .traction = 1.0f, .slopeAcceleration = 0.6f, .grounded = 1 };
    ios_kart_update(&state, &input, &events);
    assert(state.forwardSpeed > 0.5f);
    ios_kart_reset(&state, 0.0f);
    input.traction = 0.4f;
    ios_kart_update(&state, &input, &events);
    assert(state.forwardSpeed > 0.0f && state.forwardSpeed < 0.3f);

    ios_kart_reset(&state, 0.0f);
    input = (IOSKartInput){ .throttle = 1.0f, .steer = 1.0f, .traction = 1.0f, .grounded = 1 };
    ios_kart_update(&state, &input, &events);
    beforeYaw = state.headingRadians;
    input.grounded = 0;
    ios_kart_update(&state, &input, &events);
    assert(state.headingRadians > beforeYaw);
    assert(!events.hopRequested);

    /* Reversing changes travel direction, not the sign of the steering wheel. */
    ios_kart_reset(&state, 0.0f);
    state.forwardSpeed = -6.0f;
    input = (IOSKartInput){ .steer = 1.0f, .reverse = 1.0f, .traction = 1.0f, .grounded = 1 };
    ios_kart_update(&state, &input, &events);
    assert(state.headingRadians > 0.0f);

    /* Drift charge needs a real, same-direction slide at speed. Its lateral
     * velocity points outward from the intended turn, not into the corner. */
    ios_kart_reset(&state, 0.0f);
    state.forwardSpeed = 24.0f;
    input = (IOSKartInput){ .throttle = 1.0f, .steer = 0.8f, .traction = 1.0f,
                            .grounded = 1, .driftHeld = 1 };
    ios_kart_update(&state, &input, &events);
    assert(events.driftStarted && state.driftDirection == 1);
    assert(state.lateralSpeed < 0.0f);
    input.steer = -0.8f;
    tick(&state, input, &events, 5);
    assert(state.driftChargeFrames == 1); /* only the entry tick had matching input */

    state.forwardSpeed = 20.0f;
    state.lateralSpeed = 4.0f;
    ios_kart_wall_collision(&state, 0.0f, -1.0f);
    ios_kart_velocity(&state, &vx, &vz);
    assert(fabsf(vz) < 0.01f);

    ios_kart_reset(&state, 1.0f);
    assert(state.forwardSpeed == 0.0f && state.lateralSpeed == 0.0f);
    assert(fabsf(state.headingRadians - 1.0f) < 0.001f);

    puts("iOS kart dynamics tests passed");
    return 0;
}
