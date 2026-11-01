#include "ios_kart.h"

#include <math.h>
#include <string.h>

#define IOS_KART_PI 3.14159265358979323846f
#define IOS_KART_FORWARD_MAX 52.0f
#define IOS_KART_REVERSE_MAX 15.0f
#define IOS_KART_BOOST_MAX 65.0f
#define IOS_KART_DRIFT_MIN_SPEED 7.0f
#define IOS_KART_DRIFT_CHARGE 24
#define IOS_KART_BOOST_DURATION 42

static f32 ios_kart_clamp(f32 value, f32 minimum, f32 maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static f32 ios_kart_approach(f32 value, f32 target, f32 amount) {
    if (value < target) return fminf(value + amount, target);
    if (value > target) return fmaxf(value - amount, target);
    return value;
}

void ios_kart_reset(IOSKartState *state, f32 headingRadians) {
    memset(state, 0, sizeof(*state));
    state->headingRadians = remainderf(headingRadians, 2.0f * IOS_KART_PI);
}

s32 ios_kart_can_mount(f32 distance, f32 maximumDistance, s32 grounded,
                       s32 alreadyRiding, s32 kartOccupied) {
    return grounded && !alreadyRiding && !kartOccupied && distance >= 0.0f
        && distance <= maximumDistance;
}

s32 ios_kart_can_dismount(s32 grounded, s32 exitSpaceClear) {
    return grounded && exitSpaceClear;
}

void ios_kart_update(IOSKartState *state, const IOSKartInput *input, IOSKartEvents *events) {
    f32 speedMagnitude;
    f32 speedLimit;
    f32 traction;
    f32 turnScale;
    f32 turnTarget;
    f32 lateralTarget;
    f32 speedSign;
    u8 wasBoostReady;

    memset(events, 0, sizeof(*events));
    wasBoostReady = state->boostReady;
    traction = ios_kart_clamp(input->traction, 0.35f, 1.0f);
    speedMagnitude = fabsf(state->forwardSpeed);

    if (state->boostFrames > 0) state->boostFrames--;

    if (input->driftHeld && input->grounded && speedMagnitude >= IOS_KART_DRIFT_MIN_SPEED
        && fabsf(input->steer) >= 0.15f && !state->drifting) {
        state->drifting = TRUE;
        state->driftChargeFrames = 0;
        events->driftStarted = TRUE;
    }

    if (state->drifting && input->driftHeld && fabsf(input->steer) >= 0.15f) {
        if (state->driftChargeFrames < 90) state->driftChargeFrames++;
    }

    state->boostReady = state->drifting && state->driftChargeFrames >= IOS_KART_DRIFT_CHARGE;
    events->boostReady = state->boostReady && !wasBoostReady;
    if (state->drifting && !input->driftHeld) {
        if (state->driftChargeFrames >= IOS_KART_DRIFT_CHARGE
            && speedMagnitude >= IOS_KART_DRIFT_MIN_SPEED) {
            state->boostFrames = IOS_KART_BOOST_DURATION;
            state->forwardSpeed += 3.0f;
            events->boostActivated = TRUE;
        }
        state->drifting = FALSE;
        state->driftChargeFrames = 0;
        state->boostReady = FALSE;
    }

    events->hopRequested = input->grounded && input->hopPressed;

    if (input->brake > 0.01f) {
        if (state->forwardSpeed > 0.0f) {
            state->forwardSpeed = fmaxf(0.0f, state->forwardSpeed - 2.2f * input->brake);
        } else {
            state->forwardSpeed = fmaxf(-IOS_KART_REVERSE_MAX,
                                        state->forwardSpeed - 0.55f * input->brake);
        }
    } else if (input->throttle > 0.01f) {
        if (state->forwardSpeed < 0.0f) {
            state->forwardSpeed = fminf(0.0f, state->forwardSpeed + 1.5f * input->throttle);
        } else {
            speedLimit = state->boostFrames > 0 ? IOS_KART_BOOST_MAX : IOS_KART_FORWARD_MAX;
            state->forwardSpeed += (1.5f - 0.75f * speedMagnitude / speedLimit) * input->throttle;
            state->forwardSpeed = fminf(speedLimit, state->forwardSpeed);
        }
    } else {
        f32 drag = input->grounded ? 0.10f + speedMagnitude * 0.012f : 0.06f;
        state->forwardSpeed = ios_kart_approach(state->forwardSpeed, 0.0f, drag);
    }

    state->forwardSpeed += input->slopeAcceleration * traction;
    state->forwardSpeed = ios_kart_clamp(state->forwardSpeed, -IOS_KART_REVERSE_MAX,
                                         state->boostFrames > 0 ? IOS_KART_BOOST_MAX : IOS_KART_FORWARD_MAX);

    speedMagnitude = fabsf(state->forwardSpeed);
    speedSign = state->forwardSpeed < 0.0f ? -1.0f : 1.0f;
    turnScale = 0.025f + 0.055f * fminf(speedMagnitude / IOS_KART_FORWARD_MAX, 1.0f);
    if (!input->grounded) turnScale *= 0.42f;
    if (state->drifting) turnScale *= 1.18f;
    turnTarget = input->steer * turnScale * speedSign;
    state->yawRateRadians += (turnTarget - state->yawRateRadians) * 0.35f;
    state->headingRadians = remainderf(state->headingRadians + state->yawRateRadians,
                                       2.0f * IOS_KART_PI);

    if (state->drifting) {
        lateralTarget = input->steer * speedMagnitude * 0.30f * traction;
        state->lateralSpeed += (lateralTarget - state->lateralSpeed) * 0.18f;
    } else {
        state->lateralSpeed += (0.0f - state->lateralSpeed) * (input->grounded ? 0.36f : 0.10f);
    }
}

void ios_kart_velocity(const IOSKartState *state, f32 *velocityX, f32 *velocityZ) {
    f32 sinHeading = sinf(state->headingRadians);
    f32 cosHeading = cosf(state->headingRadians);
    *velocityX = state->forwardSpeed * sinHeading + state->lateralSpeed * cosHeading;
    *velocityZ = state->forwardSpeed * cosHeading - state->lateralSpeed * sinHeading;
}

void ios_kart_wall_collision(IOSKartState *state, f32 normalX, f32 normalZ) {
    f32 length = sqrtf(normalX * normalX + normalZ * normalZ);
    f32 velocityX, velocityZ, intoWall;
    f32 sinHeading, cosHeading;
    if (length < 0.001f) return;

    normalX /= length;
    normalZ /= length;
    ios_kart_velocity(state, &velocityX, &velocityZ);
    intoWall = velocityX * normalX + velocityZ * normalZ;
    if (intoWall < 0.0f) {
        velocityX -= intoWall * normalX;
        velocityZ -= intoWall * normalZ;
        sinHeading = sinf(state->headingRadians);
        cosHeading = cosf(state->headingRadians);
        state->forwardSpeed = velocityX * sinHeading + velocityZ * cosHeading;
        state->lateralSpeed = velocityX * cosHeading - velocityZ * sinHeading;
    }
}
