#include <assert.h>
#include <string.h>

#include "game/ios_reality.h"

int main(void) {
    IOSRealityRuntime runtime, restored;
    const IOSRealityPolicy *policy;
    u8 preset[IOS_REALITY_SAVE_BYTES];
    u8 invalid[IOS_REALITY_SAVE_BYTES];
    IOSRealityCheatId lastId = IOS_REALITY_PERSONAL_GRAVITY;

    assert(IOS_REALITY_PERSONAL_GRAVITY == 0);
    assert(IOS_REALITY_NON_EUCLIDEAN_CASTLE == 4);
    assert(IOS_REALITY_SPAWN_BOBOMB == IOS_REALITY_CHEAT_COUNT - 1);
    ios_reality_runtime_init(&runtime);
    policy = ios_reality_get_policy(&runtime);
    assert(policy->gravity_multiplier == 1.0f);
    assert(policy->friction_multiplier == 1.0f);
    assert(policy->movement_multiplier == 1.0f);
    assert(policy->jump_multiplier == 1.0f);
    assert(policy->mario_scale == 1.0f);

    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_PERSONAL_GRAVITY, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_cycle_parameter(&runtime, IOS_REALITY_PERSONAL_GRAVITY));
    assert(ios_reality_get_policy(&runtime)->gravity_multiplier == 1.5f);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_ZERO_GRAVITY, TRUE) == IOS_REALITY_RESULT_CONFLICT);
    assert(!ios_reality_is_enabled(&runtime, IOS_REALITY_ZERO_GRAVITY));
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_PERSONAL_GRAVITY, FALSE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_ZERO_GRAVITY, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_get_policy(&runtime)->zero_gravity);
    assert(ios_reality_get_policy(&runtime)->gravity_multiplier == 0.0f);

    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_SWIM_ANYWHERE, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_TIME_STOP, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_FREEZE_ENEMIES, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_get_policy(&runtime)->swim_anywhere);
    assert(ios_reality_get_policy(&runtime)->time_stop);
    assert(ios_reality_get_policy(&runtime)->freeze_enemies);

    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_SPEED, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_cycle_parameter(&runtime, IOS_REALITY_SPEED));
    assert(ios_reality_get_policy(&runtime)->movement_multiplier == 1.25f);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_JUMP, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_cycle_parameter(&runtime, IOS_REALITY_JUMP));
    assert(ios_reality_get_policy(&runtime)->jump_multiplier == 1.25f);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_MARIO_SIZE, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_cycle_parameter(&runtime, IOS_REALITY_MARIO_SIZE));
    assert(ios_reality_get_policy(&runtime)->mario_scale == 1.25f);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_FRICTION, TRUE) == IOS_REALITY_RESULT_OK);
    assert(ios_reality_cycle_parameter(&runtime, IOS_REALITY_FRICTION));
    assert(ios_reality_get_policy(&runtime)->friction_multiplier == 1.25f);

    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_WORLD_ROTATION, TRUE) == IOS_REALITY_RESULT_UNAVAILABLE);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_HOVER, TRUE) == IOS_REALITY_RESULT_UNAVAILABLE);
    assert(ios_reality_set_enabled(&runtime, IOS_REALITY_SPAWN_GOOMBA, TRUE) == IOS_REALITY_RESULT_ACTION);
    assert(ios_reality_definition(IOS_REALITY_WORLD_ROTATION)->unavailable_reason != NULL);
    assert((ios_reality_supported_mask() & (1u << IOS_REALITY_SPAWN_GOOMBA)) != 0);

    assert(ios_reality_serialize(&runtime, preset, sizeof(preset)) == IOS_REALITY_SAVE_BYTES);
    ios_reality_runtime_init(&restored);
    assert(ios_reality_deserialize(&restored, preset, sizeof(preset)));
    assert(restored.enabled_mask == runtime.enabled_mask);
    assert(memcmp(restored.parameter_index, runtime.parameter_index, IOS_REALITY_CHEAT_COUNT) == 0);
    assert(ios_reality_get_policy(&restored)->movement_multiplier == 1.25f);
    memcpy(invalid, preset, sizeof(invalid));
    invalid[0] = 'X';
    assert(!ios_reality_deserialize(&restored, invalid, sizeof(invalid)));
    memcpy(invalid, preset, sizeof(invalid));
    invalid[5] |= 0x10; /* unsupported World Rotation bit */
    assert(!ios_reality_deserialize(&restored, invalid, sizeof(invalid)));

    ios_reality_global_init();
    ios_reality_request_toggle(IOS_REALITY_PERSONAL_GRAVITY);
    ios_reality_process_requests();
    assert(ios_reality_global_is_enabled(IOS_REALITY_PERSONAL_GRAVITY));
    ios_reality_request_toggle(IOS_REALITY_ZERO_GRAVITY);
    ios_reality_process_requests();
    assert(!ios_reality_global_is_enabled(IOS_REALITY_ZERO_GRAVITY));
    assert(ios_reality_global_last_result(&lastId) == IOS_REALITY_RESULT_CONFLICT);
    assert(lastId == IOS_REALITY_ZERO_GRAVITY);
    ios_reality_request_action(IOS_REALITY_SPAWN_GOOMBA);
    ios_reality_request_action(IOS_REALITY_SPAWN_GOOMBA);
    assert(ios_reality_take_action_requests(IOS_REALITY_SPAWN_GOOMBA) == 2);
    ios_reality_request_reset();
    ios_reality_process_requests();
    assert(!ios_reality_global_is_enabled(IOS_REALITY_PERSONAL_GRAVITY));
    assert(ios_reality_take_action_requests(IOS_REALITY_CLEAR_SPAWNED_OBJECTS) == 1);

    ios_reality_runtime_reset(&runtime);
    assert(runtime.enabled_mask == 0);
    assert(ios_reality_get_policy(&runtime)->gravity_multiplier == 1.0f);
    assert(ios_reality_global_last_result(&lastId) == IOS_REALITY_RESULT_OK);
    return 0;
}
