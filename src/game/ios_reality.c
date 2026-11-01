#include <string.h>

#include "game/ios_reality.h"

#define CHEAT_BIT(id) (1u << (id))

static const f32 sGravityValues[] = { -2.0f, -1.0f, -0.5f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
static const f32 sSizeValues[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f };
static const f32 sFrictionValues[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
static const f32 sSpeedValues[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
static const f32 sJumpValues[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };

static s32 reality_enable_noop(IOSRealityRuntime *runtime, const IOSRealityCheatDefinition *definition) {
    (void)runtime;
    (void)definition;
    return TRUE;
}

static void reality_cleanup_noop(IOSRealityRuntime *runtime, const IOSRealityCheatDefinition *definition) {
    (void)runtime;
    (void)definition;
}

static void reality_apply_policy(const IOSRealityRuntime *runtime,
                                 const IOSRealityCheatDefinition *definition,
                                 IOSRealityPolicy *policy) {
    f32 value = ios_reality_parameter_value(runtime, (IOSRealityCheatId)definition->id);
    switch ((IOSRealityCheatId)definition->id) {
        case IOS_REALITY_PERSONAL_GRAVITY: policy->gravity_multiplier = value; break;
        case IOS_REALITY_ZERO_GRAVITY: policy->zero_gravity = TRUE; break;
        case IOS_REALITY_SWIM_ANYWHERE: policy->swim_anywhere = TRUE; break;
        case IOS_REALITY_TIME_STOP: policy->time_stop = TRUE; break;
        case IOS_REALITY_MARIO_SIZE: policy->mario_scale = value; break;
        case IOS_REALITY_FRICTION: policy->friction_multiplier = value; break;
        case IOS_REALITY_FREEZE_ENEMIES: policy->freeze_enemies = TRUE; break;
        case IOS_REALITY_SPEED: policy->movement_multiplier = value; break;
        case IOS_REALITY_JUMP: policy->jump_multiplier = value; break;
        default: break;
    }
}

#define SUPPORTED(id, cat, count, def, values, label, conflict) \
    { id, cat, IOS_REALITY_DEF_SUPPORTED, count, 0, conflict, def, values, label, NULL, \
      reality_enable_noop, reality_cleanup_noop, reality_apply_policy }
#define ACTION(id, label) \
    { id, IOS_REALITY_CATEGORY_ACTION, IOS_REALITY_DEF_SUPPORTED | IOS_REALITY_DEF_ACTION, \
      0, 0, 0, 0, NULL, label, NULL, reality_enable_noop, reality_cleanup_noop, NULL }
#define UNAVAILABLE(id, cat, label, why) \
    { id, cat, 0, 0, 0, 0, 0, NULL, label, why, reality_enable_noop, reality_cleanup_noop, NULL }

static const IOSRealityCheatDefinition sDefinitions[IOS_REALITY_CHEAT_COUNT] = {
    [IOS_REALITY_PERSONAL_GRAVITY] = SUPPORTED(IOS_REALITY_PERSONAL_GRAVITY, IOS_REALITY_CATEGORY_PHYSICS,
        9, 6, sGravityValues, "Personal Gravity", CHEAT_BIT(IOS_REALITY_ZERO_GRAVITY)),
    [IOS_REALITY_WORLD_ROTATION] = UNAVAILABLE(IOS_REALITY_WORLD_ROTATION, IOS_REALITY_CATEGORY_WORLD,
        "World Rotation", "Requires a shared transform for rendering, collision, camera, and input"),
    [IOS_REALITY_RECURSIVE_MARIO] = UNAVAILABLE(IOS_REALITY_RECURSIVE_MARIO, IOS_REALITY_CATEGORY_EXPERIMENTAL,
        "Recursive Mario", "Requires an isolated recursive scene render and bounded clone simulation"),
    [IOS_REALITY_POSSESSION] = UNAVAILABLE(IOS_REALITY_POSSESSION, IOS_REALITY_CATEGORY_EXPERIMENTAL,
        "Possession Mode", "Requires a safe player and enemy AI control handoff"),
    [IOS_REALITY_NON_EUCLIDEAN_CASTLE] = UNAVAILABLE(IOS_REALITY_NON_EUCLIDEAN_CASTLE, IOS_REALITY_CATEGORY_EXPERIMENTAL,
        "Non-Euclidean Castle", "Requires paired collision spaces and linked-world rendering"),
    [IOS_REALITY_SWIM_ANYWHERE] = SUPPORTED(IOS_REALITY_SWIM_ANYWHERE, IOS_REALITY_CATEGORY_PHYSICS,
        0, 0, NULL, "Swim Anywhere", 0),
    [IOS_REALITY_ZERO_GRAVITY] = SUPPORTED(IOS_REALITY_ZERO_GRAVITY, IOS_REALITY_CATEGORY_PHYSICS,
        0, 0, NULL, "Zero Gravity", CHEAT_BIT(IOS_REALITY_PERSONAL_GRAVITY)),
    [IOS_REALITY_TIME_STOP] = SUPPORTED(IOS_REALITY_TIME_STOP, IOS_REALITY_CATEGORY_WORLD,
        0, 0, NULL, "Time Stop", 0),
    [IOS_REALITY_SPAWN_GOOMBA] = ACTION(IOS_REALITY_SPAWN_GOOMBA, "Spawn Goomba"),
    [IOS_REALITY_MARIO_SIZE] = SUPPORTED(IOS_REALITY_MARIO_SIZE, IOS_REALITY_CATEGORY_PLAYER,
        5, 2, sSizeValues, "Mario Size (visual)", 0),
    [IOS_REALITY_FRICTION] = SUPPORTED(IOS_REALITY_FRICTION, IOS_REALITY_CATEGORY_PHYSICS,
        7, 3, sFrictionValues, "Friction", 0),
    [IOS_REALITY_FREEZE_ENEMIES] = SUPPORTED(IOS_REALITY_FREEZE_ENEMIES, IOS_REALITY_CATEGORY_WORLD,
        0, 0, NULL, "Freeze Enemies", 0),
    [IOS_REALITY_SPEED] = SUPPORTED(IOS_REALITY_SPEED, IOS_REALITY_CATEGORY_PHYSICS,
        6, 2, sSpeedValues, "Movement Speed", 0),
    [IOS_REALITY_JUMP] = SUPPORTED(IOS_REALITY_JUMP, IOS_REALITY_CATEGORY_PHYSICS,
        6, 2, sJumpValues, "Jump Height", 0),
    [IOS_REALITY_HOVER] = UNAVAILABLE(IOS_REALITY_HOVER, IOS_REALITY_CATEGORY_PHYSICS,
        "Hover Mode", "Needs a dedicated vertical input without stealing jump or action controls"),
    [IOS_REALITY_CLEAR_SPAWNED_OBJECTS] = ACTION(IOS_REALITY_CLEAR_SPAWNED_OBJECTS, "Clear Spawned Actors"),
    [IOS_REALITY_SPAWN_BOBOMB] = ACTION(IOS_REALITY_SPAWN_BOBOMB, "Spawn Bob-omb"),
};

static IOSRealityRuntime sGlobalRuntime;
static volatile s32 sGlobalInitialized;
static volatile u32 sToggleRequests[IOS_REALITY_CHEAT_COUNT];
static volatile u32 sParameterRequests[IOS_REALITY_CHEAT_COUNT];
static volatile u32 sActionRequests[IOS_REALITY_CHEAT_COUNT];
static volatile u32 sResetRequest;
static volatile u32 sPublishedMask;
static volatile u8 sPublishedParameters[IOS_REALITY_CHEAT_COUNT];
static volatile s32 sLastResult;
static volatile s32 sLastResultId;

const IOSRealityCheatDefinition *ios_reality_definition(IOSRealityCheatId id) {
    if ((u32)id >= IOS_REALITY_CHEAT_COUNT) return NULL;
    return &sDefinitions[id];
}

u32 ios_reality_supported_mask(void) {
    u32 mask = 0;
    u32 i;
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++) {
        if (sDefinitions[i].flags & IOS_REALITY_DEF_SUPPORTED) mask |= CHEAT_BIT(i);
    }
    return mask;
}

void ios_reality_runtime_init(IOSRealityRuntime *runtime) {
    u32 i;
    if (runtime == NULL) return;
    memset(runtime, 0, sizeof(*runtime));
    runtime->policy.gravity_multiplier = 1.0f;
    runtime->policy.friction_multiplier = 1.0f;
    runtime->policy.movement_multiplier = 1.0f;
    runtime->policy.jump_multiplier = 1.0f;
    runtime->policy.mario_scale = 1.0f;
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++)
        runtime->parameter_index[i] = sDefinitions[i].default_parameter;
}

void ios_reality_runtime_reset(IOSRealityRuntime *runtime) {
    u32 i;
    if (runtime == NULL) return;
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++) {
        if ((runtime->enabled_mask & CHEAT_BIT(i)) && sDefinitions[i].on_cleanup)
            sDefinitions[i].on_cleanup(runtime, &sDefinitions[i]);
    }
    ios_reality_runtime_init(runtime);
}

