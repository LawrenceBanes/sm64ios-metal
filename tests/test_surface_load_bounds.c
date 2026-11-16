#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Exercise the actual private loader helpers with tiny guarded arenas. Linker
 * GC removes unrelated level/object code from this host regression binary. */
#include "../src/engine/surface_load.c"

s32 gSurfaceNodesAllocated;
s32 gSurfacesAllocated;
s32 gNumStaticSurfaceNodes;
s32 gNumStaticSurfaces;

int main(void) {
    struct SurfaceNode nodes[1];
    struct Surface surfaces[1];
    s16 points[] = {100, 0, 100, 200, 0, 100, 100, 0, 200};
    s16 valid[] = {0, 2, 1};
    s16 spanning[] = {100, 0, 100, 1200, 0, 100, 100, 0, 200};
    s16 invalid[] = {0, 9, 1};
    s16 *indices;
    struct Surface *surface;

    sSurfaceNodePool = nodes;
    sSurfacePool = surfaces;
    sSurfaceNodePoolSize = 1;
    sSurfacePoolSize = 1;
    sCurrentCollisionVertexCount = 3;
    indices = invalid;
    assert(read_surface_data(points, &indices) == NULL);
    assert(gSurfacesAllocated == 0 && gSurfaceLoadRejectedCount == 1);

    indices = valid;
    surface = read_surface_data(points, &indices);
    assert(surface != NULL && surface->normal.y > 0.99f);
    assert(gSurfacesAllocated == 1);
    add_surface(surface, FALSE);
    assert(gSurfaceNodesAllocated == 1);
    assert(gStaticSurfacePartition[surface_partition_cell_index(100)]
                                  [surface_partition_cell_index(100)]
                                  [SPATIAL_PARTITION_FLOORS].next == nodes);

    indices = valid;
    assert(read_surface_data(points, &indices) == NULL);
    assert(gSurfacesAllocated == 1);
    assert(alloc_surface_node() == NULL);
    assert(gSurfaceNodesAllocated == 1);

    memset(gStaticSurfacePartition, 0, sizeof(gStaticSurfacePartition));
    gSurfaceNodesAllocated = 0;
    gSurfacesAllocated = 0;
    indices = valid;
    surface = read_surface_data(spanning, &indices);
    assert(surface != NULL);
    add_surface(surface, FALSE);
    assert(gSurfaceNodesAllocated == 0);
    assert(gStaticSurfacePartition[surface_partition_cell_index(100)]
                                  [surface_partition_cell_index(100)]
                                  [SPATIAL_PARTITION_FLOORS].next == NULL);
    puts("surface pool and index bounds: passed");
    return 0;
}
