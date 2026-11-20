#include <stdint.h>

unsigned int configFiltering = 0;

/* The standalone backend link test does not link gfx_pc.c. The full iOS
 * source-validation step compiles the real implementation; class zero keeps
 * this rendering-backend-only executable independent of gameplay state. */
uint32_t gfx_current_render_material_class(void) {
    return 0;
}