s32 ios_reality_is_enabled(const IOSRealityRuntime *runtime, IOSRealityCheatId id) {
    return runtime != NULL && (u32)id < IOS_REALITY_CHEAT_COUNT
        && (runtime->enabled_mask & CHEAT_BIT(id)) != 0;
}

f32 ios_reality_parameter_value(const IOSRealityRuntime *runtime, IOSRealityCheatId id) {
    const IOSRealityCheatDefinition *definition = ios_reality_definition(id);
    u8 index;
    if (runtime == NULL || definition == NULL || definition->parameter_count == 0
        || definition->parameter_values == NULL) return 1.0f;
    index = runtime->parameter_index[id];
    if (index >= definition->parameter_count) index = definition->default_parameter;
    return definition->parameter_values[index];
}

IOSRealityResult ios_reality_set_enabled(IOSRealityRuntime *runtime, IOSRealityCheatId id, s32 enabled) {
    const IOSRealityCheatDefinition *definition = ios_reality_definition(id);
    u32 bit;
    if (runtime == NULL || definition == NULL) return IOS_REALITY_RESULT_UNAVAILABLE;
    bit = CHEAT_BIT(id);
    if (definition->flags & IOS_REALITY_DEF_ACTION) return IOS_REALITY_RESULT_ACTION;
    if (!(definition->flags & IOS_REALITY_DEF_SUPPORTED)) {
        runtime->rejected_mask |= bit;
        return IOS_REALITY_RESULT_UNAVAILABLE;
    }
    if (!enabled) {
        if (runtime->enabled_mask & bit) {
            if (definition->on_cleanup) definition->on_cleanup(runtime, definition);
            runtime->enabled_mask &= ~bit;
            runtime->rejected_mask &= ~bit;
            ios_reality_update_policy(runtime);
        }
        return IOS_REALITY_RESULT_OK;
    }
    if (runtime->enabled_mask & bit) return IOS_REALITY_RESULT_OK;
    if ((definition->dependencies & runtime->enabled_mask) != definition->dependencies) {
        runtime->rejected_mask |= bit;
        return IOS_REALITY_RESULT_DEPENDENCY;
    }
    if (definition->conflicts & runtime->enabled_mask) {
        runtime->rejected_mask |= bit;
        return IOS_REALITY_RESULT_CONFLICT;
    }
    if (definition->on_enable && !definition->on_enable(runtime, definition)) {
        runtime->rejected_mask |= bit;
        return IOS_REALITY_RESULT_DEPENDENCY;
    }
    runtime->enabled_mask |= bit;
    runtime->rejected_mask &= ~bit;
    ios_reality_update_policy(runtime);
    return IOS_REALITY_RESULT_OK;
}

