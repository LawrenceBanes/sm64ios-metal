#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/world_region.h"
#include "engine/surface_load.h"
#include "engine/surface_collision.h"
#include "game/object_list_processor.h"
#include "surface_terrains.h"

/* Actual collision query code reads these same partition heads. */
SpatialPartitionCell gStaticSurfacePartition[SURFACE_PARTITION_DIM][SURFACE_PARTITION_DIM];
SpatialPartitionCell gDynamicSurfacePartition[SURFACE_PARTITION_DIM][SURFACE_PARTITION_DIM];
struct NumTimesCalled gNumCalls;
struct Object *gCurrentObject;
struct Object *gMarioObject;
s16 gCheckingSurfaceCollisionsForCamera;
s16 gFindFloorIncludeSurfaceIntangible;
s32 gNumFindFloorMisses;

s32 mario_ios_portal_surface_passable(const struct Surface *surface, f32 x, f32 y, f32 z,
                                       f32 radius) {
    (void)surface; (void)x; (void)y; (void)z; (void)radius;
    return 0;
}

static const struct WorldRegionVertex westVertices[] = {
    {-2000, 0, -500}, {0, 0, -500}, {-2000, 0, 500}, {0, 0, 500}
};
static const struct WorldRegionVertex eastVertices[] = {
    {0, 0, -500}, {2000, 0, -500}, {0, 0, 500}, {2000, 0, 500}
};
static const struct WorldRegionTriangle floorTriangles[] = {
    {0, 2, 1, SURFACE_DEFAULT, 0}, {1, 2, 3, SURFACE_DEFAULT, 0}
};

static struct WorldRegionDesc region(u32 id, const struct WorldRegionVertex *vertices) {
    struct WorldRegionDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.id = id;
    desc.vertices = vertices;
    desc.vertexCount = 4;
    desc.triangles = floorTriangles;
    desc.triangleCount = 2;
    desc.renderList = vertices;
    return desc;
}

static u32 cell_count(s32 x, s32 z, u8 category) {
    struct SurfaceNode *node = gStaticSurfacePartition[
        surface_partition_cell_index(z)][surface_partition_cell_index(x)][category].next;
    u32 count = 0;
    while (node != NULL) {
        count++;
        assert(count <= 8);
        node = node->next;
    }
    return count;
}

static void test_coordinate_limits(void) {
    s16 local;
    s64 world;
    assert(world_coord_to_s16(LLONG_MAX, LLONG_MAX - 32767, &local) && local == 32767);
    assert(world_coord_to_s16(LLONG_MIN, LLONG_MIN + 32768, &local) && local == -32768);
    assert(!world_coord_to_s16(LLONG_MAX, LLONG_MIN, &local));
    assert(!world_coord_to_s16(LLONG_MIN, LLONG_MAX, &local));
    assert(world_coord_add(LLONG_MAX - 5, 5, &world) && world == LLONG_MAX);
    assert(!world_coord_add(LLONG_MAX - 5, 6, &world));
    assert(!world_coord_add(LLONG_MIN + 5, -6, &world));
    assert(surface_partition_float_in_bounds(-32768.0f));
    assert(!surface_partition_float_in_bounds(32768.0f));
    assert(!surface_partition_float_in_bounds(65536.0f));
    assert(!surface_partition_float_in_bounds(NAN));
}

