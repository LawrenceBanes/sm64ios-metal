#ifndef RENDERING_GRAPH_NODE_H
#define RENDERING_GRAPH_NODE_H

#include <PR/ultratypes.h>

#include "engine/graph_node.h"

extern struct GraphNodeRoot *gCurGraphNodeRoot;
extern struct GraphNodeMasterList *gCurGraphNodeMasterList;
extern struct GraphNodePerspective *gCurGraphNodeCamFrustum;
extern struct GraphNodeCamera *gCurGraphNodeCamera;
extern struct GraphNodeObject *gCurGraphNodeObject;
extern struct GraphNodeHeldObject *gCurGraphNodeHeldObject;
extern u16 gAreaUpdateCounter;

// after processing an object, the type is reset to this
#define ANIM_TYPE_NONE                  0

// Not all parts have full animation: to save space, some animations only
// have xz, y, or no translation at all. All animations have rotations though
#define ANIM_TYPE_TRANSLATION           1
#define ANIM_TYPE_VERTICAL_TRANSLATION  2
#define ANIM_TYPE_LATERAL_TRANSLATION   3
#define ANIM_TYPE_NO_TRANSLATION        4

// Every animation includes rotation, after processing any of the above
// translation types the type is set to this
#define ANIM_TYPE_ROTATION              5

void geo_process_node_and_siblings(struct GraphNode *firstNode);
void geo_process_root(struct GraphNodeRoot *node, Vp *b, Vp *c, s32 clearColor);

#ifdef MOD_RUNTIME_HOST_TEST
#include <stddef.h>
#include <stdbool.h>
struct Object;
struct AllocOnlyPool;
/* Invoke the production object GeoLayout traversal with isolated host state. */
bool rendering_graph_host_process_object(struct Object *object,
                                         struct GraphNodeRoot *root,
                                         struct GraphNodePerspective *frustum,
                                         struct GraphNodeCamera *camera,
                                         struct GraphNodeMasterList *master,
                                         struct AllocOnlyPool *display_list_pool,
                                         void *gfx_scratch, size_t gfx_scratch_size);
unsigned int rendering_graph_host_display_list_submissions(void);
unsigned int rendering_graph_host_geo_callbacks(void);
unsigned int rendering_graph_host_transform_nodes(void);
unsigned int rendering_graph_host_animated_part_visits(void);
unsigned long long rendering_graph_host_animation_transform_hash(void);
#endif

#endif // RENDERING_GRAPH_NODE_H