s32 ios_reality_cycle_parameter(IOSRealityRuntime *runtime, IOSRealityCheatId id) {
    const IOSRealityCheatDefinition *definition = ios_reality_definition(id);
    if (runtime == NULL || definition == NULL || definition->parameter_count < 2) return FALSE;
    runtime->parameter_index[id] = (runtime->parameter_index[id] + 1) % definition->parameter_count;
    ios_reality_update_policy(runtime);
    return TRUE;
}

void ios_reality_update_policy(IOSRealityRuntime *runtime) {
    u32 i;
    if (runtime == NULL) return;
    runtime->policy.gravity_multiplier = 1.0f;
    runtime->policy.friction_multiplier = 1.0f;
    runtime->policy.movement_multiplier = 1.0f;
    runtime->policy.jump_multiplier = 1.0f;
    runtime->policy.mario_scale = 1.0f;
    runtime->policy.zero_gravity = FALSE;
    runtime->policy.swim_anywhere = FALSE;
    runtime->policy.time_stop = FALSE;
    runtime->policy.freeze_enemies = FALSE;
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++) {
        if ((runtime->enabled_mask & CHEAT_BIT(i)) && sDefinitions[i].on_update)
            sDefinitions[i].on_update(runtime, &sDefinitions[i], &runtime->policy);
    }
    if (runtime->policy.zero_gravity) runtime->policy.gravity_multiplier = 0.0f;
}

