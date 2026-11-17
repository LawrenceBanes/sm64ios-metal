#ifndef SM64_GAME_HYRULE_GATE_H
#define SM64_GAME_HYRULE_GATE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct HyruleGateProximityState {
    bool inside;
} HyruleGateProximityState;

typedef enum HyruleGateProximityEvent {
    HYRULE_GATE_NO_EVENT = 0,
    HYRULE_GATE_ENTERED,
    HYRULE_GATE_LEFT,
} HyruleGateProximityEvent;

typedef enum HyruleGateResponse {
    HYRULE_GATE_RESPONSE_NONE = 0,
    HYRULE_GATE_RESPONSE_DESTINATION_UNAVAILABLE,
    HYRULE_GATE_RESPONSE_REQUEST_TRANSITION,
} HyruleGateResponse;

static inline bool hyrule_gate_contains(float x, float y, float z,
                                        float centerX, float centerY, float centerZ,
                                        float horizontalRadius, float verticalTolerance) {
    const float dx = x - centerX;
    const float dy = y - centerY;
    const float dz = z - centerZ;
    return dx * dx + dz * dz <= horizontalRadius * horizontalRadius
        && dy >= -verticalTolerance && dy <= verticalTolerance;
}

static inline HyruleGateProximityEvent hyrule_gate_update_proximity(
        HyruleGateProximityState *state, bool inside) {
    if (state == NULL) return HYRULE_GATE_NO_EVENT;
    if (inside == state->inside) return HYRULE_GATE_NO_EVENT;
    state->inside = inside;
    return inside ? HYRULE_GATE_ENTERED : HYRULE_GATE_LEFT;
}

static inline bool hyrule_gate_destination_valid(bool destinationRegistered,
                                                  int level, int area, int node) {
    return destinationRegistered && level > 0 && area > 0 && area <= 255
        && node >= 0 && node <= 255;
}

static inline HyruleGateResponse hyrule_gate_response(bool entered,
                                                       bool destinationRegistered,
                                                       int level, int area, int node) {
    if (!entered) return HYRULE_GATE_RESPONSE_NONE;
    return hyrule_gate_destination_valid(destinationRegistered, level, area, node)
        ? HYRULE_GATE_RESPONSE_REQUEST_TRANSITION
        : HYRULE_GATE_RESPONSE_DESTINATION_UNAVAILABLE;
}

#endif
