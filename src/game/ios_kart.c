#include "ios_kart.h"

#include <math.h>
#include <string.h>

#define IOS_KART_PI 3.14159265358979323846f
#define IOS_KART_FORWARD_MAX 44.0f
#define IOS_KART_REVERSE_MAX 12.0f
#define IOS_KART_BOOST_MAX 56.0f
#define IOS_KART_DRIFT_MIN_SPEED 8.0f
#define IOS_KART_DRIFT_CHARGE 24
#define IOS_KART_BOOST_DURATION 32

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

void ios_kart_map_controller_input(IOSKartInput *input, f32 stickX, f32 stickY,
                                   s32 accelerate, s32 brake, s32 drift, s32 hopPressed) {
    stickX = ios_kart_clamp(stickX, -1.0f, 1.0f);
    stickY = ios_kart_clamp(stickY, -1.0f, 1.0f);
    input->steer = stickX; /* positive right remains positive yaw in SM64's +Z convention */
    input->throttle = accelerate ? 1.0f : fmaxf(stickY, 0.0f);
    input->reverse = (!accelerate) ? fmaxf(-stickY, 0.0f) : 0.0f;
    input->brake = brake ? 1.0f : 0.0f;
    input->driftHeld = drift != 0;
    input->hopPressed = hopPressed != 0;
}

f32 ios_kart_slope_acceleration(f32 normalX, f32 normalY, f32 normalZ, f32 headingRadians) {
    f32 normalYSafe = fmaxf(normalY, 0.25f);
    f32 uphillNormalDot = normalX * sinf(headingRadians) + normalZ * cosf(headingRadians);
    /* For a plane rising along +Z, its normal has negative Z; gravity therefore
     * accelerates toward -Z. The normal dot heading has the correct downhill sign. */
    return ios_kart_clamp(0.38f * uphillNormalDot / normalYSafe, -0.9f, 0.9f);
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
    f32 driftSign;
    f32 steerMagnitude;
    f32 speedFraction;
    u8 wasBoostReady;

    memset(events, 0, sizeof(*events));
    wasBoostReady = state->boostReady;
    traction = ios_kart_clamp(input->traction, 0.35f, 1.0f);
    speedMagnitude = fabsf(state->forwardSpeed);

    if (state->boostFrames > 0) state->boostFrames--;

    if (input->driftHeld && input->grounded && state->forwardSpeed >= IOS_KART_DRIFT_MIN_SPEED
        && fabsf(input->steer) >= 0.32f && !state->drifting) {
        state->drifting = TRUE;
        state->driftChargeFrames = 0;
        state->driftDirection = input->steer < 0.0f ? -1 : 1;
        events->driftStarted = TRUE;
    }

    if (state->drifting && input->grounded && input->driftHeld
        && input->steer * state->driftDirection >= 0.32f
        && state->forwardSpeed >= IOS_KART_DRIFT_MIN_SPEED) {
        if (state->driftChargeFrames < 90) state->driftChargeFrames++;
    }

    state->boostReady = state->drifting && state->driftChargeFrames >= IOS_KART_DRIFT_CHARGE;
    events->boostReady = state->boostReady && !wasBoostReady;
    if (state->drifting && !input->driftHeld) {
        if (state->driftChargeFrames >= IOS_KART_DRIFT_CHARGE
            && state->forwardSpeed >= IOS_KART_DRIFT_MIN_SPEED) {
            state->boostFrames = IOS_KART_BOOST_DURATION;
            state->forwardSpeed += 4.0f;
            events->boostActivated = TRUE;
        }
        state->drifting = FALSE;
        state->driftChargeFrames = 0;
        state->boostReady = FALSE;
        state->driftDirection = 0;
    }

    events->hopRequested = input->grounded && input->hopPressed;

    if (input->brake > 0.01f) {
        state->forwardSpeed = ios_kart_approach(state->forwardSpeed, 0.0f, 2.0f * input->brake);
    } else if (input->reverse > 0.01f) {
        if (state->forwardSpeed > 0.0f) {
            state->forwardSpeed = ios_kart_approach(state->forwardSpeed, 0.0f, 1.8f * input->reverse);
        } else {
            state->forwardSpeed = ios_kart_approach(state->forwardSpeed, -IOS_KART_REVERSE_MAX,
                                                    0.62f * input->reverse);
        }
    } else if (input->throttle > 0.01f) {
        if (state->forwardSpeed < 0.0f) {
            state->forwardSpeed = ios_kart_approach(state->forwardSpeed, 0.0f, 1.2f * input->throttle);
        } else {
            speedLimit = state->boostFrames > 0 ? IOS_KART_BOOST_MAX : IOS_KART_FORWARD_MAX;
            speedFraction = fminf(speedMagnitude / speedLimit, 1.0f);
            state->forwardSpeed += (0.92f - 0.48f * speedFraction) * input->throttle;
            state->forwardSpeed = fminf(speedLimit, state->forwardSpeed);
        }
    } else {
        f32 drag = input->grounded ? 0.12f + speedMagnitude * 0.014f : 0.06f;
        state->forwardSpeed = ios_kart_approach(state->forwardSpeed, 0.0f, drag);
    }

    if (input->brake <= 0.01f) state->forwardSpeed += input->slopeAcceleration * traction;
    state->forwardSpeed = ios_kart_clamp(state->forwardSpeed, -IOS_KART_REVERSE_MAX,
                                         state->boostFrames > 0 ? IOS_KART_BOOST_MAX : IOS_KART_FORWARD_MAX);

    speedMagnitude = fabsf(state->forwardSpeed);
    speedFraction = fminf(speedMagnitude / 28.0f, 1.0f);
    turnScale = speedMagnitude > 1.5f ? 0.008f + 0.038f * speedFraction : 0.0f;
    if (!input->grounded) turnScale *= 0.5f;
    if (state->drifting) turnScale *= 1.08f;
    turnTarget = input->steer * turnScale;
    state->yawRateRadians += (turnTarget - state->yawRateRadians) * 0.24f;
    state->headingRadians = remainderf(state->headingRadians + state->yawRateRadians,
                                       2.0f * IOS_KART_PI);
    state->steeringAngleRadians += (input->steer * 0.48f - state->steeringAngleRadians) * 0.22f;

    if (state->drifting) {
        driftSign = (f32)state->driftDirection;
        steerMagnitude = fmaxf(0.0f, input->steer * driftSign);
        lateralTarget = -driftSign * speedMagnitude * (0.12f + 0.20f * steerMagnitude) * traction;
        state->lateralSpeed += (lateralTarget - state->lateralSpeed) * 0.14f;
        state->forwardSpeed = ios_kart_approach(state->forwardSpeed, 0.0f,
                                                0.10f * steerMagnitude * traction);
    } else {
        state->lateralSpeed += (0.0f - state->lateralSpeed) * (input->grounded ? 0.24f : 0.10f);
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