const IOSRealityPolicy *ios_reality_get_policy(const IOSRealityRuntime *runtime) {
    return runtime ? &runtime->policy : NULL;
}

void ios_reality_global_init(void) {
    u32 i;
    if (__atomic_load_n(&sGlobalInitialized, __ATOMIC_ACQUIRE)) return;
    ios_reality_runtime_init(&sGlobalRuntime);
    __atomic_store_n(&sPublishedMask, 0, __ATOMIC_RELEASE);
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++)
        __atomic_store_n(&sPublishedParameters[i], sGlobalRuntime.parameter_index[i], __ATOMIC_RELEASE);
    __atomic_store_n(&sGlobalInitialized, TRUE, __ATOMIC_RELEASE);
}

const IOSRealityPolicy *ios_reality_global_policy(void) {
    ios_reality_global_init();
    return &sGlobalRuntime.policy;
}

s32 ios_reality_global_is_enabled(IOSRealityCheatId id) {
    if ((u32)id >= IOS_REALITY_CHEAT_COUNT) return FALSE;
    return (__atomic_load_n(&sPublishedMask, __ATOMIC_ACQUIRE) & CHEAT_BIT(id)) != 0;
}

u8 ios_reality_global_parameter_index(IOSRealityCheatId id) {
    if ((u32)id >= IOS_REALITY_CHEAT_COUNT) return 0;
    if (!__atomic_load_n(&sGlobalInitialized, __ATOMIC_ACQUIRE)) return sDefinitions[id].default_parameter;
    return __atomic_load_n(&sPublishedParameters[id], __ATOMIC_ACQUIRE);
}

IOSRealityResult ios_reality_global_last_result(IOSRealityCheatId *id) {
    if (id) *id = (IOSRealityCheatId)__atomic_load_n(&sLastResultId, __ATOMIC_ACQUIRE);
    return (IOSRealityResult)__atomic_load_n(&sLastResult, __ATOMIC_ACQUIRE);
}

static void reality_record_result(IOSRealityCheatId id, IOSRealityResult result) {
    __atomic_store_n(&sLastResultId, id, __ATOMIC_RELEASE);
    __atomic_store_n(&sLastResult, result, __ATOMIC_RELEASE);
}

void ios_reality_request_toggle(IOSRealityCheatId id) {
    if ((u32)id < IOS_REALITY_CHEAT_COUNT && (sDefinitions[id].flags & IOS_REALITY_DEF_SUPPORTED)
        && !(sDefinitions[id].flags & IOS_REALITY_DEF_ACTION))
        __atomic_fetch_add(&sToggleRequests[id], 1, __ATOMIC_RELAXED);
}

void ios_reality_request_parameter_cycle(IOSRealityCheatId id) {
    if ((u32)id < IOS_REALITY_CHEAT_COUNT && sDefinitions[id].parameter_count > 1)
        __atomic_fetch_add(&sParameterRequests[id], 1, __ATOMIC_RELAXED);
}

void ios_reality_request_action(IOSRealityCheatId id) {
    if ((u32)id < IOS_REALITY_CHEAT_COUNT
        && (sDefinitions[id].flags & (IOS_REALITY_DEF_SUPPORTED | IOS_REALITY_DEF_ACTION))
            == (IOS_REALITY_DEF_SUPPORTED | IOS_REALITY_DEF_ACTION))
        __atomic_fetch_add(&sActionRequests[id], 1, __ATOMIC_RELAXED);
}

