#include <assert.h>
#include <stdio.h>

#include "game/hyrule_gate.h"

int main(void) {
    HyruleGateProximityState state = { false };

    assert(!hyrule_gate_contains(0, 0, 701, 0, 0, 0, 700, 500));
    assert(hyrule_gate_contains(700, 0, 0, 0, 0, 0, 700, 500));
    assert(!hyrule_gate_contains(0, 501, 0, 0, 0, 0, 700, 500));

    assert(hyrule_gate_update_proximity(&state, false) == HYRULE_GATE_NO_EVENT);
    assert(hyrule_gate_update_proximity(&state, true) == HYRULE_GATE_ENTERED);
    assert(hyrule_gate_update_proximity(&state, true) == HYRULE_GATE_NO_EVENT);
    assert(hyrule_gate_update_proximity(&state, false) == HYRULE_GATE_LEFT);
    assert(hyrule_gate_update_proximity(&state, true) == HYRULE_GATE_ENTERED);

    /* Kokiri is planned but is not registered as a runtime SM64 level. */
    assert(!hyrule_gate_destination_valid(false, 0, 0, 0));
    assert(hyrule_gate_response(true, false, 0, 0, 0)
           == HYRULE_GATE_RESPONSE_DESTINATION_UNAVAILABLE);
    assert(hyrule_gate_response(false, true, 2, 1, 9) == HYRULE_GATE_RESPONSE_NONE);
    assert(hyrule_gate_response(true, true, 2, 1, 9)
           == HYRULE_GATE_RESPONSE_REQUEST_TRANSITION);
    assert(!hyrule_gate_destination_valid(true, 2, 0, 9));
    assert(!hyrule_gate_destination_valid(true, 2, 1, 256));

    puts("Hyrule gate proximity and destination safety checks passed");
    return 0;
}
