#ifndef IOS_REALITY_H
#define IOS_REALITY_H

#include <PR/ultratypes.h>

#define IOS_REALITY_CHEAT_COUNT 17
#define IOS_REALITY_SAVE_BYTES (9 + IOS_REALITY_CHEAT_COUNT)

/* IDs are part of the serialized preset format. Append new IDs; never renumber. */
typedef enum IOSRealityCheatId {
    IOS_REALITY_PERSONAL_GRAVITY = 0,
    IOS_REALITY_WORLD_ROTATION = 1,
    IOS_REALITY_RECURSIVE_MARIO = 2,
    IOS_REALITY_POSSESSION = 3,
    IOS_REALITY_NON_EUCLIDEAN_CASTLE = 4,
    IOS_REALITY_SWIM_ANYWHERE = 5,
    IOS_REALITY_ZERO_GRAVITY = 6,
    IOS_REALITY_TIME_STOP = 7,
    IOS_REALITY_SPAWN_GOOMBA = 8,
    IOS_REALITY_MARIO_SIZE = 9,
    IOS_REALITY_FRICTION = 10,
    IOS_REALITY_FREEZE_ENEMIES = 11,
    IOS_REALITY_SPEED = 12,
    IOS_REALITY_JUMP = 13,
    IOS_REALITY_HOVER = 14,
    IOS_REALITY_CLEAR_SPAWNED_OBJECTS = 15,
    IOS_REALITY_SPAWN_BOBOMB = 16,
} IOSRealityCheatId;

typedef enum IOSRealityCategory {
    IOS_REALITY_CATEGORY_PHYSICS,
    IOS_REALITY_CATEGORY_WORLD,
    IOS_REALITY_CATEGORY_PLAYER,
    IOS_REALITY_CATEGORY_EXPERIMENTAL,
    IOS_REALITY_CATEGORY_ACTION,
} IOSRealityCategory;

typedef enum IOSRealityDefinitionFlags {
    IOS_REALITY_DEF_SUPPORTED = 1 << 0,
    IOS_REALITY_DEF_ACTION = 1 << 1,
} IOSRealityDefinitionFlags;

typedef enum IOSRealityResult {
    IOS_REALITY_RESULT_OK,
    IOS_REALITY_RESULT_UNAVAILABLE,
    IOS_REALITY_RESULT_CONFLICT,
    IOS_REALITY_RESULT_DEPENDENCY,
    IOS_REALITY_RESULT_ACTION,
} IOSRealityResult;

typedef struct IOSRealityPolicy {
    f32 gravity_multiplier;
    f32 friction_multiplier;
    f32 movement_multiplier;
    f32 jump_multiplier;
    f32 mario_scale;
    u8 zero_gravity;
    u8 swim_anywhere;
    u8 time_stop;
    u8 freeze_enemies;
} IOSRealityPolicy;

struct IOSRealityRuntime;
struct IOSRealityCheatDefinition;
typedef s32 (*IOSRealityEnableCallback)(struct IOSRealityRuntime *, const struct IOSRealityCheatDefinition *);
typedef void (*IOSRealityCleanupCallback)(struct IOSRealityRuntime *, const struct IOSRealityCheatDefinition *);
typedef void (*IOSRealityUpdateCallback)(const struct IOSRealityRuntime *, const struct IOSRealityCheatDefinition *, IOSRealityPolicy *);

typedef struct IOSRealityCheatDefinition {
    u8 id;
    u8 category;
    u8 flags;
    u8 parameter_count;
    u32 dependencies;
    u32 conflicts;
    u8 default_parameter;
    const f32 *parameter_values;
    const char *name;
    const char *unavailable_reason;
    IOSRealityEnableCallback on_enable;
    IOSRealityCleanupCallback on_cleanup;
    IOSRealityUpdateCallback on_update;
} IOSRealityCheatDefinition;

typedef struct IOSRealityRuntime {
    u32 enabled_mask;
    u32 rejected_mask;
    u8 parameter_index[IOS_REALITY_CHEAT_COUNT];
    IOSRealityPolicy policy;
} IOSRealityRuntime;

void ios_reality_runtime_init(IOSRealityRuntime *runtime);
void ios_reality_runtime_reset(IOSRealityRuntime *runtime);
IOSRealityResult ios_reality_set_enabled(IOSRealityRuntime *runtime, IOSRealityCheatId id, s32 enabled);
s32 ios_reality_cycle_parameter(IOSRealityRuntime *runtime, IOSRealityCheatId id);
void ios_reality_update_policy(IOSRealityRuntime *runtime);
s32 ios_reality_is_enabled(const IOSRealityRuntime *runtime, IOSRealityCheatId id);
f32 ios_reality_parameter_value(const IOSRealityRuntime *runtime, IOSRealityCheatId id);
const IOSRealityCheatDefinition *ios_reality_definition(IOSRealityCheatId id);
const IOSRealityPolicy *ios_reality_get_policy(const IOSRealityRuntime *runtime);
u32 ios_reality_supported_mask(void);

/* The UIKit producer queues requests; only the game thread consumes them. */
void ios_reality_global_init(void);
const IOSRealityPolicy *ios_reality_global_policy(void);
s32 ios_reality_global_is_enabled(IOSRealityCheatId id);
u8 ios_reality_global_parameter_index(IOSRealityCheatId id);
IOSRealityResult ios_reality_global_last_result(IOSRealityCheatId *id);
void ios_reality_request_toggle(IOSRealityCheatId id);
void ios_reality_request_parameter_cycle(IOSRealityCheatId id);
void ios_reality_request_action(IOSRealityCheatId id);
void ios_reality_request_reset(void);
void ios_reality_process_requests(void);
u32 ios_reality_take_action_requests(IOSRealityCheatId id);

/* Versioned, pointer-free format for a preset; it contains no game assets. */
u32 ios_reality_serialize(const IOSRealityRuntime *runtime, u8 *output, u32 capacity);
s32 ios_reality_deserialize(IOSRealityRuntime *runtime, const u8 *input, u32 length);

#endif /* IOS_REALITY_H */
