#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/game/ios_kart.h"

static void tick(IOSKartState *state, IOSKartInput input, IOSKartEvents *events, int count) {
    int i;
    for (i = 0; i < count; ++i) ios_kart_update(state, &input, events);
}

int main(void) {
    IOSKartState state;
    IOSKartEvents events;
    IOSKartInput input = {0};
    f32 beforeYaw, vx, vz;

    assert(ios_kart_can_mount(100.0f, 210.0f, 1, 0, 0));
    assert(!ios_kart_can_mount(211.0f, 210.0f, 1, 0, 0));
    assert(!ios_kart_can_mount(100.0f, 210.0f, 0, 0, 0));
    assert(!ios_kart_can_mount(100.0f, 210.0f, 1, 1, 0));
    assert(!ios_kart_can_mount(100.0f, 210.0f, 1, 0, 1));
    assert(ios_kart_can_dismount(1, 1));
    assert(!ios_kart_can_dismount(0, 1));
    assert(!ios_kart_can_dismount(1, 0));

    input.grounded = TRUE;
    input.traction = 1.0f;
    ios_kart_reset(&state, 0.0f);
    input.throttle = 1.0f;
    tick(&state, input, &events, 55);
    assert(state.forwardSpeed > 35.0f && state.forwardSpeed <= 52.0f);
    tick(&state, input, &events, 100);
    assert(state.forwardSpeed == 52.0f);

    input.throttle = 0.0f;
    tick(&state, input, &events, 20);
    assert(state.forwardSpeed > 0.0f && state.forwardSpeed < 52.0f);

    input.brake = 1.0f;
    tick(&state, input, &events, 35);
    assert(state.forwardSpeed <= 0.0f && state.forwardSpeed >= -15.0f);
    tick(&state, input, &events, 35);
    assert(state.forwardSpeed < 0.0f && state.forwardSpeed >= -15.0f);

    ios_kart_reset(&state, 0.0f);
    input.brake = 0.0f;
    input.throttle = 1.0f;
    tick(&state, input, &events, 30);
    input.throttle = 0.0f;
    input.steer = 1.0f;
    beforeYaw = state.headingRadians;
    tick(&state, input, &events, 30);
    assert(state.headingRadians > beforeYaw);

    ios_kart_reset(&state, 0.0f);
    input.steer = 0.8f;
    input.throttle = 1.0f;
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
    assert(state.forwardSpeed <= 65.0f);

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