void ios_reality_request_reset(void) {
    __atomic_store_n(&sResetRequest, 1, __ATOMIC_RELEASE);
}

void ios_reality_process_requests(void) {
    u32 i, count;
    ios_reality_global_init();
    if (__atomic_exchange_n(&sResetRequest, 0, __ATOMIC_ACQUIRE)) {
        ios_reality_runtime_reset(&sGlobalRuntime);
        for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++) {
            __atomic_store_n(&sToggleRequests[i], 0, __ATOMIC_RELEASE);
            __atomic_store_n(&sParameterRequests[i], 0, __ATOMIC_RELEASE);
        }
        __atomic_fetch_add(&sActionRequests[IOS_REALITY_CLEAR_SPAWNED_OBJECTS], 1, __ATOMIC_RELEASE);
        reality_record_result(IOS_REALITY_PERSONAL_GRAVITY, IOS_REALITY_RESULT_OK);
    }
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++) {
        count = __atomic_exchange_n(&sParameterRequests[i], 0, __ATOMIC_ACQUIRE);
        while (count--) ios_reality_cycle_parameter(&sGlobalRuntime, (IOSRealityCheatId)i);
        count = __atomic_exchange_n(&sToggleRequests[i], 0, __ATOMIC_ACQUIRE);
        if (count & 1u) {
            IOSRealityResult result = ios_reality_set_enabled(&sGlobalRuntime, (IOSRealityCheatId)i,
                !ios_reality_is_enabled(&sGlobalRuntime, (IOSRealityCheatId)i));
            reality_record_result((IOSRealityCheatId)i, result);
        }
    }
    __atomic_store_n(&sPublishedMask, sGlobalRuntime.enabled_mask, __ATOMIC_RELEASE);
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++)
        __atomic_store_n(&sPublishedParameters[i], sGlobalRuntime.parameter_index[i], __ATOMIC_RELEASE);
}

u32 ios_reality_take_action_requests(IOSRealityCheatId id) {
    if ((u32)id >= IOS_REALITY_CHEAT_COUNT) return 0;
    return __atomic_exchange_n(&sActionRequests[id], 0, __ATOMIC_ACQUIRE);
}

u32 ios_reality_serialize(const IOSRealityRuntime *runtime, u8 *output, u32 capacity) {
    u32 i;
    if (runtime == NULL || output == NULL || capacity < IOS_REALITY_SAVE_BYTES) return 0;
    output[0] = 'R'; output[1] = 'D'; output[2] = 'B'; output[3] = '1';
    output[4] = 1;
    output[5] = (u8)runtime->enabled_mask;
    output[6] = (u8)(runtime->enabled_mask >> 8);
    output[7] = (u8)(runtime->enabled_mask >> 16);
    output[8] = (u8)(runtime->enabled_mask >> 24);
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++) output[9 + i] = runtime->parameter_index[i];
    return IOS_REALITY_SAVE_BYTES;
}

s32 ios_reality_deserialize(IOSRealityRuntime *runtime, const u8 *input, u32 length) {
    IOSRealityRuntime candidate;
    u32 mask, i;
    if (runtime == NULL || input == NULL || length != IOS_REALITY_SAVE_BYTES
        || input[0] != 'R' || input[1] != 'D' || input[2] != 'B' || input[3] != '1' || input[4] != 1)
        return FALSE;
    mask = (u32)input[5] | ((u32)input[6] << 8) | ((u32)input[7] << 16) | ((u32)input[8] << 24);
    if (mask & ~ios_reality_supported_mask()) return FALSE;
    ios_reality_runtime_init(&candidate);
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++) {
        if (input[9 + i] >= (sDefinitions[i].parameter_count ? sDefinitions[i].parameter_count : 1)) return FALSE;
        candidate.parameter_index[i] = input[9 + i];
    }
    for (i = 0; i < IOS_REALITY_CHEAT_COUNT; i++) {
        if ((mask & CHEAT_BIT(i)) && ios_reality_set_enabled(&candidate, (IOSRealityCheatId)i, TRUE) != IOS_REALITY_RESULT_OK)
            return FALSE;
    }
    *runtime = candidate;
    return TRUE;
}
