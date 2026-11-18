#include <ultra64.h>
#include "sm64.h"
#include "geo_commands.h"

#include "game/level_geo.h"
#include "game/geo_misc.h"
#include "game/camera.h"
#include "game/moving_texture.h"
#include "game/screen_transition.h"
#include "game/paintings.h"

#include "make_const_nonconst.h"

#include "levels/castle_grounds/header.h"
#if defined(IOS_CONNECTED_WORLD)
#include "levels/hyrule_field/header.h"
#include "build/hyrule_field_connection.h"
extern const Gfx castle_grounds_hyrule_connector_dl[];
#endif

#include "levels/castle_grounds/areas/1/11/geo.inc.c"
#include "levels/castle_grounds/areas/1/3/geo.inc.c"
#include "levels/castle_grounds/areas/1/7/geo.inc.c"
#include "levels/castle_grounds/areas/1/8/geo.inc.c"
#include "levels/castle_grounds/areas/1/geo.inc.c"

#ifdef IOS_SCENE_DIAGNOSTIC
const GeoLayout scene_diagnostic_geo[] = {
    GEO_NODE_SCREEN_AREA(10, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2),
    GEO_OPEN_NODE(),
        GEO_ZBUFFER(0),
        GEO_OPEN_NODE(),
            GEO_NODE_ORTHO(100),
            GEO_OPEN_NODE(),
                GEO_BACKGROUND_COLOR(0x19CF),
            GEO_CLOSE_NODE(),
        GEO_CLOSE_NODE(),
        GEO_ZBUFFER(1),
        GEO_OPEN_NODE(),
            GEO_CAMERA_FRUSTUM_WITH_FUNC(45, 100, 8000, geo_camera_fov),
            GEO_OPEN_NODE(),
                GEO_CAMERA(1, 0, 300, 1400, 0, 180, -700, geo_camera_main),
                GEO_OPEN_NODE(),
                    GEO_RENDER_OBJ(),
                GEO_CLOSE_NODE(),
            GEO_CLOSE_NODE(),
        GEO_CLOSE_NODE(),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
#endif