static void test_seam_and_lifecycle(void) {
    struct WorldPoint zero = {0, 0, 0};
    struct WorldPoint player;
    struct WorldRegionDesc west = region(1, westVertices);
    struct WorldRegionDesc east = region(2, eastVertices);
    struct WorldRegionInfo info;
    struct Surface *firstFloor = NULL;
    u32 iteration;
    world_region_reset(zero);
    west.neighbors[0] = 2;
    west.neighborCount = 1;
    assert(world_region_register(&west) == WORLD_REGION_OK);
    assert(world_region_register(&east) == WORLD_REGION_OK);
    assert(world_region_register(&east) == WORLD_REGION_DUPLICATE);
    assert(world_region_count() == 2);
    assert(world_region_info(1, &info) && info.triangleCount == 2 && info.nodeCount > 0);
    assert(info.neighborCount == 1 && info.neighbors[0] == 2);
    for (iteration = 0; iteration < 100; iteration++) {
        s32 x;
        assert(world_region_activate(1) == WORLD_REGION_OK);
        assert(world_region_activate(2) == WORLD_REGION_OK);
        assert(world_region_info(1, &info));
        assert(world_region_render_near_legacy(&info, -1000, 1000, 0, 5000));
        assert(!world_region_render_near_legacy(&info, 15000, 1000, 0, 5000));
        for (x = -1900; x <= 1900; x += 100) {
            struct Surface *floor = NULL;
            assert(cell_count(x, 0, SPATIAL_PARTITION_FLOORS) > 0);
            assert(find_floor(x, 100, 0, &floor) == 0.0f);
            assert(floor != NULL && floor->normal.y > 0.99f);
            if (x == -1900 && iteration == 0) firstFloor = floor;
            if (x == -1900) assert(floor == firstFloor);
        }
        {
            struct Surface *wrappedFloor = NULL;
            assert(find_floor(65536.0f, 100, 0, &wrappedFloor) == -11000.0f);
            assert(wrappedFloor == NULL);
            assert(find_floor(-65536.0f, 100, 0, &wrappedFloor) == -11000.0f);
            assert(wrappedFloor == NULL);
        }
        assert(world_region_deactivate(1) == WORLD_REGION_OK);
        assert(cell_count(-1500, 0, SPATIAL_PARTITION_FLOORS) == 0);
        assert(cell_count(1500, 0, SPATIAL_PARTITION_FLOORS) > 0);
        assert(world_region_deactivate(2) == WORLD_REGION_OK);
        assert(cell_count(1500, 0, SPATIAL_PARTITION_FLOORS) == 0);
    }
    player.x = -1800; player.y = 0; player.z = 0;
    assert(world_region_update(player, 0, 0) == WORLD_REGION_OK);
    assert(world_region_info(1, &info) && info.active);
    assert(world_region_info(2, &info) && info.active);
    world_region_update_legacy(-1800, 0, 0, -3000, 0, -3000, 256);
    assert(world_region_info(2, &info) && info.active);
    player.x = 20000;
    assert(world_region_update(player, 100, 256) == WORLD_REGION_OK);
    assert(world_region_info(1, &info) && !info.active);
    assert(world_region_info(2, &info) && !info.active);
    world_region_reset(zero);
    assert(world_region_count() == 0);
}

static void test_rejection_is_atomic(void) {
    struct WorldPoint zero = {0, 0, 0};
    struct WorldRegionDesc bad = region(3, westVertices);
    struct WorldRegionTriangle invalid[] = {
        {0, 2, 1, SURFACE_DEFAULT, 0}, {1, 2, 99, SURFACE_DEFAULT, 0}
    };
    struct WorldRegionVertex distant[] = {
        {40000, 0, 0}, {41000, 0, 0}, {40000, 0, 1000}
    };
    world_region_reset(zero);
    bad.triangles = invalid;
    assert(world_region_register(&bad) == WORLD_REGION_BAD_GEOMETRY);
    assert(world_region_count() == 0);
    bad.vertices = distant;
    bad.vertexCount = 3;
    bad.triangleCount = 1;
    bad.triangles = floorTriangles;
    assert(world_region_register(&bad) == WORLD_REGION_OUT_OF_RANGE);
    assert(world_region_count() == 0);
}

static void test_winding_and_rebased_window(void) {
    const struct WorldRegionTriangle inverted[] = {
        {0, 1, 2, SURFACE_DEFAULT, 0}
    };
    struct WorldRegionDesc desc = region(4, westVertices);
    struct WorldPoint origin = {1000000000LL, 0, 0};
    struct Surface *floor = NULL;
    world_region_reset(origin);
    desc.origin = origin;
    desc.triangles = inverted;
    desc.triangleCount = 1;
    assert(world_region_register(&desc) == WORLD_REGION_OK);
    assert(world_region_activate(4) == WORLD_REGION_OK);
    assert(cell_count(-1000, 0, SPATIAL_PARTITION_CEILS) > 0);
    assert(find_floor(-1000, 100, 0, &floor) == -11000.0f);
    assert(floor == NULL);
    world_region_reset(origin);
}

int main(void) {
    test_coordinate_limits();
    test_seam_and_lifecycle();
    test_rejection_is_atomic();
    test_winding_and_rebased_window();
    puts("world region collision/lifecycle: passed");
    return 0;
}
